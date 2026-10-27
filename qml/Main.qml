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
            aboutDialog: aboutDialog
        }
        const loader = overlays[name]
        if (!loader) return null
        loader.active = true
        return loader.item
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
        value: AppSettings.effectiveTerminalThemeId
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
                    onManageLocalProfilesRequested: root.ensureOverlay("settingsDrawer").openSection("Local profiles")
                    startupCommand: root.startupCommand
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

    Shortcut {
        sequence: AppSettings.keyBindings.quickConnect || ""
        enabled: !terminalPane.securityPromptActive
        onActivated: topBar.focusQuickConnect()
    }
}
