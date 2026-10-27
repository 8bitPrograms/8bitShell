pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    objectName: "catalogSidebar"

    color: Theme.surface
    border.color: Theme.borderSoft
    implicitWidth: 272

    signal openSessionRequested(var profile)
    signal addSessionRequested(string folder)
    signal editSessionRequested(int index)
    signal settingsRequested()
    signal profileRequested()
    signal routesRequested()
    signal folderConnectionRequested(string path)
    signal manageMacrosRequested(string path)

    property bool selectionMode: false
    property var selectedNodes: ({})
    readonly property int selectedCount: Object.keys(selectedNodes).length
    property var pendingDeletion: ({})
    property string folderDialogMode: "create"
    property string folderDialogPath: ""
    property string noticeText: ""
    property alias filterText: searchField.text
    property bool catalogDragActive: false
    property string catalogDragKind: ""
    property string catalogDragPath: ""
    property int catalogDragIndex: -1
    property string catalogDragName: ""
    property real catalogDragViewportY: -1
    readonly property int catalogDragScrollDirection: {
        if (!catalogDragActive || catalogDragViewportY < 0)
            return 0
        if (catalogDragViewportY < 46)
            return -1
        if (catalogDragViewportY > sessionsList.height - 46)
            return 1
        return 0
    }

    // treeRows() returns a value snapshot. Keep an explicit snapshot property so a
    // move with the same number of visible rows still resets ListView's delegates.
    property var treeItems: []
    property var inspectedNode: null
    readonly property var inspectorFields: {
        const node = inspectedNode
        const empty = [
            { label: "Name", value: "" },
            { label: "Host", value: "" },
            { label: "User name", value: "" },
            { label: "Port", value: "" },
            { label: "Protocol", value: "" },
            { label: "Folder", value: "" }
        ]
        if (!node || !node.kind)
            return empty
        if (node.kind === "folder") {
            const count = Number(node.totalCount || 0)
            return [
                { label: "Name", value: String(node.name || "") },
                { label: "Host", value: count === 1 ? "1 session" : count + " sessions" },
                { label: "User name", value: "" },
                { label: "Port", value: "" },
                { label: "Protocol", value: "Folder" },
                { label: "Folder", value: String(node.path || "") }
            ]
        }
        return [
            { label: "Name", value: String(node.name || "") },
            { label: "Host", value: String(node.host || "") },
            { label: "User name", value: String(node.user || "") },
            { label: "Port", value: String(Number(node.port || 22)) },
            { label: "Protocol", value: "SSH" },
            { label: "Folder", value: String(node.group || "") }
        ]
    }

    function inspectNode(node) {
        if (!node || !node.kind)
            return
        inspectedNode = {
            kind: node.kind,
            key: node.key,
            name: node.name || "",
            host: node.host || "",
            user: node.user || "",
            port: node.port || 22,
            group: node.group || "",
            path: node.path || node.group || "",
            totalCount: node.totalCount || 0
        }
    }
    readonly property int searchResultCount: {
        if (searchField.text.length === 0)
            return SessionStore.count
        let matches = 0
        for (let index = 0; index < root.treeItems.length; ++index) {
            if (root.treeItems[index].kind === "session")
                ++matches
        }
        return matches
    }

    function showNotice(message) {
        noticeText = message
        noticeTimer.restart()
    }

    function toggleNodeSelection(node) {
        const next = Object.assign({}, selectedNodes)
        if (next[node.key]) delete next[node.key]
        else next[node.key] = node
        selectedNodes = next
    }

    function selectVisibleSessions() {
        // Search-result ancestors are context, not selected subtrees. Selecting
        // results must never silently include nonmatching children of a folder.
        const next = Object.assign({}, selectedNodes)
        for (const node of treeItems)
            if (node.kind === "session") next[node.key] = node
        selectedNodes = next
    }

    function requestDeletion(ids, folders, all) {
        const preview = SessionStore.deletionPreview(ids, folders, all)
        if (!preview.valid) { showNotice(preview.error); return }
        pendingDeletion = {ids: ids.slice(), folders: folders.slice(), all: all,
                           revision: preview.revision, sessions: preview.sessions,
                           folderCount: preview.folders}
        deleteEntriesDialog.open()
    }

    function requestSelectedDeletion() {
        const ids = [], folders = []
        for (const key of Object.keys(selectedNodes)) {
            const node = selectedNodes[key]
            if (node.kind === "folder") folders.push(node.path)
            else ids.push(node.id)
        }
        requestDeletion(ids, folders, false)
    }

    function confirmDeletion() {
        const pending = pendingDeletion
        const removed = SessionStore.deleteEntries(pending.ids, pending.folders,
                                                    pending.all, pending.revision)
        deleteEntriesDialog.close()
        if (!removed) { showNotice(SessionStore.lastError); return }
        selectedNodes = ({})
        const existing = new Set(SessionStore.folderPaths())
        setExpandedFolders(AppSettings.expandedSessionFolders.filter(path => existing.has(path)))
        showNotice(pending.sessions + " sessions and " + pending.folderCount + " folders deleted")
    }

    Keys.onPressed: function(event) {
        if (!root.selectionMode || searchField.activeFocus) return
        if (event.key === Qt.Key_Delete && root.selectedCount > 0) {
            root.requestSelectedDeletion()
            event.accepted = true
        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            root.selectVisibleSessions()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            root.selectionMode = false
            root.selectedNodes = ({})
            event.accepted = true
        }
    }

    function refreshTreeItems(resetPosition) {
        const previousY = sessionsList.contentY
        treeItems = SessionStore.treeRows(searchField.text,
                                          AppSettings.expandedSessionFolders)
        if (inspectedNode && inspectedNode.key) {
            const key = inspectedNode.key
            const fresh = treeItems.find(function(item) { return item.key === key })
            if (fresh)
                inspectNode(fresh)
        }
        Qt.callLater(function() {
            sessionsList.forceLayout()
            if (resetPosition) {
                sessionsList.positionViewAtBeginning()
                return
            }
            const minimum = sessionsList.originY
            const maximum = Math.max(minimum,
                                     minimum + sessionsList.contentHeight
                                     - sessionsList.height)
            sessionsList.contentY = Math.max(minimum,
                                             Math.min(maximum, previousY))
        })
    }

    function openCatalogMenu() {
        catalogMenu.openBelow(catalogButton, true)
    }

    function triggerCatalogMenuButton() {
        catalogButton.click()
    }

    function openFirstTreeMenu(kind) {
        let row = -1
        for (let index = 0; index < treeItems.length; ++index) {
            if (treeItems[index].kind === kind) {
                row = index
                break
            }
        }
        if (row < 0)
            return
        sessionsList.positionViewAtIndex(row, ListView.Contain)
        sessionsList.forceLayout()
        Qt.callLater(function() {
            const item = sessionsList.itemAtIndex(row)
            if (item)
                item.triggerMenuButton()
        })
    }

    function setExpandedFolders(paths) {
        const oldY = sessionsList.contentY
        AppSettings.expandedSessionFolders = paths
        Qt.callLater(function() {
            const maximum = Math.max(sessionsList.originY,
                                     sessionsList.originY
                                     + sessionsList.contentHeight - sessionsList.height)
            sessionsList.contentY = Math.min(maximum,
                                             Math.max(sessionsList.originY, oldY))
        })
    }

    function toggleFolder(path) {
        const paths = AppSettings.expandedSessionFolders.slice()
        const index = paths.indexOf(path)
        if (index >= 0)
            paths.splice(index, 1)
        else
            paths.push(path)
        setExpandedFolders(paths)
    }

    function openFolderCreate(parentPath) {
        folderDialogMode = "create"
        folderDialogPath = parentPath
        folderNameField.text = ""
        folderDialog.open()
        folderNameField.forceActiveFocus()
    }

    function openFolderRename(path, name) {
        folderDialogMode = "rename"
        folderDialogPath = path
        folderNameField.text = name
        folderDialog.open()
        folderNameField.selectAll()
        folderNameField.forceActiveFocus()
    }

    function leafName(path) {
        const separator = path.lastIndexOf("/")
        return separator < 0 ? path : path.slice(separator + 1)
    }

    function parentPath(path) {
        const separator = path.lastIndexOf("/")
        return separator < 0 ? "" : path.slice(0, separator)
    }

    function joinedPath(parent, name) {
        return parent.length > 0 ? parent + "/" + name : name
    }

    function remapExpandedTree(oldPath, newPath) {
        const separator = oldPath.lastIndexOf("/")
        const paths = AppSettings.expandedSessionFolders.map(function(path) {
            if (path === oldPath)
                return newPath
            if (path.startsWith(oldPath + "/"))
                return newPath + path.slice(oldPath.length)
            return path
        })
        setExpandedFolders(paths)
    }

    function remapExpandedFolder(oldPath, newName) {
        remapExpandedTree(oldPath, joinedPath(parentPath(oldPath), newName))
    }

    function canDropCatalogNode(destinationPath) {
        if (!catalogDragActive)
            return false
        if (catalogDragKind === "session")
            return catalogDragIndex >= 0 && catalogDragPath !== destinationPath
        if (catalogDragKind !== "folder" || catalogDragPath.length === 0)
            return false

        const source = catalogDragPath.toLocaleLowerCase()
        const destination = destinationPath.toLocaleLowerCase()
        const sourceParent = parentPath(catalogDragPath).toLocaleLowerCase()
        return destination !== source
               && !destination.startsWith(source + "/")
               && destination !== sourceParent
    }

    function finishCatalogDrag() {
        catalogDragActive = false
        catalogDragKind = ""
        catalogDragPath = ""
        catalogDragIndex = -1
        catalogDragName = ""
        catalogDragViewportY = -1
    }

    function ensureExpanded(path) {
        if (path.length === 0)
            return
        const paths = AppSettings.expandedSessionFolders.slice()
        if (paths.indexOf(path) < 0) {
            paths.push(path)
            setExpandedFolders(paths)
        }
    }

    function moveCatalogNode(kind, path, sourceIndex, destinationPath) {
        let moved = false
        if (kind === "session") {
            moved = SessionStore.moveSession(sourceIndex, destinationPath)
            if (moved)
                ensureExpanded(destinationPath)
        } else if (kind === "folder") {
            moved = SessionStore.moveFolder(path, destinationPath)
            if (moved) {
                const newPath = joinedPath(destinationPath, leafName(path))
                remapExpandedTree(path, newPath)
                ensureExpanded(destinationPath)
            }
        }

        if (moved) {
            const destinationLabel = destinationPath.length > 0
                                     ? destinationPath : "root"
            showNotice("Moved to " + destinationLabel)
        } else {
            showNotice(SessionStore.lastError.length > 0
                       ? SessionStore.lastError : "Could not move this item")
        }
        finishCatalogDrag()
    }

    function importOpenSsh() {
        const count = SessionStore.importOpenSshConfig()
        showNotice(count > 0 ? SessionStore.lastImportSummary
                             : (SessionStore.lastError.length > 0
                                ? SessionStore.lastError
                                : SessionStore.lastImportSummary))
    }

    Timer {
        id: noticeTimer
        interval: 5000
        onTriggered: root.noticeText = ""
    }

    Connections {
        target: SessionStore
        function onRevisionChanged() { root.refreshTreeItems(false) }
    }

    Connections {
        target: AppSettings
        function onExpandedSessionFoldersChanged() {
            root.refreshTreeItems(false)
        }
    }

    Component.onCompleted: refreshTreeItems(false)

    Timer {
        interval: 28
        repeat: true
        running: root.catalogDragScrollDirection !== 0
        onTriggered: {
            const minimum = sessionsList.originY
            const maximum = Math.max(minimum,
                                     minimum + sessionsList.contentHeight
                                     - sessionsList.height)
            const next = sessionsList.contentY
                         + root.catalogDragScrollDirection * 9
            sessionsList.contentY = Math.max(minimum, Math.min(maximum, next))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 6
        anchors.bottomMargin: 4
        spacing: 4

        Item {
            Layout.fillWidth: true
            implicitHeight: 0

            AppMenu {
                id: catalogMenu
                objectName: "catalogContextMenu"

                AppMenuItem {
                    text: "New SSH session"
                    onTriggered: root.addSessionRequested("")
                }
                AppMenuItem {
                    text: "New folder"
                    onTriggered: root.openFolderCreate("")
                }
                AppMenuItem {
                    text: "Macro groups…"
                    onTriggered: root.manageMacrosRequested("")
                }
                AppMenuItem {
                    text: "Connection routes…"
                    objectName: "connectionRoutesMenuItem"
                    visible: PersonalProfile.cyberArkEnabled
                    onTriggered: root.routesRequested()
                }
                AppMenuSeparator {}
                AppMenuItem {
                    text: "Session YAML import / export…"
                    objectName: "sessionYamlMenuItem"
                    onTriggered: sessionsYamlDialog.open()
                }
                AppMenuItem {
                    text: "Import mRemoteNG tree…"
                    onTriggered: mRemoteImportDialog.open()
                }
                AppMenuItem {
                    text: "Import ~/.ssh/config"
                    onTriggered: root.importOpenSsh()
                }
                AppMenuSeparator {}
                AppMenuItem {
                    text: "Expand all folders"
                    enabled: SessionStore.folderCount > 0
                    onTriggered: root.setExpandedFolders(SessionStore.folderPaths())
                }
                AppMenuItem {
                    text: "Collapse all folders"
                    enabled: AppSettings.expandedSessionFolders.length > 0
                    onTriggered: root.setExpandedFolders([])
                }
                AppMenuItem {
                    text: "Reload catalog"
                    onTriggered: {
                        SessionStore.reload()
                        root.showNotice(SessionStore.lastError.length > 0
                                        ? SessionStore.lastError : "Catalog reloaded")
                    }
                }
                AppMenuSeparator {}
                AppMenuItem {
                    objectName: "selectSessionsMenuItem"
                    text: root.selectionMode ? "Finish selecting" : "Select sessions / folders…"
                    onTriggered: {
                        root.selectionMode = !root.selectionMode
                        root.selectedNodes = ({})
                    }
                }
                AppMenuItem {
                    objectName: "deleteAllSessionsMenuItem"
                    text: "Delete all saved sessions / folders…"
                    enabled: SessionStore.count > 0 || SessionStore.folderCount > 0
                    onTriggered: root.requestDeletion([], [], true)
                }
            }

        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: searchField
                Layout.fillWidth: true
                implicitHeight: 26
            placeholderText: "Search sessions"
            color: Theme.text
            placeholderTextColor: Theme.textFaint
            selectionColor: Theme.accentDim
            selectedTextColor: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(10)
            leftPadding: 26
            rightPadding: clearSearch.visible ? 30 : 10
            selectByMouse: true

            onTextChanged: root.refreshTreeItems(true)

            background: Rectangle {
                radius: Theme.radius
                color: Theme.canvas
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
            }

            SearchIcon {
                color: searchField.activeFocus ? Theme.accent : Theme.textFaint
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
            }

                Text { textFormat: Text.PlainText;
                    id: clearSearch
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    color: clearSearchMouse.containsMouse ? Theme.text : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(15)

                    MouseArea {
                        id: clearSearchMouse
                        anchors.fill: parent
                        anchors.margins: -7
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: searchField.clear()
                    }
                }
            }

            AppButton {
                id: catalogButton
                objectName: "catalogMenuButton"
                text: ""
                iconText: "⋯"
                compact: true
                implicitWidth: 26
                implicitHeight: 26
                onClicked: root.openCatalogMenu()
                ToolTip.visible: hovered
                ToolTip.text: "Catalog actions"
            }
            AppButton {
                objectName: "newSessionBesideSearch"
                text: ""
                iconText: "＋"
                compact: true
                implicitWidth: 26
                implicitHeight: 26
                onClicked: root.addSessionRequested("")
                ToolTip.visible: hovered
                ToolTip.text: "Save a new SSH session"
            }
        }

        Rectangle {
            id: rootDropTarget
            objectName: "catalogRootDropTarget"
            Layout.fillWidth: true
            implicitHeight: 18
            Layout.topMargin: 2
            radius: Theme.radiusSmall
            color: rootDrop.containsDrag
                   ? (root.canDropCatalogNode("") ? Theme.accentDim
                                                   : Qt.rgba(0.95, 0.27, 0.34, 0.13))
                   : root.catalogDragActive ? Theme.canvas : "transparent"
            border.color: rootDrop.containsDrag
                          ? (root.canDropCatalogNode("") ? Theme.accent : Theme.red)
                          : root.catalogDragActive ? Theme.border : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.catalogDragActive ? 8 : 0
                anchors.rightMargin: root.catalogDragActive ? 7 : 0
                spacing: 6

                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: root.catalogDragActive
                          ? (rootDrop.containsDrag ? "RELEASE TO MOVE ‘"
                                                    : "DROP HERE TO MOVE ‘")
                            + root.catalogDragName + "’ TO ROOT"
                          : searchField.text.length > 0 ? "SEARCH RESULTS" : "SAVED SESSIONS"
                    color: rootDrop.containsDrag && root.canDropCatalogNode("")
                           ? Theme.accentBright : Theme.textMuted
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.weight: Font.DemiBold
                }

                Text { textFormat: Text.PlainText;
                    visible: !root.catalogDragActive && SessionStore.folderCount > 0
                             && searchField.text.length === 0
                    text: SessionStore.folderCount + " folders"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                }

                Rectangle {
                    visible: !root.catalogDragActive
                    implicitWidth: Math.max(22, sessionCount.implicitWidth + 10)
                    implicitHeight: 18
                    radius: 9
                    color: Theme.surfaceHover
                    Text { textFormat: Text.PlainText;
                        id: sessionCount
                        anchors.centerIn: parent
                        text: searchField.text.length > 0
                              ? root.searchResultCount + "/" + SessionStore.count
                              : SessionStore.count
                        color: Theme.textFaint
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(8)
                    }
                }
            }

            DropArea {
                id: rootDrop
                objectName: "catalogRootDropArea"
                anchors.fill: parent
                enabled: root.catalogDragActive
                keys: ["bitshell/catalog-node"]

                onEntered: function(drag) {
                    if (!root.canDropCatalogNode(""))
                        drag.accepted = false
                }
                onDropped: function(drop) {
                    if (!root.canDropCatalogNode("")) {
                        drop.accepted = false
                        return
                    }
                    drop.acceptProposedAction()
                    root.moveCatalogNode(root.catalogDragKind,
                                         root.catalogDragPath,
                                         root.catalogDragIndex,
                                         "")
                }
            }
        }

        RowLayout {
            visible: root.selectionMode
            Layout.fillWidth: true
            spacing: 5
            Text {
                textFormat: Text.PlainText
                text: root.selectedCount + " selected"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(10)
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "selectVisibleSessionsButton"
                text: "Visible"
                compact: true
                ToolTip.visible: hovered
                ToolTip.text: "Select visible sessions only · Ctrl+A"
                onClicked: root.selectVisibleSessions()
            }
            AppButton {
                objectName: "deleteSelectedSessionsButton"
                text: "Delete…"
                compact: true
                enabled: root.selectedCount > 0
                onClicked: root.requestSelectedDeletion()
            }
            AppButton {
                objectName: "finishSessionSelectionButton"
                text: "×"
                compact: true
                implicitWidth: 26
                onClicked: { root.selectionMode = false; root.selectedNodes = ({}) }
                ToolTip.visible: hovered
                ToolTip.text: "Finish selecting"
            }
        }

        Item {
            id: catalogArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: sessionsList
                objectName: "sessionTreeList"
                anchors.fill: parent
                clip: true
                spacing: 0
                model: root.treeItems
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 480
                ScrollBar.vertical: ScrollBar { id: catalogScrollBar; policy: ScrollBar.AsNeeded }

                delegate: SessionTreeRow {
                    // Keep row actions out of the scrollbar hit area.
                    width: Math.max(0, sessionsList.width - catalogScrollBar.width - 4)
                    selectionMode: root.selectionMode
                    nodeSelected: !!root.selectedNodes[modelData.key]
                    inspected: root.inspectedNode && root.inspectedNode.key === modelData.key
                    onInspectRequested: function(node) { root.inspectNode(node) }
                    onSelectionToggleRequested: function(node) { root.toggleNodeSelection(node) }
                    onDeleteSelectionRequested: root.requestSelectedDeletion()
                    catalogDragActive: root.catalogDragActive
                    catalogDragKind: root.catalogDragKind
                    catalogDragPath: root.catalogDragPath
                    catalogDragIndex: root.catalogDragIndex

                    onFolderToggleRequested: function(path) { root.toggleFolder(path) }
                    onFolderMacrosRequested: function(path) { root.manageMacrosRequested(path) }
                    onFolderConnectionRequested: function(path) { root.folderConnectionRequested(path) }
                    onFolderAddSessionRequested: function(path) {
                        root.addSessionRequested(path)
                    }
                    onFolderAddRequested: function(path) { root.openFolderCreate(path) }
                    onFolderRenameRequested: function(path, name) {
                        root.openFolderRename(path, name)
                    }
                    onFolderRemoveRequested: function(path, name, totalCount) {
                        root.requestDeletion([], [path], false)
                    }
                    onSessionOpenRequested: function(sourceIndex) {
                        root.openSessionRequested(SessionStore.get(sourceIndex))
                    }
                    onSessionEditRequested: function(sourceIndex) {
                        root.editSessionRequested(sourceIndex)
                    }
                    onSessionDuplicateRequested: function(sourceIndex) {
                        const created = SessionStore.duplicateSession(sourceIndex)
                        root.showNotice(created >= 0 ? "Session duplicated"
                                                     : SessionStore.lastError)
                    }
                    onSessionRemoveRequested: function(sourceIndex, name) {
                        root.requestDeletion([SessionStore.get(sourceIndex).id], [], false)
                    }
                    onNodeMoveRequested: function(kind, path, sourceIndex,
                                                  destinationPath) {
                        root.moveCatalogNode(kind, path, sourceIndex,
                                             destinationPath)
                    }
                    onDragActivityChanged: function(active, kind, path,
                                                    sourceIndex, name) {
                        if (active) {
                            root.catalogDragActive = true
                            root.catalogDragKind = kind
                            root.catalogDragPath = path
                            root.catalogDragIndex = sourceIndex
                            root.catalogDragName = name
                        } else {
                            root.finishCatalogDrag()
                        }
                    }
                    onDragPositionChanged: function(viewportY) {
                        root.catalogDragViewportY = viewportY
                    }
                }
            }

            Column {
                visible: SessionStore.count === 0 && SessionStore.folderCount === 0
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 230)
                spacing: 9

                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: "No saved connections"
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                    font.weight: Font.DemiBold
                }
                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: "Create one, import OpenSSH aliases, or bring in an entire mRemoteNG tree."
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Add session"
                    iconText: "＋"
                    emphasized: true
                    compact: true
                    onClicked: root.addSessionRequested("")
                }
            }

            Column {
                visible: root.treeItems.length === 0
                         && (SessionStore.count > 0 || SessionStore.folderCount > 0)
                         && searchField.text.length > 0
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 230)
                spacing: 6

                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: "No matching connections"
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                    font.weight: Font.DemiBold
                }
                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: "Search checks folder paths, names, hosts, users, and tags."
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }

            // A single right-button layer makes the whole catalog useful as a
            // context surface, including the empty state and space below rows.
            // Row hits are forwarded to their own menus so folder-specific
            // actions retain the exact destination path.
            MouseArea {
                id: catalogAreaContextTarget
                objectName: "catalogAreaContextTarget"
                anchors.fill: parent
                z: 2
                acceptedButtons: Qt.RightButton

                onPressed: function(mouse) {
                    const contentPoint = sessionsList.contentItem.mapFromItem(
                                             catalogAreaContextTarget,
                                             mouse.x, mouse.y)
                    const row = sessionsList.itemAt(contentPoint.x,
                                                     contentPoint.y)
                    if (row) {
                        const rowPoint = row.mapFromItem(catalogAreaContextTarget,
                                                         mouse.x, mouse.y)
                        row.openContextMenuAt(rowPoint)
                        return
                    }
                    catalogAreaMenu.popup(catalogAreaContextTarget,
                                          mouse.x, mouse.y)
                }
            }

            AppMenu {
                id: catalogAreaMenu
                objectName: "catalogAreaContextMenu"

                AppMenuItem {
                    objectName: "catalogAreaNewSessionItem"
                    text: "New SSH session"
                    onTriggered: root.addSessionRequested("")
                }
                AppMenuItem {
                    objectName: "catalogAreaNewFolderItem"
                    text: "New folder"
                    onTriggered: root.openFolderCreate("")
                }
            }
        }

        Rectangle {
            id: sessionInspector
            objectName: "sessionInspector"
            Layout.fillWidth: true
            implicitHeight: 112
            radius: 3
            color: Theme.canvas
            border.color: Theme.borderSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 5
                anchors.bottomMargin: 5
                spacing: 0

                Repeater {
                    model: root.inspectorFields
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            textFormat: Text.PlainText
                            text: modelData.label
                            Layout.preferredWidth: 68
                            color: Theme.textFaint
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(9)
                            elide: Text.ElideRight
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: modelData.value
                            Layout.fillWidth: true
                            color: modelData.value.length > 0 ? Theme.text : Theme.textFaint
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(9)
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

    }

    Rectangle {
        visible: root.noticeText.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: 12
        height: Math.max(34, noticeLabel.implicitHeight + 16)
        z: 50
        radius: Theme.radiusSmall
        color: Theme.accentDim
        border.color: Theme.accent

        Text { textFormat: Text.PlainText;
            id: noticeLabel
            anchors.fill: parent
            anchors.margins: 8
            verticalAlignment: Text.AlignVCenter
            text: root.noticeText
            color: Theme.accentBright
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(9)
        }
    }

    Dialog {
        id: sessionsYamlDialog
        objectName: "sessionsYamlDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 490)
        modal: true
        title: "Session catalog · YAML"
        standardButtons: Dialog.Close
        background: Rectangle { color: Theme.surface; radius: Theme.radiusLarge; border.color: Theme.border }
        contentItem: YamlFileActions {
            objectName: "catalogYamlActions"
            storage: SessionStore; label: "Sessions"
        }
    }
    FileDialog {
        id: mRemoteImportDialog
        title: "Import mRemoteNG connections"
        fileMode: FileDialog.OpenFile
        nameFilters: ["mRemoteNG exports (*.xml *.confCons *.txt)", "All files (*)"]
        onAccepted: {
            const count = SessionStore.importMRemoteNg(selectedFile.toString())
            root.showNotice(SessionStore.lastError.length > 0
                            ? SessionStore.lastError
                            : SessionStore.lastImportSummary)
            if (count > 0)
                root.setExpandedFolders([])
        }
    }

    Dialog {
        id: folderDialog
        objectName: "folderEditorDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 390
        modal: true
        title: root.folderDialogMode === "create" ? "New folder" : "Rename folder"
        standardButtons: Dialog.Save | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: 8

            Text { textFormat: Text.PlainText;
                visible: root.folderDialogPath.length > 0
                Layout.fillWidth: true
                text: root.folderDialogMode === "create"
                      ? "Inside: " + root.folderDialogPath
                      : "Path: " + root.folderDialogPath
                color: Theme.textMuted
                elide: Text.ElideMiddle
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
            }
            TextField {
                id: folderNameField
                Layout.fillWidth: true
                placeholderText: "Folder name"
                selectByMouse: true
                onAccepted: folderDialog.accept()
            }
        }

        onAccepted: {
            const name = folderNameField.text.trim()
            if (name.length === 0) {
                root.showNotice("Folder name is required")
                return
            }
            if (root.folderDialogMode === "create") {
                const created = SessionStore.addFolder(root.folderDialogPath, name)
                root.showNotice(created ? "Folder created" : SessionStore.lastError)
                if (created) {
                    const parentPaths = AppSettings.expandedSessionFolders.slice()
                    if (root.folderDialogPath.length > 0
                            && parentPaths.indexOf(root.folderDialogPath) < 0)
                        parentPaths.push(root.folderDialogPath)
                    root.setExpandedFolders(parentPaths)
                }
            } else {
                const renamed = SessionStore.renameFolder(root.folderDialogPath, name)
                root.showNotice(renamed ? "Folder renamed" : SessionStore.lastError)
                if (renamed)
                    root.remapExpandedFolder(root.folderDialogPath, name)
            }
        }
    }

    Dialog {
        id: deleteEntriesDialog
        objectName: "deleteCatalogEntriesDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 32 : 440)
        contentHeight: deletionSummary.implicitHeight
        // Keep wrapping text independent of the Basic dialog's inferred height.
        implicitHeight: contentHeight + topPadding + bottomPadding + 48 + footer.implicitHeight + 2 * spacing
        modal: true
        title: root.pendingDeletion.all ? "Delete the entire saved catalog?" : "Delete selected entries?"
        closePolicy: Popup.CloseOnEscape
        onOpened: cancelDeletionButton.forceActiveFocus()
        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radius
            border.color: Theme.border
        }
        header: Text {
            text: deleteEntriesDialog.title
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(13)
            font.weight: Font.DemiBold
            padding: 16
        }

        contentItem: Text {
            id: deletionSummary
            objectName: "deleteCatalogSummary"
            width: deleteEntriesDialog.width - deleteEntriesDialog.leftPadding - deleteEntriesDialog.rightPadding
            textFormat: Text.PlainText
            text: (root.pendingDeletion.sessions || 0) + " saved sessions and "
                  + (root.pendingDeletion.folderCount || 0) + " folders will be deleted."
                  + "\n\nSelected folders include ALL descendants, even hidden or filtered-out sessions."
                  + "\n\nOpen terminals stay open. Remote files, SSH keys, routes, logs and macro groups are not deleted."
                  + "\n\nThis cannot be undone in the app; no automatic backup is created."
            color: Theme.text
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
        }
        footer: RowLayout {
            implicitHeight: 46
            spacing: 8
            Item { Layout.fillWidth: true; implicitHeight: 1 }
            AppButton {
                id: cancelDeletionButton
                objectName: "cancelCatalogDeletionButton"
                text: "Cancel"
                onClicked: deleteEntriesDialog.close()
            }
            AppButton {
                objectName: "confirmCatalogDeletionButton"
                text: "Delete permanently"
                destructive: true
                emphasized: true
                onClicked: root.confirmDeletion()
            }
        }
    }
}
