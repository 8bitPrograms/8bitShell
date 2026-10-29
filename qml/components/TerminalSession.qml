import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BitShell

Item {
    id: root

    property alias terminal: terminalControl
    property string launchMode: "local"
    property string command: ""
    property var localCommand: []
    property var localProfile: ({})
    property var profile: ({})
    property int zoomPercent: 100
    property bool searchVisible: false
    property bool tiled: false
    property bool performanceProbe: false
    property string paneTitle: ""
    property bool paneActive: false
    property bool showBroadcastChoice: false
    property bool broadcastIncluded: false
    signal activated()
    signal broadcastSelected(bool included)
    readonly property bool securityPromptVisible: pasteConfirm.visible || x11Confirm.visible || sshSuggestions.securityPromptVisible

    signal zoomStepRequested(int steps)

    function focusTerminal() {
        if (pasteConfirm.visible) pasteConfirm.standardButton(Dialog.Cancel).forceActiveFocus()
        else if (x11Confirm.visible) x11Confirm.standardButton(Dialog.Cancel).forceActiveFocus()
        else if (sshSuggestions.securityPromptVisible) sshSuggestions.focusPrompt()
        else terminalControl.forceActiveFocus()
    }

    function openSearch() {
        searchVisible = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function closeSearch() {
        searchVisible = false
        terminalControl.clearSearch()
        terminalControl.forceActiveFocus()
    }

    TerminalView {
        id: terminalControl
        anchors.fill: parent
        anchors.topMargin: root.tiled ? 29 : 0
        onActiveFocusChanged: if (activeFocus) root.activated()
        fontFamily: AppSettings.terminalFontFamily
        fontPixelSize: AppSettings.terminalFontSize * root.zoomPercent / 100
        textAntialiasing: AppSettings.terminalTextAntialiasing
        fontHinting: AppSettings.terminalFontHinting
        scrollbackLimit: AppSettings.scrollbackLines
        copyOnSelect: AppSettings.copyOnSelect
        keyBindings: AppSettings.keyBindings
        backspaceMode: root.profile.backspaceMode === "bs"
                       || root.profile.backspaceMode === "del"
                       ? root.profile.backspaceMode : AppSettings.backspaceMode
        colorScheme: Theme.activeTerminalTheme
        logEnabled: !root.performanceProbe && AppSettings.logByDefault
        performanceProbe: root.performanceProbe
        logRootDirectory: AppSettings.logDirectory
        logDirectoryTemplate: AppSettings.logDirectoryTemplate
        logFileNameTemplate: AppSettings.logFileNameTemplate
        logTimestamps: AppSettings.logTimestamps
        logAppend: AppSettings.logAppend
        logRotationMegabytes: AppSettings.logRotationMegabytes
        logRetainedFiles: AppSettings.logRetainedFiles
        syntaxHighlightEnabled: AppSettings.highlightByDefault
        highlightRules: HighlightRuleStore.rules

        onZoomRequested: function(steps) {
            root.zoomStepRequested(steps)
        }
        onBell: bellAnimation.restart()
        onContextMenuRequested: function(position) {
            terminalMenu.popup(terminalControl, position.x, position.y)
        }
        onPasteConfirmationRequested: function(preview, appendEnter) {
            pastePreview.text = preview
            pasteConfirm.executes = appendEnter
            pasteConfirm.open()
        }
        onX11ConfirmationRequested: function(target) { x11Confirm.host = target; x11Confirm.open() }
        onCursorEpochChanged: {
            cursorBlink.solid = true
            if (cursorBlink.running) cursorBlink.restart()
        }

        // Blink lives here so the terminal texture is not uploaded twice a second.
        Rectangle {
            id: cursorOverlay
            objectName: "terminalCursorOverlay"
            enabled: false
            z: 1
            visible: terminalControl.cursorOverlayVisible
            x: terminalControl.cursorX
            y: terminalControl.cursorY
            width: terminalControl.cursorWidth
            height: terminalControl.cursorHeight
            color: terminalControl.cursorColor
            antialiasing: false
            opacity: (!cursorBlink.running || cursorBlink.solid)
                     ? (terminalControl.activeFocus ? 145 / 255 : 65 / 255) : 0
        }
        Timer {
            id: cursorBlink
            interval: 530
            repeat: true
            running: terminalControl.cursorBlinkEnabled
                     && cursorOverlay.visible
                     && terminalControl.activeFocus
                     && terminalControl.visible
                     && terminalControl.Window.window !== null
                     && terminalControl.Window.active
                     && terminalControl.Window.visibility !== Window.Minimized
            property bool solid: true
            onRunningChanged: if (running) solid = true
            onTriggered: solid = !solid
        }

        Component.onCompleted: {
            if (root.launchMode === "profile")
                terminalControl.startSshProfile(root.profile)
            else if (root.launchMode === "quick")
                terminalControl.connectToTarget(root.command)
            else if (root.launchMode === "localProfile")
                terminalControl.startLocalProfile(root.localProfile)
            else if (root.launchMode === "command")
                terminalControl.startLocalCommand(root.localCommand)
            else
                terminalControl.startLocalShell()
        }
    }

    LocalSshSuggestions {
        id: sshSuggestions
        anchors.fill: parent
        z: 12
        terminal: terminalControl
        blocked: root.searchVisible || pasteConfirm.visible || x11Confirm.visible
    }

    Rectangle {
        visible: root.tiled; anchors.top: parent.top; width: parent.width; height: 29
        color: root.paneActive ? Theme.surfaceSelected : Theme.surface
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 5; spacing: 5
            AbstractButton {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                onClicked: root.activated()
                contentItem: Text { text: root.paneTitle; textFormat: Text.PlainText; color: root.paneActive ? Theme.accentBright : Theme.textMuted; font.pixelSize: Theme.uiFontSize(10); elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter }
            }
            LockIcon { visible: root.launchMode === "profile" || root.launchMode === "quick"; Layout.preferredWidth: 13; Layout.preferredHeight: 15 }
            CheckBox {
                objectName: "tileBroadcastCheck"; visible: root.showBroadcastChoice
                enabled: terminalControl.remoteSession && terminalControl.running
                text: "Target"; font.pixelSize: Theme.uiFontSize(10); checked: root.broadcastIncluded
                onToggled: root.broadcastSelected(checked)
                ToolTip.visible: hovered; ToolTip.text: "Include this SSH terminal. Confirm authentication is complete before selecting it."
            }
        }
    }
    Rectangle {
        visible: root.tiled; anchors.fill: parent; color: "transparent"
        border.color: root.paneActive ? (Theme.cyberpunkChrome ? Theme.secondaryAccent : Theme.accent) : Theme.borderSoft; border.width: 1
    }

    Dialog {
        id: x11Confirm
        objectName: "x11ConfirmationDialog"
        property string host: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 40)
        title: "Allow remote graphical applications?"
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        footer: AppDialogButtonBox { }
        onOpened: standardButton(Dialog.Cancel).forceActiveFocus()
        onAccepted: terminalControl.confirmX11(true)
        onRejected: terminalControl.confirmX11(false)
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.text
                text: "X11 forwarding gives graphical access to " + x11Confirm.host
                      + ". Only untrusted (-X), time-limited forwarding is allowed; it still depends on your local X server's security. Use only with hosts you trust. Cancel is the safe default."
            }
        }
    }

    Dialog {
        id: pasteConfirm
        objectName: "pasteConfirmationDialog"
        property bool executes: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(580, parent.width - 40)
        title: "Review terminal input"
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        footer: AppDialogButtonBox { }
        onOpened: standardButton(Dialog.Cancel).forceActiveFocus()
        onAccepted: terminalControl.confirmPaste(true)
        onRejected: terminalControl.confirmPaste(false)
        onClosed: { pastePreview.text = ""; terminalControl.confirmPaste(false) }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: pasteConfirm.executes
                      ? "This macro sends Enter and may execute a command. Check the complete command and destination before continuing."
                      : "Multiline or invisible characters can execute commands. Paste only if you trust and understand this content."
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: "Destination: " + terminalControl.activeTarget
                color: Theme.accent
                elide: Text.ElideRight
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                TextArea {
                    id: pastePreview
                    objectName: "pastePreview"
                    readOnly: true
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    font.family: Theme.monoFont
                    background: Rectangle { color: Theme.surfaceRaised }
                }
            }
        }
    }

    AppMenu {
        id: terminalMenu
        objectName: "terminalContextMenu"
        AppMenuItem { text: "Copy"; enabled: terminalControl.hasSelection; onTriggered: terminalControl.copySelection() }
        AppMenuItem { text: "Paste"; enabled: terminalControl.running; onTriggered: { terminalControl.pasteClipboard(); root.focusTerminal() } }
        AppMenuItem { text: "Paste selection"; enabled: terminalControl.running; onTriggered: terminalControl.pasteSelection() }
        AppMenuItem { text: "Select all"; onTriggered: terminalControl.selectAll() }
        AppMenuSeparator {}
        AppMenuItem { text: "Find in terminal"; onTriggered: root.openSearch() }
        AppMenuItem { text: "Zoom in"; onTriggered: root.zoomStepRequested(1) }
        AppMenuItem { text: "Zoom out"; onTriggered: root.zoomStepRequested(-1) }
        AppMenuSeparator {}
        AppMenuItem { text: "Clear scrollback"; onTriggered: terminalControl.clearScrollback() }
    }

    Rectangle {
        id: bellFlash
        anchors.fill: parent
        color: "transparent"
        border.width: 2
        border.color: Theme.amber
        opacity: 0
    }

    SequentialAnimation {
        id: bellAnimation
        NumberAnimation {
            target: bellFlash
            property: "opacity"
            to: 0.8
            duration: 55
        }
        NumberAnimation {
            target: bellFlash
            property: "opacity"
            to: 0
            duration: 180
        }
    }

    Rectangle {
        id: searchBar
        objectName: "terminalFindBar"
        visible: root.searchVisible
        anchors.top: terminalControl.top
        anchors.right: parent.right
        anchors.topMargin: 10
        anchors.rightMargin: 12
        width: Math.min(390, Math.max(0, parent.width - 24))
        height: 38
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.color: searchField.activeFocus ? Theme.accent : Theme.border
        z: 20

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 5
            spacing: 5

            SearchIcon {
                color: Theme.accent
            }
            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: "Find in terminal"
                color: Theme.text
                placeholderTextColor: Theme.textFaint
                selectionColor: Theme.accentDim
                selectedTextColor: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(11)
                background: null
                onTextChanged: terminalControl.searchText = text
                onAccepted: terminalControl.findNext()
            }
            Text { textFormat: Text.PlainText;
                text: terminalControl.searchMatchCount === 0
                      ? "0 / 0"
                      : (terminalControl.currentSearchMatch + 1)
                        + " / " + terminalControl.searchMatchCount
                color: Theme.textMuted
                font.family: Theme.monoFont
                font.pixelSize: Theme.uiFontSize(9)
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignHCenter
            }
            AppButton {
                text: ""
                iconText: "↑"
                compact: true
                implicitWidth: 27
                onClicked: terminalControl.findPrevious()
            }
            AppButton {
                text: ""
                iconText: "↓"
                compact: true
                implicitWidth: 27
                onClicked: terminalControl.findNext()
            }
            AppButton {
                text: ""
                iconText: "×"
                compact: true
                implicitWidth: 27
                onClicked: root.closeSearch()
            }
        }
    }

    Rectangle {
        id: restartOverlay
        objectName: "terminalRestartOverlay"
        anchors.centerIn: terminalControl
        visible: !terminalControl.running
        width: Math.min(350, Math.max(0, parent.width - 24))
        height: Math.min(130, Math.max(54, terminalControl.height - 16))
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
        z: 10

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: restartOverlay.height < 110 ? 8 : 18
            spacing: 10

            Text { textFormat: Text.PlainText;
                visible: restartOverlay.height >= 100
                Layout.fillWidth: true
                Layout.maximumHeight: 52
                text: terminalControl.statusText
                color: Theme.textMuted
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(11)
            }
            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Restart session"
                iconText: "↻"
                emphasized: true
                onClicked: terminalControl.restartSession()
            }
        }
    }

    Shortcut {
        enabled: root.visible && root.searchVisible && !root.securityPromptVisible
        sequence: "Escape"
        onActivated: {
            if (root.searchVisible)
                root.closeSearch()
        }
    }
}
