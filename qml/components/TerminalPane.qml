pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    objectName: "terminalPane"
    color: Theme.terminal
    border.color: activeTerminal && activeTerminal.activeFocus
                  ? Theme.accentDim : Theme.borderSoft

    property int currentIndex: -1
    property int nextTabId: 1
    property var startupCommand: []
    property int startupTabCount: 1
    property bool performanceProbe: false

    function performanceSessionDiagnostics() {
        const diagnostics = []
        for (let index = 0; index < tabsModel.count; ++index) {
            const item = sessionRepeater.itemAt(index) as TerminalSession
            if (item) diagnostics.push(item.terminal.memoryDiagnostics())
        }
        return diagnostics
    }
    property var activeTerminal: null
    property var activeProfile: ({})
    property int splitCount: 1
    property bool multiSshVisible: false
    property string broadcastMode: "include"
    property string broadcastScope: "all"
    property var targetChecks: ({})
    property int broadcastRevision: 0
    readonly property int pageStart: Math.floor(Math.max(0, currentIndex) / splitCount) * splitCount
    readonly property var broadcastRows: { const revision = broadcastRevision; const count = tabCount; return collectBroadcastRows() }
    property string activeCatalogId: ""
    readonly property string activeFolder: {
        return SessionStore.revision >= 0 ? (SessionStore.getById(activeCatalogId).group || "") : ""
    }
    readonly property bool monitoringActive: currentIndex >= 0 && tabsModel.get(currentIndex).monitoring
    signal manageMacrosRequested(string folderPath)
    signal manageLocalProfilesRequested()
    signal tabClosed(string tabId)
    property string draggedTabId: ""
    property int tabDropIndex: -1
    property real tabDragViewportX: -1
    property int contextTabIndex: -1
    readonly property int tabCount: tabsModel.count
    readonly property string activeTabId: currentIndex >= 0 ? tabsModel.get(currentIndex).tabId : ""
    readonly property bool activeRemote: currentIndex >= 0 && (tabsModel.get(currentIndex).mode === "profile" || tabsModel.get(currentIndex).mode === "quick")
    readonly property bool securityPromptActive: {
        if (connectionError.visible || routeApproval.visible || multiBar.reviewVisible) return true
        for (let i = 0; i < sessionRepeater.count; ++i) {
            const session = sessionRepeater.itemAt(i) as TerminalSession
            if (session && session.securityPromptVisible) return true
        }
        return false
    }
    readonly property bool tabOverflow: tabsView.contentWidth > tabsView.width + 2
    readonly property int tabDragScrollDirection: {
        if (draggedTabId.length === 0 || tabDragViewportX < 0)
            return 0
        if (tabDragViewportX < 34)
            return -1
        if (tabDragViewportX > tabsView.width - 34)
            return 1
        return 0
    }

    ListModel {
        id: tabsModel
        dynamicRoles: true
    }

    function defaultTabData() {
        return {
            tabId: "tab-" + nextTabId++,
            mode: "local",
            label: "Local",
            launchCommand: "",
            profileName: "",
            catalogId: "",
            monitoring: AppSettings.monitoringByDefault,
            host: "",
            user: "",
            port: 22,
            identityKey: "",
            identityFile: "",
            proxyJump: "",
            forwardX11: false,
            agentForwarding: false,
            compression: false,
            backspaceMode: "default",
            psmpJson: "",
            localCommandJson: "[]",
            localProfileJson: "{}",
            zoom: 100,
            pinned: false,
            tabColor: ""
        }
    }

    function collectBroadcastRows() {
        const rows = []
        for (let i = 0; i < tabsModel.count; ++i) {
            const item = sessionRepeater.itemAt(i) as TerminalSession
            if (!item) continue
            const tab = tabsModel.get(i), terminal = item.terminal
            const tick = targetChecks[tab.tabId] === undefined ? broadcastMode === "exclude" : targetChecks[tab.tabId]
            const included = terminal.broadcastArmed && (broadcastMode === "exclude" ? !tick : tick)
            rows.push({id: tab.tabId, name: tab.label, host: tab.host || tab.launchCommand,
                remote: terminal.remoteSession, running: terminal.running, included: included,
                checked: broadcastMode === "exclude" ? !included : included,
                inView: i >= pageStart && i < pageStart + splitCount})
        }
        return rows
    }
    function terminalForId(id) {
        const item = sessionRepeater.itemAt(indexForTabId(id)) as TerminalSession
        return item ? item.terminal : null
    }
    function setTargetChecked(id, checked) {
        if (securityPromptActive) return
        const terminal = terminalForId(id)
        const include = broadcastMode === "exclude" ? !checked : checked
        if (!terminal || !terminal.setBroadcastArmed(include)) return
        const copy = Object.assign({}, targetChecks); copy[id] = checked; targetChecks = copy
        multiBar.cancelReview()
    }
    function isBroadcastIncluded(id) {
        for (const row of broadcastRows) if (row.id === id) return row.included
        return false
    }
    function setBroadcastMode(mode) {
        if (securityPromptActive || broadcastMode === mode) return
        for (const row of broadcastRows) { const terminal = terminalForId(row.id); if (terminal) terminal.setBroadcastArmed(false) }
        targetChecks = ({}); broadcastMode = mode; multiBar.cancelReview()
    }
    function selectBroadcastTargets(include) {
        if (securityPromptActive) return
        for (const row of broadcastRows) {
            if (row.remote && row.running && (broadcastScope === "all" || row.inView))
                setTargetChecked(row.id, broadcastMode === "exclude" ? !include : include)
        }
    }
    function clearBroadcastTargets() {
        for (const row of broadcastRows) { const terminal = terminalForId(row.id); if (terminal) terminal.setBroadcastArmed(false) }
        targetChecks = ({}); multiBar.cancelReview()
    }
    function broadcastTerminals() {
        const targets = []
        for (const row of broadcastRows) if (row.included && (broadcastScope === "all" || row.inView)) targets.push(terminalForId(row.id))
        return targets
    }
    function setSplitCount(count) {
        if (securityPromptActive || ![1, 2, 4, 6].includes(count)) return
        splitCount = count; multiBar.cancelReview(); scheduleActiveTerminalSync(false)
    }
    function pageSessions(direction) { activate(Math.max(0, Math.min(tabsModel.count - 1, pageStart + direction * splitCount))) }

    function activate(index) {
        if (securityPromptActive) return
        if (index < 0 || index >= tabsModel.count)
            return
        currentIndex = index
        scheduleActiveTerminalSync(true)
    }

    function scheduleActiveTerminalSync(revealTab) {
        const focusOwner = root.Window.activeFocusItem
        Qt.callLater(function() {
            // Never steal focus from a search field, dialog or another pane that
            // the user focused while this deferred model update was pending.
            syncActiveTerminal(root.Window.activeFocusItem === focusOwner)
            if (revealTab) tabsView.positionViewAtIndex(currentIndex, ListView.Contain)
        })
    }

    function syncActiveTerminal(restoreFocus) {
        const item = sessionRepeater.itemAt(currentIndex) as TerminalSession
        activeTerminal = item ? item.terminal : null
        activeProfile = profileForTab(currentIndex)
        activeCatalogId = currentIndex >= 0 ? tabsModel.get(currentIndex).catalogId : ""
        if (item && restoreFocus !== false && !securityPromptActive)
            item.focusTerminal()
    }

    function profileForTab(index) {
        if (index < 0 || index >= tabsModel.count)
            return ({})
        const tab = tabsModel.get(index)
        if (tab.mode === "profile") {
            const result = ({
                name: tab.profileName,
                host: tab.host,
                user: tab.user,
                port: tab.port,
                identityKey: tab.identityKey,
                identityFile: tab.identityFile,
                proxyJump: tab.proxyJump,
                forwardX11: tab.forwardX11,
                agentForwarding: tab.agentForwarding,
                compression: tab.compression,
                backspaceMode: tab.backspaceMode
            })
            if (tab.psmpJson) result.psmp = JSON.parse(tab.psmpJson)
            return result
        }
        let target = tab.launchCommand.trim()
        if (tab.mode !== "quick" || target.length === 0)
            return ({})
        if (target.toLowerCase().startsWith("ssh "))
            target = target.slice(4).trim()
        if (target.indexOf(" ") >= 0 || target.startsWith("-"))
            return ({unsupported: true})
        const separator = target.lastIndexOf("@")
        return ({
            name: tab.label,
            host: separator >= 0 ? target.slice(separator + 1) : target,
            user: separator >= 0 ? target.slice(0, separator) : "",
            port: 22,
            identityFile: "",
            proxyJump: "",
            compression: false,
            backspaceMode: "default"
        })
    }

    function newLocalSession() {
        newLocalProfile(LocalProfileStore.defaultProfileId)
    }
    function newLocalProfile(profileId) {
        if (securityPromptActive) return
        const profile = LocalProfileStore.profile(profileId)
        if (!profile.id) {
            connectionError.message = "This local profile is unavailable. Choose a default in Settings → Local profiles."
            connectionError.open()
            return
        }
        const tab = defaultTabData()
        tab.mode = "localProfile"
        tab.label = profile.name
        tab.localProfileJson = JSON.stringify(profile)
        tabsModel.append(tab)
        activate(tabsModel.count - 1)
    }
    function newCommandSession(command) {
        const tab = defaultTabData()
        tab.mode = "command"
        tab.label = command[0]
        tab.localCommandJson = JSON.stringify(command)
        tabsModel.append(tab)
        activate(tabsModel.count - 1)
    }

    function connectTo(target) {
        if (securityPromptActive) return
        const trimmed = target.trim()
        if (trimmed.length === 0)
            return
        const tab = defaultTabData()
        tab.mode = "quick"
        tab.launchCommand = trimmed
        tab.label = trimmed.toLowerCase().startsWith("ssh ")
                    ? trimmed.slice(4).trim() : trimmed
        tabsModel.append(tab)
        activate(tabsModel.count - 1)
    }

    function openProfile(profile) {
        if (securityPromptActive) return
        if (!profile || !profile.host)
            return
        const result = SessionStore.prepareProfile(JSON.parse(JSON.stringify(profile)), PersonalProfile.routingIdentity)
        if (result.error) { connectionError.message = result.error; connectionError.open(); return }
        if (result.fingerprint && !PersonalProfile.routeApproved(result.fingerprint)) {
            routeApproval.pending = result
            routeApproval.open()
            return
        }
        appendPreparedProfile(result.profile)
    }

    function appendPreparedProfile(profile) {
        const tab = defaultTabData()
        tab.mode = "profile"
        tab.profileName = profile.name || profile.host
        tab.catalogId = profile.id || ""
        tab.label = tab.profileName
        tab.host = profile.host || ""
        tab.user = profile.user || ""
        tab.port = profile.port || 22
        tab.identityKey = profile.identityKey || ""
        tab.identityFile = profile.identityFile || ""
        tab.proxyJump = profile.proxyJump || ""
        tab.forwardX11 = Boolean(profile.forwardX11)
        tab.agentForwarding = Boolean(profile.agentForwarding)
        tab.compression = Boolean(profile.compression)
        tab.backspaceMode = profile.backspaceMode || "default"
        tab.psmpJson = profile.psmp ? JSON.stringify(profile.psmp) : ""
        tabsModel.append(tab)
        activate(tabsModel.count - 1)
    }

    function toggleMonitoring() {
        if (!monitoringActive && activeProfile.psmp && !activeProfile.psmp.route.allowMonitoring) {
            connectionError.message = "Monitoring is disabled for this CyberArk route. No direct connection will be attempted."
            connectionError.open(); return
        }
        if (currentIndex >= 0) tabsModel.setProperty(currentIndex, "monitoring", !monitoringActive)
    }

    function closeTab(index) {
        if (securityPromptActive) return
        if (index < 0 || index >= tabsModel.count)
            return
        const oldCurrent = currentIndex
        const closedId = tabsModel.get(index).tabId
        activeTerminal = null
        tabsModel.remove(index)
        root.tabClosed(closedId)
        if (tabsModel.count === 0) {
            currentIndex = -1
            newLocalSession()
            return
        }
        if (oldCurrent > index)
            currentIndex = oldCurrent - 1
        else if (oldCurrent === index)
            currentIndex = Math.min(index, tabsModel.count - 1)
        scheduleActiveTerminalSync(false)
    }

    function pinnedCount() {
        let count = 0
        for (let i = 0; i < tabsModel.count; ++i)
            if (tabsModel.get(i).pinned) ++count
        return count
    }

    function setPinned(index, pinned) {
        if (securityPromptActive || index < 0 || index >= tabsModel.count
                || tabsModel.get(index).pinned === pinned) return
        const activeId = tabIdAt(currentIndex)
        tabsModel.setProperty(index, "pinned", pinned)
        const destination = pinned ? Math.max(0, pinnedCount() - 1) : pinnedCount()
        if (index !== destination) tabsModel.move(index, destination, 1)
        currentIndex = indexForTabId(activeId)
        scheduleActiveTerminalSync(true)
    }

    function closeMatching(keepIndex, side) {
        if (securityPromptActive) return
        const keepId = tabIdAt(keepIndex)
        const activeId = tabIdAt(currentIndex)
        for (let i = tabsModel.count - 1; i >= 0; --i) {
            const tab = tabsModel.get(i)
            const matches = side === "others" ? tab.tabId !== keepId
                          : side === "left" ? i < keepIndex : i > keepIndex
            if (matches && !tab.pinned) {
                root.tabClosed(tab.tabId)
                tabsModel.remove(i)
            }
        }
        if (tabsModel.count === 0) { currentIndex = -1; newLocalSession(); return }
        const activeAfter = indexForTabId(activeId)
        currentIndex = activeAfter >= 0 ? activeAfter : Math.max(0, indexForTabId(keepId))
        scheduleActiveTerminalSync(true)
    }

    function duplicateTab(index) {
        if (securityPromptActive || index < 0 || index >= tabsModel.count) return
        const source = tabsModel.get(index)
        const copy = defaultTabData()
        for (const key of ["mode", "launchCommand", "profileName", "catalogId", "host", "user",
                           "port", "identityKey", "identityFile", "proxyJump", "forwardX11", "agentForwarding",
                           "compression", "backspaceMode", "psmpJson", "localCommandJson", "localProfileJson", "zoom", "tabColor"])
            copy[key] = source[key]
        copy.label = source.label + " copy"
        copy.monitoring = false
        tabsModel.insert(index + 1, copy)
        activate(index + 1)
    }

    function renameTab(index, name) {
        const normalized = name.trim()
        if (index >= 0 && index < tabsModel.count && normalized.length > 0 && normalized.length <= 128)
            tabsModel.setProperty(index, "label", normalized)
    }

    function tabColorValue(name) {
        if (name === "red") return Theme.red
        if (name === "amber") return Theme.amber
        if (name === "blue") return Theme.blue
        if (name === "green") return Theme.green
        if (name === "violet") return Theme.violet
        return "transparent"
    }

    function tintedTabColor(baseColor, colorName, strength) {
        if (!colorName || colorName.length === 0)
            return baseColor
        const category = tabColorValue(colorName)
        return Qt.tint(baseColor, Qt.rgba(category.r, category.g,
                                          category.b, strength))
    }

    function tabIdAt(index) {
        return index >= 0 && index < tabsModel.count
               ? tabsModel.get(index).tabId : ""
    }

    function tabInfoAt(index) {
        if (index < 0 || index >= tabsModel.count) return ({})
        const tab = tabsModel.get(index)
        return ({tabId: tab.tabId, label: tab.label, pinned: tab.pinned,
                 tabColor: tab.tabColor, mode: tab.mode})
    }

    function setTabColor(index, colorName) {
        if (securityPromptActive || index < 0 || index >= tabsModel.count) return
        const allowed = ["", "red", "amber", "blue", "green", "violet"]
        tabsModel.setProperty(index, "tabColor", allowed.includes(colorName) ? colorName : "")
    }

    function indexForTabId(tabId) {
        for (let index = 0; index < tabsModel.count; ++index) {
            if (tabsModel.get(index).tabId === tabId)
                return index
        }
        return -1
    }

    function moveTab(from, to) {
        if (securityPromptActive || from < 0 || from >= tabsModel.count || to < 0 || to >= tabsModel.count
                || from === to)
            return false
        const activeId = tabIdAt(currentIndex)
        const sourcePinned = tabsModel.get(from).pinned
        const boundary = pinnedCount()
        const boundedTo = sourcePinned ? Math.min(to, Math.max(0, boundary - 1))
                                       : Math.max(to, boundary)
        tabsModel.move(from, boundedTo, 1)
        ++broadcastRevision // Visible-pane scope must follow order, even if the active page is unchanged.
        currentIndex = indexForTabId(activeId)
        scheduleActiveTerminalSync(true)
        return true
    }

    function moveTabById(tabId, destinationIndex) {
        return moveTab(indexForTabId(tabId), destinationIndex)
    }

    function scrollTabs(direction) {
        const maximum = Math.max(0, tabsView.contentWidth - tabsView.width)
        const distance = Math.max(130, tabsView.width * 0.72)
        tabsView.contentX = Math.max(0, Math.min(maximum,
                                                tabsView.contentX
                                                + direction * distance))
    }

    function finishTabDrag() {
        draggedTabId = ""
        tabDropIndex = -1
        tabDragViewportX = -1
    }

    function adjustZoom(index, steps) {
        if (index < 0 || index >= tabsModel.count)
            return
        const value = tabsModel.get(index).zoom
        tabsModel.setProperty(index, "zoom",
                              Math.max(50, Math.min(250, value + steps * 10)))
    }

    function openSearch() {
        const item = sessionRepeater.itemAt(currentIndex) as TerminalSession
        if (item)
            item.openSearch()
    }

    Component.onCompleted: {
        for (let index = 0; index < startupTabCount; ++index) {
            if (startupCommand.length > 0) newCommandSession(startupCommand)
            else newLocalSession()
        }
    }

    Timer {
        interval: 26
        repeat: true
        running: root.tabDragScrollDirection !== 0
        onTriggered: {
            const maximum = Math.max(0, tabsView.contentWidth - tabsView.width)
            tabsView.contentX = Math.max(0, Math.min(maximum,
                                  tabsView.contentX
                                  + root.tabDragScrollDirection * 9))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: tabHeader
            Layout.fillWidth: true
            implicitHeight: 41
            color: Theme.surface
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                spacing: 5

                AppButton { objectName: "previousSplitPage"; visible: root.splitCount > 1; text: "‹"; compact: true; implicitWidth: 26; enabled: root.pageStart > 0; onClicked: root.pageSessions(-1) }
                Text { visible: root.splitCount > 1; textFormat: Text.PlainText; text: (root.pageStart + 1) + "–" + Math.min(root.tabCount, root.pageStart + root.splitCount) + " / " + root.tabCount; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(9) }
                AppButton { objectName: "nextSplitPage"; visible: root.splitCount > 1; text: "›"; compact: true; implicitWidth: 26; enabled: root.pageStart + root.splitCount < root.tabCount; onClicked: root.pageSessions(1) }

                AppButton {
                    id: previousTabsButton
                    objectName: "previousTabsButton"
                    visible: root.tabOverflow
                    text: ""
                    iconText: "‹"
                    compact: true
                    implicitWidth: 27
                    Layout.leftMargin: 3
                    enabled: tabsView.contentX > 1
                    onClicked: root.scrollTabs(-1)
                    ToolTip.visible: hovered
                    ToolTip.text: "Earlier tabs"
                }

                ListView {
                    id: tabsView
                    objectName: "openTabsView"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: 1
                    model: tabsModel
                    currentIndex: root.currentIndex
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    interactive: root.draggedTabId.length === 0

                    delegate: Rectangle {
                        id: tabDelegate
                        required property int index
                        required property string tabId
                        required property string label
                        required property string mode
                        required property bool pinned
                        required property string tabColor
                        property bool dragInProgress: false
                        readonly property color categoryColor: root.tabColorValue(tabColor)
                        readonly property color baseColor: index === root.currentIndex
                                                           ? Theme.canvas
                                                           : root.draggedTabId.length > 0
                                                             && root.tabDropIndex === index
                                                             ? Theme.surfaceSelected
                                                           : tabHover.containsMouse
                                                             ? Theme.surfaceHover
                                                             : Theme.surface
                        readonly property real tintStrength: index === root.currentIndex
                                                             ? 0.34
                                                             : tabHover.containsMouse ? 0.30 : 0.23
                        readonly property bool processRunning: {
                            const revision = root.broadcastRevision
                            const terminal = root.terminalForId(tabId)
                            return terminal && terminal.running
                        }

                        objectName: "openTab:" + tabId

                        width: Math.min(190, Math.max(120, tabText.implicitWidth + 58))
                        height: ListView.view.height
                        opacity: dragInProgress ? 0.42 : 1
                        // Tabs are application chrome. Mixing terminal background
                        // with UI text makes light-UI/dark-terminal tabs unreadable.
                        color: root.tintedTabColor(baseColor, tabColor, tintStrength)
                        border.color: root.draggedTabId.length > 0
                                      && root.tabDropIndex === index
                                      ? Theme.accent
                                      : tabColor.length > 0
                                        ? Qt.rgba(categoryColor.r, categoryColor.g,
                                                  categoryColor.b,
                                                  index === root.currentIndex ? 0.85 : 0.52)
                                        : "transparent"
                        border.width: tabColor.length > 0 ? 1 : 0

                        Behavior on color { ColorAnimation { duration: 110 } }
                        Behavior on border.color { ColorAnimation { duration: 110 } }

                        Rectangle {
                            objectName: "tabCategoryStripe:" + tabDelegate.tabId
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: tabDelegate.tabColor.length > 0 ? 4 : 0
                            color: tabDelegate.categoryColor
                        }

                        RowLayout {
                            z: 1
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 4
                                color: tabDelegate.processRunning ? Theme.green : Theme.textFaint
                            }
                            Text {
                                visible: tabDelegate.pinned
                                text: "◆"
                                textFormat: Text.PlainText
                                color: Theme.accent
                                font.pixelSize: Theme.uiFontSize(8)
                                ToolTip.visible: pinHover.hovered
                                ToolTip.text: "Pinned tab"
                                HoverHandler { id: pinHover }
                            }
                            Text { textFormat: Text.PlainText;
                                id: tabText
                                Layout.fillWidth: true
                                text: tabDelegate.label
                                color: tabDelegate.tabColor.length > 0
                                       ? Theme.text
                                       : tabDelegate.index === root.currentIndex
                                       ? Theme.text : Theme.textMuted
                                elide: Text.ElideMiddle
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize(11)
                                font.weight: tabDelegate.index === root.currentIndex
                                             ? Font.DemiBold : Font.Medium
                            }
                            LockIcon {
                                objectName: "openTabSshLock:" + tabDelegate.tabId
                                visible: tabDelegate.mode === "profile" || tabDelegate.mode === "quick"
                                Layout.preferredWidth: 13; Layout.preferredHeight: 15
                            }
                            Text { textFormat: Text.PlainText;
                                text: "×"
                                color: closeHover.containsMouse ? Theme.text : Theme.textFaint
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize(13)

                                MouseArea {
                                    id: closeHover
                                    anchors.fill: parent
                                    anchors.margins: -7
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: function(mouse) {
                                        mouse.accepted = true
                                        root.closeTab(tabDelegate.index)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            z: 2
                            visible: tabDelegate.index === root.currentIndex
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: Theme.accent
                        }

                        MouseArea {
                            id: tabHover
                            anchors.fill: parent
                            z: 0
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    root.contextTabIndex = tabDelegate.index
                                    tabMenu.popup(tabDelegate, mouse.x, mouse.y)
                                } else root.activate(tabDelegate.index)
                            }
                        }

                        function updateTabDragPosition() {
                            if (!dragInProgress || !Overlay.overlay)
                                return
                            const overlayPoint = tabDelegate.mapToItem(
                                                   Overlay.overlay,
                                                   tabDragHandler.centroid.position.x,
                                                   tabDragHandler.centroid.position.y)
                            tabDragAnchor.x = overlayPoint.x
                            tabDragAnchor.y = overlayPoint.y
                            const viewPoint = tabsView.mapFromItem(Overlay.overlay,
                                                                  overlayPoint.x,
                                                                  overlayPoint.y)
                            root.tabDragViewportX = viewPoint.x
                            const hoveredIndex = tabsView.indexAt(
                                                   viewPoint.x + tabsView.contentX,
                                                   viewPoint.y + tabsView.contentY)
                            if (hoveredIndex >= 0)
                                root.tabDropIndex = hoveredIndex
                        }

                        DragHandler {
                            id: tabDragHandler
                            target: null
                            acceptedButtons: Qt.LeftButton
                            enabled: !closeHover.pressed
                            onActiveChanged: {
                                if (active) {
                                    tabDelegate.dragInProgress = true
                                    root.draggedTabId = tabDelegate.tabId
                                    root.tabDropIndex = tabDelegate.index
                                    tabDelegate.updateTabDragPosition()
                                } else {
                                    const movingId = root.draggedTabId
                                    const destinationIndex = root.tabDropIndex
                                    tabDragAnchor.Drag.drop()
                                    tabDelegate.dragInProgress = false
                                    tabDragAnchor.x = 0
                                    tabDragAnchor.y = 0
                                    root.finishTabDrag()
                                    if (movingId.length > 0 && destinationIndex >= 0) {
                                        // Keep the dragged delegate alive until Qt has finished
                                        // delivering the release event, then reorder the model.
                                        Qt.callLater(function() {
                                            root.moveTabById(movingId,
                                                             destinationIndex)
                                        })
                                    }
                                }
                            }
                            onCentroidChanged: tabDelegate.updateTabDragPosition()
                        }

                        Item {
                            id: tabDragAnchor
                            parent: Overlay.overlay
                            width: 1
                            height: 1
                            z: 100
                            Drag.active: tabDelegate.dragInProgress
                            Drag.source: tabDelegate
                            Drag.keys: ["bitshell/open-tab"]
                            Drag.supportedActions: Qt.MoveAction
                        }

                        Rectangle {
                            parent: Overlay.overlay
                            visible: tabDelegate.dragInProgress
                            x: tabDragAnchor.x + 9
                            y: tabDragAnchor.y + 8
                            z: 101
                            width: Math.min(210, tabDelegate.width)
                            height: 32
                            radius: Theme.radius
                            color: Theme.surfaceRaised
                            border.color: Theme.accent
                            opacity: 0.96
                            Text { textFormat: Text.PlainText;
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: tabDelegate.label
                                color: Theme.text
                                elide: Text.ElideMiddle
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize(10)
                                font.weight: Font.DemiBold
                            }
                        }

                        DropArea {
                            anchors.fill: parent
                            enabled: root.draggedTabId.length > 0
                                     && root.draggedTabId !== tabDelegate.tabId
                            keys: ["bitshell/open-tab"]
                            onEntered: root.tabDropIndex = tabDelegate.index
                        }

                    }

                    WheelHandler {
                        target: null
                        onWheel: function(event) {
                            const pixel = Math.abs(event.pixelDelta.x)
                                          > Math.abs(event.pixelDelta.y)
                                          ? event.pixelDelta.x : event.pixelDelta.y
                            const angle = Math.abs(event.angleDelta.x)
                                          > Math.abs(event.angleDelta.y)
                                          ? event.angleDelta.x : event.angleDelta.y
                            const delta = pixel !== 0 ? pixel : angle
                            const maximum = Math.max(0, tabsView.contentWidth
                                                       - tabsView.width)
                            tabsView.contentX = Math.max(0, Math.min(maximum,
                                                      tabsView.contentX - delta))
                            event.accepted = true
                        }
                    }
                }

                AppButton {
                    id: nextTabsButton
                    objectName: "nextTabsButton"
                    visible: root.tabOverflow
                    text: ""
                    iconText: "›"
                    compact: true
                    implicitWidth: 27
                    enabled: tabsView.contentX
                             < tabsView.contentWidth - tabsView.width - 1
                    onClicked: root.scrollTabs(1)
                    ToolTip.visible: hovered
                    ToolTip.text: "Later tabs"
                }

                LocalLaunchButton {
                    Layout.leftMargin: 1
                    enabled: !root.securityPromptActive
                    onLaunchDefault: root.newLocalSession()
                    onLaunchProfile: function(profileId) { root.newLocalProfile(profileId) }
                    onManageRequested: root.manageLocalProfilesRequested()
                }

                Item { Layout.preferredWidth: 6 }
            }
        }

        Item {
            id: terminalStack
            objectName: "terminalStage"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property int columns: root.splitCount === 1 ? 1 : root.splitCount === 6 && width >= 1050 ? 3 : 2
            readonly property int rows: Math.ceil(root.splitCount / columns)
            readonly property real cellWidth: (width - (columns - 1) * 4) / columns
            readonly property real cellHeight: (height - (rows - 1) * 4) / rows

            Repeater {
                id: sessionRepeater
                model: tabsModel

                delegate: TerminalSession {
                    id: sessionDelegate
                    required property int index
                    required property string tabId
                    required property string label
                    required property string mode
                    required property string launchCommand
                    required property string profileName
                    required property string host
                    required property string user
                    required property int port
                    required property string identityKey
                    required property string identityFile
                    required property string proxyJump
                    required property bool forwardX11
                    required property bool agentForwarding
                    required property bool compression
                    required property string backspaceMode
                    required property string psmpJson
                    required property string localCommandJson
                    required property string localProfileJson
                    required property int zoom

                    objectName: "terminalTile:" + tabId
                    readonly property int slot: index % root.splitCount
                    visible: index >= root.pageStart && index < root.pageStart + root.splitCount
                    x: (slot % terminalStack.columns) * (terminalStack.cellWidth + 4)
                    y: Math.floor(slot / terminalStack.columns) * (terminalStack.cellHeight + 4)
                    width: terminalStack.cellWidth; height: terminalStack.cellHeight
                    tiled: root.splitCount > 1
                    paneTitle: label
                    paneActive: index === root.currentIndex
                    performanceProbe: root.performanceProbe
                    showBroadcastChoice: root.multiSshVisible
                    broadcastIncluded: root.isBroadcastIncluded(tabId)
                    // Returning focus to the same pane must not undo manual tab scrolling.
                    // A modal may restore focus to a terminal that belonged to the
                    // previously selected single-tab page. Ignore that stale focus
                    // transition when the tile is no longer visible.
                    onActivated: if (sessionDelegate.visible && index !== root.currentIndex)
                                     root.activate(index)
                    onBroadcastSelected: function(included) { root.setTargetChecked(tabId, root.broadcastMode === "exclude" ? !included : included) }
                    launchMode: mode
                    command: sessionDelegate.launchCommand
                    localCommand: JSON.parse(localCommandJson)
                    localProfile: JSON.parse(localProfileJson)
                    zoomPercent: zoom
                    profile: {
                        const result = ({
                        name: profileName,
                        host: host,
                        user: user,
                        port: port,
                        identityKey: identityKey,
                        identityFile: identityFile,
                        proxyJump: proxyJump,
                        forwardX11: forwardX11,
                        agentForwarding: agentForwarding,
                        compression: compression,
                        backspaceMode: backspaceMode
                        })
                        if (psmpJson) result.psmp = JSON.parse(psmpJson)
                        return result
                    }
                    onZoomStepRequested: function(steps) {
                        root.adjustZoom(sessionDelegate.index, steps)
                    }
                    Connections {
                        target: sessionDelegate.terminal
                        function onBroadcastStateChanged() { ++root.broadcastRevision }
                        function onRunningChanged() { ++root.broadcastRevision }
                    }
                }

                onItemAdded: function(index, item) {
                    ++root.broadcastRevision
                    if (index === root.currentIndex)
                        root.scheduleActiveTerminalSync(false)
                }
                onItemRemoved: function(index, item) {
                    ++root.broadcastRevision
                    const sessionItem = item as TerminalSession
                    if (sessionItem && sessionItem.terminal === root.activeTerminal)
                        root.activeTerminal = null
                }
            }
        }

        MultiSshBar {
            id: multiBar; objectName: "multiSshBar"
            Layout.fillWidth: true; visible: root.multiSshVisible
            workspace: root
        }

        MacroBar {
            Layout.fillWidth: true
            visible: AppSettings.macroBarVisible
            terminal: root.activeTerminal
            folderPath: root.activeFolder
            onFolderPathChanged: overrideGroup = ""
            onManageRequested: function(path) { root.manageMacrosRequested(path) }
        }
        MonitorBar {
            Layout.fillWidth: true
            visible: root.monitoringActive
            monitoring: root.monitoringActive
            sessionRunning: Boolean(root.activeTerminal && root.activeTerminal.running)
            profile: root.activeProfile
            onCloseRequested: root.toggleMonitoring()
        }
        Rectangle {
            id: statusBar
            objectName: "terminalStatusBar"
            Layout.fillWidth: true
            implicitHeight: 32
            color: Theme.surface
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text { textFormat: Text.PlainText;
                    text: root.activeRemote ? "SSH" : "LOCAL"
                    color: Theme.accent
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                Text { textFormat: Text.PlainText;
                    objectName: "terminalStatusText"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: 330
                    text: root.activeTerminal ? root.activeTerminal.statusText : "No active terminal"
                    color: root.activeTerminal && root.activeTerminal.running
                           ? Theme.textMuted : Theme.red
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                Rectangle {
                    visible: statusBar.width >= 740
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 12
                    color: Theme.border
                }
                Text { textFormat: Text.PlainText;
                    visible: statusBar.width >= 740
                    text: "UTF-8"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                Text { textFormat: Text.PlainText;
                    visible: Boolean(root.activeTerminal)
                    text: root.activeTerminal && root.activeTerminal.backspaceMode === "bs"
                          ? "BKSP ^H" : "BKSP ^?"
                    color: root.activeTerminal && root.activeTerminal.backspaceMode === "bs"
                           ? Theme.amber : Theme.textFaint
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(9)
                    HoverHandler { id: backspaceModeHover }
                    ToolTip.visible: backspaceModeHover.hovered
                    ToolTip.text: root.activeTerminal
                                  && root.activeTerminal.backspaceMode === "bs"
                                  ? "Backspace sends BS (0x08)"
                                  : "Backspace sends DEL (0x7F)"
                }
                Text { textFormat: Text.PlainText;
                    visible: statusBar.width >= 640
                    text: root.activeTerminal
                          ? root.activeTerminal.columns + " × " + root.activeTerminal.rows : "—"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                Item { visible: statusBar.width >= 950; Layout.fillWidth: true }
                Text { textFormat: Text.PlainText;
                    visible: statusBar.width >= 950
                    text: root.activeTerminal
                          ? root.activeTerminal.scrollbackLines.toLocaleString(Qt.locale(), "f", 0)
                            + " history lines"
                          : ""
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                AppButton {
                    visible: root.activeTerminal && root.activeTerminal.scrollOffset > 0
                    text: "↓"; compact: true; implicitWidth: 25; implicitHeight: 22
                    onClicked: root.activeTerminal.scrollToBottom()
                    ToolTip.visible: hovered; ToolTip.text: "Return to live output"
                }
                AppButton {
                    objectName: "terminalFindButton"
                    text: "Find"; compact: true; implicitHeight: 22
                    enabled: Boolean(root.activeTerminal)
                    onClicked: root.openSearch()
                }
                PanelButton {
                    objectName: "highlightToggle"
                    text: "HL"; implicitHeight: 22
                    enabled: Boolean(root.activeTerminal)
                    panelVisible: Boolean(root.activeTerminal && root.activeTerminal.syntaxHighlightEnabled)
                    helpText: "Output highlighting"
                    onClicked: if (root.activeTerminal) root.activeTerminal.syntaxHighlightEnabled = !root.activeTerminal.syntaxHighlightEnabled
                }
                PanelButton {
                    objectName: "loggingToggle"
                    text: root.activeTerminal && root.activeTerminal.logEnabled
                          && root.activeTerminal.logFilePath.length > 0 ? "● REC" : "LOG"
                    implicitHeight: 22
                    enabled: Boolean(root.activeTerminal)
                    panelVisible: Boolean(root.activeTerminal && root.activeTerminal.logEnabled)
                    helpText: !root.activeTerminal ? "No active terminal"
                              : root.activeTerminal.logError || root.activeTerminal.logFilePath || "Session logging"
                    onClicked: if (root.activeTerminal) root.activeTerminal.logEnabled = !root.activeTerminal.logEnabled
                }
                AppButton {
                    visible: statusBar.width >= 720
                    text: "−"; compact: true; implicitWidth: 25; implicitHeight: 22
                    onClicked: root.adjustZoom(root.currentIndex, -1)
                }
                Text {
                    visible: statusBar.width >= 720
                    text: root.currentIndex >= 0 ? tabsModel.get(root.currentIndex).zoom + "%" : "—"
                    textFormat: Text.PlainText; color: Theme.textMuted
                    font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(8)
                }
                AppButton {
                    visible: statusBar.width >= 720
                    text: "+"; compact: true; implicitWidth: 25; implicitHeight: 22
                    onClicked: root.adjustZoom(root.currentIndex, 1)
                }
                PanelButton {
                    objectName: "multiSshToggle"
                    text: "Multi SSH"
                    implicitHeight: 22
                    panelVisible: root.multiSshVisible
                    enabled: !root.securityPromptActive
                    helpText: "Command bar for explicitly selected SSH terminals"
                    onClicked: {
                        root.multiSshVisible = !root.multiSshVisible
                        if (!root.multiSshVisible) root.clearBroadcastTargets()
                    }
                }
                PanelButton {
                    objectName: "macroBarToggle"
                    text: "Macros"
                    implicitHeight: 22
                    panelVisible: AppSettings.macroBarVisible
                    helpText: panelVisible ? "Hide macro buttons" : "Show macro buttons"
                    onClicked: AppSettings.macroBarVisible = !AppSettings.macroBarVisible
                }
                PanelButton {
                    objectName: "monitoringToggle"
                    text: "Monitor"
                    implicitHeight: 22
                    panelVisible: root.monitoringActive
                    enabled: root.currentIndex >= 0 && !root.securityPromptActive
                    helpText: panelVisible ? "Stop monitoring this session" : "Monitor this session"
                    onClicked: root.toggleMonitoring()
                }
            }
        }
    }

    AppMenu {
        id: tabMenu
        objectName: "openTabContextMenu"
        readonly property bool valid: root.contextTabIndex >= 0 && root.contextTabIndex < tabsModel.count
        AppMenuItem {
            objectName: "pinTabMenuItem"
            text: tabMenu.valid && tabsModel.get(root.contextTabIndex).pinned ? "Unpin tab" : "Pin tab"
            onTriggered: if (tabMenu.valid) root.setPinned(root.contextTabIndex,
                                                           !tabsModel.get(root.contextTabIndex).pinned)
        }
        AppMenuItem {
            text: "Rename tab…"
            onTriggered: if (tabMenu.valid) {
                renameField.text = tabsModel.get(root.contextTabIndex).label
                renameDialog.open(); renameField.selectAll(); renameField.forceActiveFocus()
            }
        }
        AppMenuItem { text: "Duplicate tab"; onTriggered: root.duplicateTab(root.contextTabIndex) }
        AppMenuSeparator {}
        AppMenu {
            id: categoryMenu
            objectName: "tabCategoryMenu"
            title: "Category"
            AppMenuItem {
                objectName: "tabCategoryNone"
                text: "No category"; showSwatch: true
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === ""
                onTriggered: root.setTabColor(root.contextTabIndex, "")
            }
            AppMenuItem {
                objectName: "tabCategoryRed"
                text: "Red"; showSwatch: true; swatchColor: Theme.red
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === "red"
                onTriggered: root.setTabColor(root.contextTabIndex, "red")
            }
            AppMenuItem {
                objectName: "tabCategoryAmber"
                text: "Amber"; showSwatch: true; swatchColor: Theme.amber
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === "amber"
                onTriggered: root.setTabColor(root.contextTabIndex, "amber")
            }
            AppMenuItem {
                objectName: "tabCategoryBlue"
                text: "Blue"; showSwatch: true; swatchColor: Theme.blue
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === "blue"
                onTriggered: root.setTabColor(root.contextTabIndex, "blue")
            }
            AppMenuItem {
                objectName: "tabCategoryGreen"
                text: "Green"; showSwatch: true; swatchColor: Theme.green
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === "green"
                onTriggered: root.setTabColor(root.contextTabIndex, "green")
            }
            AppMenuItem {
                objectName: "tabCategoryViolet"
                text: "Violet"; showSwatch: true; swatchColor: Theme.violet
                selected: tabMenu.valid && tabsModel.get(root.contextTabIndex).tabColor === "violet"
                onTriggered: root.setTabColor(root.contextTabIndex, "violet")
            }
        }
        AppMenuSeparator {}
        AppMenuItem { text: "Close tabs to the left"; onTriggered: root.closeMatching(root.contextTabIndex, "left") }
        AppMenuItem { text: "Close tabs to the right"; onTriggered: root.closeMatching(root.contextTabIndex, "right") }
        AppMenuItem { text: "Close other tabs"; onTriggered: root.closeMatching(root.contextTabIndex, "others") }
        AppMenuItem { text: "Close tab"; onTriggered: root.closeTab(root.contextTabIndex) }
    }

    Dialog {
        id: renameDialog
        objectName: "renameTabDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(410, parent.width - 40)
        title: "Rename tab"
        modal: true; focus: true
        standardButtons: Dialog.Save | Dialog.Cancel
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: AppTextField {
            id: renameField
            objectName: "renameTabField"
            placeholderText: "Tab title"
            maximumLength: 128
            onAccepted: renameDialog.accept()
        }
        onAccepted: root.renameTab(root.contextTabIndex, renameField.text)
    }

    Dialog {
        id: connectionError; objectName: "connectionErrorDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        property string message: ""
        title: "Connection blocked"; modal: true; focus: true; width: Math.min(520, parent.width - 40)
        standardButtons: Dialog.Ok
        contentItem: Text { text: connectionError.message; textFormat: Text.PlainText; color: Theme.text; wrapMode: Text.Wrap }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
    }
    Dialog {
        id: routeApproval; objectName: "routeApprovalDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        property var pending: ({})
        property bool approved: false
        title: "Review CyberArk gateway"; modal: true; focus: true
        width: Math.min(600, parent.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { approved = false; standardButton(Dialog.Cancel).forceActiveFocus() }
        onAccepted: {
            approved = PersonalProfile.approveRoute(pending.fingerprint)
            if (!approved) { connectionError.message = PersonalProfile.lastError; connectionError.open() }
        }
        onClosed: {
            const snapshot = pending
            pending = ({})
            if (approved) { approved = false; root.appendPreparedProfile(snapshot.profile) }
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; color: Theme.text; wrapMode: Text.Wrap
                text: "This route will send your employee identity to the gateway below. Approve only a gateway you recognize. This does not approve its SSH host key."
            }
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; color: Theme.accent; wrapMode: Text.WrapAnywhere
                text: routeApproval.pending.profile ? "Target: " + routeApproval.pending.profile.host
                    + "\nGateway: " + routeApproval.pending.profile.psmp.route.host + ":" + (routeApproval.pending.profile.psmp.route.port || 22)
                    + "\nLogin: " + routeApproval.pending.login : ""
            }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; text: "Gateway host-key verification stays mandatory. Final-target verification is handled by CyberArk. SFTP/monitoring use this same route only when explicitly enabled." }
        }
    }

    Shortcut {
        sequence: AppSettings.keyBindings.find
        enabled: Boolean(root.activeTerminal && root.activeTerminal.activeFocus)
        onActivated: root.openSearch()
    }
    Shortcut {
        sequence: AppSettings.keyBindings.newTab
        enabled: !root.securityPromptActive
        onActivated: root.newLocalSession()
    }
    Shortcut {
        sequence: AppSettings.keyBindings.closeTab
        enabled: !root.securityPromptActive
        onActivated: root.closeTab(root.currentIndex)
    }
    Shortcut {
        sequence: AppSettings.keyBindings.nextTab
        enabled: !root.securityPromptActive
        onActivated: {
            if (tabsModel.count > 1)
                root.activate((root.currentIndex + 1) % tabsModel.count)
        }
    }
}
