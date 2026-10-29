pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import BitShell

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1510
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    title: "8bitShell"
    color: Theme.canvas
    // The toolbar is the title bar. System buttons live inside it.
    flags: Qt.Window | Qt.FramelessWindowHint

    property int inspectorMode: AppSettings.fileBrowserVisible ? 1 : 0 // 0 hidden, 1 files
    property string startupOverlay: ""
    property string startupFilter: ""
    property var startupCommand: []
    property string startupDirectory: ""
    property var updater: null
    Binding { target: Theme; property: "bonusThemesUnlocked"; value: AppSettings.bonusThemesUnlocked }
    property int startupTabCount: 1
    property bool performanceProbe: false
    function performanceSessionDiagnostics() { return terminalPane.performanceSessionDiagnostics() }
    readonly property int performanceTabCount: terminalPane.tabCount
    readonly property int performanceRunningTabs: {
        let count = 0
        for (const row of terminalPane.broadcastRows) if (row.running) ++count
        return count
    }
    property real navigationWidth: sidebarContainer.visible ? sidebarContainer.width : 224

    // Keep optional editors out of the cold-start object tree. Load synchronously
    // on the first request, then retain their state for subsequent opens.
    function ensureOverlay(name) {
        const overlays = {
            settingsDrawer: settingsDrawer,
            sessionEditorDialog: sessionEditor,
            macroManager: macroManager,
            routeManager: routeManager,
            folderConnectionDialog: folderConnection,
            sshKeyManager: sshKeyManager,
            aboutDialog: aboutDialog,
            commandPalette: commandPaletteLoader
        }
        const loader = overlays[name]
        if (!loader) return null
        loader.active = true
        return loader.item
    }

    property var paletteReturnFocus: null
    readonly property bool commandPaletteBlocked: terminalPane.securityPromptActive
        || (sessionEditor.item !== null && sessionEditor.item.visible)
        || (macroManager.item !== null && macroManager.item.visible)
        || (routeManager.item !== null && routeManager.item.visible)
        || (folderConnection.item !== null && folderConnection.item.visible)
        || (sshKeyManager.item !== null && sshKeyManager.item.visible)
        || (aboutDialog.item !== null && aboutDialog.item.visible)

    function openCommandPalette(initialQuery) {
        if (commandPaletteBlocked) return
        const palette = ensureOverlay("commandPalette")
        if (palette.visible) { palette.close(); return }
        paletteReturnFocus = root.activeFocusItem
        if (settingsDrawer.item && settingsDrawer.item.visible) settingsDrawer.item.close()
        topBar.dismissSearch()
        palette.showPalette(initialQuery || "")
        refreshCommandPalette()
    }
    function restorePaletteFocus() {
        if (commandPaletteBlocked) return
        if (paletteReturnFocus && paletteReturnFocus.visible && paletteReturnFocus.enabled)
            paletteReturnFocus.forceActiveFocus()
        else if (terminalPane.activeTerminal) terminalPane.activeTerminal.forceActiveFocus()
        paletteReturnFocus = null
    }
    function refreshCommandPalette() {
        const palette = commandPaletteLoader.item
        if (!palette || !palette.visible) return
        palette.results = paletteActions.search(palette.query, terminalPane.collectPaletteRows())
    }
    function executePaletteAction(id, query) {
        if (commandPaletteBlocked) return
        // IDs survive reordered tabs and catalog reloads. Revalidate at execution,
        // after the palette has closed and before opening any other modal.
        const action = paletteActions.search(query, terminalPane.collectPaletteRows()).find(row => row.id === id)
        if (!action || !action.enabled) { restorePaletteFocus(); return }
        if (action.type === "query") {
            commandPaletteLoader.item.showPalette(action.target)
            refreshCommandPalette()
            return
        }
        if (action.type === "tab") { terminalPane.activatePaletteTab(action.target); return }
        if (action.type === "saved") {
            const profile = SessionStore.getById(action.target)
            if (profile.host) terminalPane.openProfile(profile)
            return
        }
        if (action.type === "connect") { terminalPane.connectTo(action.target); return }
        if (action.type === "routeConnect") {
            const parsed = SessionStore.parseSshTarget(query)
            if (parsed.host) terminalPane.openProfile({host: parsed.host, port: parsed.port || 22,
                connection: {mode: "route", route: action.target}})
            return
        }
        if (action.type === "save") {
            ensureOverlay("sessionEditorDialog").openCreateTarget(action.target ? SessionStore.parseSshTarget(action.target) : ({}))
            return
        }
        if (action.type === "local") { terminalPane.newLocalProfile(action.target); return }
        if (action.type === "settings") { ensureOverlay("settingsDrawer").openSection(action.target); return }
        if (action.type === "routes") { ensureOverlay("routeManager").openManager(); return }
        if (action.type === "keys") { ensureOverlay("sshKeyManager").openManager(); return }
        if (action.type === "uiTheme") {
            if (AppSettings.darkAppearance) AppSettings.uiThemeId = action.target
            else AppSettings.uiLightThemeId = action.target
        } else if (action.type === "terminalTheme") {
            if (AppSettings.darkAppearance) AppSettings.terminalThemeId = action.target
            else AppSettings.terminalLightThemeId = action.target
        } else switch (action.id) {
            case "appearance:toggle": AppSettings.toggleAppearance(); break
            case "sidebar:toggle": AppSettings.sidebarVisible = !AppSettings.sidebarVisible; break
            case "files:toggle": root.inspectorMode = root.inspectorMode === 1 ? 0 : 1; break
            case "macros:toggle": AppSettings.macroBarVisible = !AppSettings.macroBarVisible; break
            case "separate:toggle": AppSettings.separateTerminalTheme = !AppSettings.separateTerminalTheme; break
            case "cyberark:toggle": PersonalProfile.cyberArkEnabled = !PersonalProfile.cyberArkEnabled; break
            case "logging:toggle": AppSettings.logByDefault = !AppSettings.logByDefault; break
            case "highlight:toggle": AppSettings.highlightByDefault = !AppSettings.highlightByDefault; break
        }
        restorePaletteFocus()
    }

    onInspectorModeChanged: {
        if (inspectorMode === 0 || inspectorMode === 1)
            AppSettings.fileBrowserVisible = inspectorMode === 1
    }

    Binding {
        target: Theme
        property: "uiThemeId"
        value: AppSettings.effectiveUiThemeId
    }
    Binding {
        target: Theme
        property: "terminalThemeId"
        value: AppSettings.separateTerminalTheme
               ? AppSettings.effectiveTerminalThemeId : Theme.activeUiTheme.id
    }

    onStartupFilterChanged: Qt.callLater(function() {
        sidebar.filterText = startupFilter
    })

    onStartupOverlayChanged: Qt.callLater(function() {
        if (startupOverlay === "session")
            ensureOverlay("sessionEditorDialog").openCreate()
        else if (startupOverlay === "settings")
            ensureOverlay("settingsDrawer").openSection("Profile")
        else if (startupOverlay === "keys")
            ensureOverlay("sshKeyManager").openManager()
        else if (startupOverlay === "about")
            ensureOverlay("aboutDialog").openAbout()
        else if (startupOverlay === "rule")
            ensureOverlay("settingsDrawer").openRules(true)
        else if (startupOverlay === "catalog-menu")
            sidebar.triggerCatalogMenuButton()
        else if (startupOverlay === "folder-menu")
            sidebar.openFirstTreeMenu("folder")
        else if (startupOverlay === "session-menu")
            sidebar.openFirstTreeMenu("session")
    })

    palette: Theme.controlPalette

    Item {
        parent: Overlay.overlay
        anchors.fill: parent
        z: -1
        enabled: root.visibility !== Window.Maximized

        function beginResize(edge) {
            if (root.visibility !== Window.Maximized)
                root.startSystemResize(edge)
        }

        MouseArea {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 5
            cursorShape: Qt.SizeVerCursor
            onPressed: parent.beginResize(Qt.TopEdge)
        }
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 5
            cursorShape: Qt.SizeVerCursor
            onPressed: parent.beginResize(Qt.BottomEdge)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            cursorShape: Qt.SizeHorCursor
            onPressed: parent.beginResize(Qt.LeftEdge)
        }
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            cursorShape: Qt.SizeHorCursor
            onPressed: parent.beginResize(Qt.RightEdge)
        }
        MouseArea {
            width: 10
            height: 10
            anchors.left: parent.left
            anchors.top: parent.top
            cursorShape: Qt.SizeFDiagCursor
            onPressed: parent.beginResize(Qt.LeftEdge | Qt.TopEdge)
        }
        MouseArea {
            width: 10
            height: 10
            anchors.right: parent.right
            anchors.top: parent.top
            cursorShape: Qt.SizeBDiagCursor
            onPressed: parent.beginResize(Qt.RightEdge | Qt.TopEdge)
        }
        MouseArea {
            width: 10
            height: 10
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeBDiagCursor
            onPressed: parent.beginResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        MouseArea {
            width: 10
            height: 10
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            onPressed: parent.beginResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }

    header: TopBar {
        id: topBar
        navigationWidth: root.navigationWidth
        filesVisible: root.inspectorMode === 1
        splitCount: terminalPane.splitCount
        onCommandPaletteRequested: root.openCommandPalette("")
        onToggleFiles: root.inspectorMode = root.inspectorMode === 1 ? 0 : 1
        onSplitCountRequested: function(count) { terminalPane.setSplitCount(count) }
        onSettingsRequested: function(section) { root.ensureOverlay("settingsDrawer").openSection(section) }
        onConnectRequested: function(target) {
            if (target.trim().length === 0)
                return
            const savedIndex = SessionStore.find(target)
            if (savedIndex >= 0)
                terminalPane.openProfile(SessionStore.get(savedIndex))
            else
                terminalPane.connectTo(target)
        }
        onSavedSessionRequested: function(id) {
            const profile = SessionStore.getById(id)
            if (profile.host) terminalPane.openProfile(profile)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.canvas

        SplitView {
            anchors.fill: parent
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 3
                color: SplitHandle.hovered || SplitHandle.pressed
                       ? Theme.accent : Theme.borderSoft
            }

            UpwardPanel {
                id: sidebarContainer
                objectName: "connectionsPanel"
                expanded: AppSettings.sidebarVisible
                SplitView.minimumWidth: 220
                SplitView.preferredWidth: 272
                SplitView.maximumWidth: 470

                Sidebar {
                    id: sidebar
                    anchors.fill: parent
                    onManageMacrosRequested: function(path) { root.ensureOverlay("macroManager").openForFolder(path) }
                    onOpenSessionRequested: function(profile) {
                        terminalPane.openProfile(profile)
                    }
                    onAddSessionRequested: function(folder) {
                        root.ensureOverlay("sessionEditorDialog").openCreate(folder)
                    }
                    onEditSessionRequested: function(index) {
                        root.ensureOverlay("sessionEditorDialog").openEdit(index)
                    }
                    onSettingsRequested: root.ensureOverlay("settingsDrawer").open()
                    onProfileRequested: root.ensureOverlay("settingsDrawer").openSection("Profile")
                    onRoutesRequested: root.ensureOverlay("routeManager").openManager()
                    onFolderConnectionRequested: function(path) { root.ensureOverlay("folderConnectionDialog").openForFolder(path) }
                }
            }

            SplitView {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 540
                orientation: Qt.Horizontal

                handle: Rectangle {
                    implicitWidth: 3
                    color: SplitHandle.hovered || SplitHandle.pressed
                           ? Theme.accent : Theme.borderSoft
                }

                TerminalPane {
                    id: terminalPane
                    commandPaletteOpen: commandPaletteLoader.item !== null && commandPaletteLoader.item.visible
                    onManageLocalProfilesRequested: root.ensureOverlay("settingsDrawer").openSection("Local profiles")
                    startupCommand: root.startupCommand
                    startupDirectory: root.startupDirectory
                    startupTabCount: root.startupTabCount
                    performanceProbe: root.performanceProbe
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 470
                    onManageMacrosRequested: function(path) { root.ensureOverlay("macroManager").openForFolder(path) }
                    onTabClosed: function(tabId) {
                        const panel = filePanel.item as SftpPanel
                        if (panel) panel.forgetSession(tabId)
                    }
                }

                UpwardPanel {
                    id: filesContainer
                    objectName: "filesPanel"
                    expanded: root.inspectorMode === 1
                    SplitView.preferredWidth: 365
                    SplitView.minimumWidth: 280
                    SplitView.maximumWidth: 640

                    Loader {
                        id: filePanel
                        objectName: "filePanelLoader"
                        anchors.fill: parent
                        property bool initialized: false
                        active: root.inspectorMode === 1 || initialized
                        onLoaded: initialized = true
                        sourceComponent: SftpPanel {
                            profile: terminalPane.activeProfile
                            sessionKey: terminalPane.activeTabId
                        }
                    }
                }

            }
        }
    }

    Loader {
        id: macroManager
        active: false
        sourceComponent: MacroManager {}
    }

    Connections {
        target: SessionStore
        function onFolderMoved(oldPath, newPath) { MacroStore.remapFolder(oldPath, newPath) }
        function onFolderRemoved(path) { MacroStore.removeFolder(path) }
        function onFoldersRemoved(paths) { MacroStore.removeFolders(paths) }
    }

    Loader {
        id: sessionEditor
        active: false
        sourceComponent: SessionEditorDialog {
            onKeysRequested: root.ensureOverlay("sshKeyManager").openManager()
        }
    }

    Loader {
        id: settingsDrawer
        active: false
        sourceComponent: SettingsDrawer {
            updater: root.updater
            parent: Overlay.overlay
            onRoutesRequested: root.ensureOverlay("routeManager").openManager()
            onKeysRequested: root.ensureOverlay("sshKeyManager").openManager()
            onAboutRequested: root.ensureOverlay("aboutDialog").openAbout()
        }
    }
    Loader { id: routeManager; active: false; sourceComponent: RouteManager {} }
    Loader { id: folderConnection; active: false; sourceComponent: FolderConnectionDialog {} }
    Loader { id: sshKeyManager; active: false; sourceComponent: SshKeyManager {} }
    Loader { id: aboutDialog; active: false; sourceComponent: AboutDialog {} }

    CommandPaletteActions { id: paletteActions }
    Loader {
        id: commandPaletteLoader
        objectName: "commandPaletteLoader"
        active: false
        sourceComponent: CommandPalette {
            shortcutText: AppSettings.keyBindings.commandPalette || ""
            onSearchChanged: root.refreshCommandPalette()
            onChosen: function(id) { root.executePaletteAction(id, query) }
            onClosed: if (!activationPending && !visible) root.restorePaletteFocus()
        }
    }
    Timer { id: paletteRefresh; interval: 30; onTriggered: root.refreshCommandPalette() }
    function schedulePaletteRefresh() { if (commandPaletteLoader.item && commandPaletteLoader.item.visible) paletteRefresh.restart() }
    Connections { target: terminalPane; function onPaletteRevisionChanged() { root.schedulePaletteRefresh() } }
    Connections { target: SessionStore; function onRevisionChanged() { root.schedulePaletteRefresh() } }
    Connections { target: LocalProfileStore; function onChanged() { root.schedulePaletteRefresh() } }
    Connections { target: PersonalProfile; function onChanged() { root.schedulePaletteRefresh() } }
    Connections {
        target: AppSettings
        function onInputSettingsChanged() { root.schedulePaletteRefresh() }
        function onAppearanceChanged() { root.schedulePaletteRefresh() }
        function onBonusThemesChanged() { root.schedulePaletteRefresh() }
        function onLogByDefaultChanged() { root.schedulePaletteRefresh() }
        function onHighlightByDefaultChanged() { root.schedulePaletteRefresh() }
    }
    Shortcut {
        sequence: AppSettings.keyBindings.commandPalette || ""
        enabled: !root.commandPaletteBlocked && sequence.length > 0
        onActivated: root.openCommandPalette("")
    }

    Shortcut {
        sequence: AppSettings.keyBindings.quickConnect || ""
        enabled: !root.commandPaletteBlocked && (!commandPaletteLoader.item || !commandPaletteLoader.item.visible)
        onActivated: topBar.focusQuickConnect()
    }
}
