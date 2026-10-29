import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root
    objectName: "sessionEditorDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 620, 620)
    height: Math.min(parent ? parent.height - 48 : 720, 720)
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    property int editIndex: -1
    property string formError: ""
    property string selectedIdentityKey: ""
    property bool externalIdentityOpen: false
    property string selectedAuthenticationMode: "auto"
    property string editSessionId: ""
    property bool hasStoredPassword: false
    property bool removeStoredPassword: false
    readonly property var passwordInfo: {
        const revision = SessionStore.revision
        return SessionStore.passwordStatus({id: editSessionId, host: hostField.text.trim(),
            user: userField.text.trim(), port: Number(portField.text), group: groupField.text.trim(),
            authenticationMode: "password", proxyJump: proxyJumpField.text.trim(),
            connection: connectionChoice.toSelection()})
    }
    readonly property bool canRememberPassword: Boolean(passwordInfo.eligible && passwordInfo.available)
    onCanRememberPasswordChanged: if (!canRememberPassword) rememberPasswordCheck.checked = false
    readonly property var identityOptions: {
        const rows = [
            {text: "Use application default", value: ""},
            {text: "OpenSSH defaults / SSH agent", value: "none"}
        ]
        for (let index = 0; index < SshKeyStore.keys.length; ++index) {
            const entry = SshKeyStore.keys[index]
            rows.push({text: entry.name + " · " + entry.type + " · " + entry.fingerprint, value: entry.id})
        }
        if (selectedIdentityKey && selectedIdentityKey !== "none"
                && !SshKeyStore.get(selectedIdentityKey).id)
            rows.push({text: "Missing managed key · connection will be blocked", value: selectedIdentityKey})
        return rows
    }
    readonly property var backspaceOptions: [
        { text: "Use terminal default", value: "default" },
        { text: "DEL · ^? · 0x7F", value: "del" },
        { text: "BS · ^H · 0x08", value: "bs" }
    ]

    function backspaceIndex(value) {
        for (let index = 0; index < backspaceOptions.length; ++index) {
            if (backspaceOptions[index].value === value)
                return index
        }
        return 0
    }

    function identityIndex(value) {
        for (let index = 0; index < identityOptions.length; ++index) {
            if (identityOptions[index].value === value) return index
        }
        return 0
    }

    signal sessionSaved(int index)
    signal keysRequested()

    function openCreate(folder) {
        editIndex = -1
        editSessionId = ""
        selectedAuthenticationMode = "auto"
        passwordField.text = ""
        hasStoredPassword = false
        removeStoredPassword = false
        rememberPasswordCheck.checked = false
        nameField.text = ""
        groupField.text = folder || ""
        hostField.text = ""
        userField.text = ""
        portField.text = "22"
        selectedIdentityKey = ""
        identityField.text = ""
        externalIdentityOpen = false
        proxyJumpField.text = ""
        tagsField.text = ""
        x11Check.checked = false
        agentCheck.checked = false
        compressionCheck.checked = false
        backspaceCombo.currentIndex = 0
        connectionChoice.load({mode: "inherit"})
        formError = ""
        open()
        nameField.forceActiveFocus()
    }

    function openEdit(index) {
        const profile = SessionStore.get(index)
        if (!profile || !profile.host)
            return
        editIndex = index
        editSessionId = profile.id || ""
        selectedAuthenticationMode = profile.authenticationMode || "auto"
        passwordField.text = ""
        removeStoredPassword = false
        nameField.text = profile.name || ""
        groupField.text = profile.group || ""
        hostField.text = profile.host || ""
        userField.text = profile.user || ""
        portField.text = String(profile.port || 22)
        selectedIdentityKey = profile.identityKey || ""
        identityField.text = profile.identityFile || ""
        externalIdentityOpen = identityField.text.length > 0
        proxyJumpField.text = profile.proxyJump || ""
        tagsField.text = profile.tags ? profile.tags.join(", ") : ""
        x11Check.checked = Boolean(profile.forwardX11)
        agentCheck.checked = Boolean(profile.agentForwarding)
        compressionCheck.checked = Boolean(profile.compression)
        backspaceCombo.currentIndex = backspaceIndex(profile.backspaceMode || "default")
        connectionChoice.load(profile.connection || {mode: "direct"})
        hasStoredPassword = SessionStore.passwordStatus(profile).stored
        rememberPasswordCheck.checked = hasStoredPassword
        formError = ""
        open()
        nameField.forceActiveFocus()
    }

    function openCreateTarget(profile) {
        openCreate("")
        if (!profile || !profile.host) return
        nameField.text = profile.host
        hostField.text = profile.host
        userField.text = profile.user || ""
        portField.text = String(profile.port || 22)
        identityField.text = profile.identityFile || ""
        externalIdentityOpen = identityField.text.length > 0
        proxyJumpField.text = profile.proxyJump || ""
        x11Check.checked = Boolean(profile.forwardX11)
        compressionCheck.checked = Boolean(profile.compression)
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function saveSession() {
        const host = hostField.text.trim()
        if (host.length === 0) {
            formError = "Host is required."
            hostField.forceActiveFocus()
            return
        }
        const port = Number(portField.text)
        if (!Number.isInteger(port) || port < 1 || port > 65535) {
            formError = "Port must be between 1 and 65535."
            portField.forceActiveFocus()
            return
        }

        const tagValues = tagsField.text.split(",")
                              .map(function(value) { return value.trim() })
                              .filter(function(value) { return value.length > 0 })
        const values = {
            id: root.editSessionId,
            name: nameField.text.trim().length > 0 ? nameField.text.trim() : host,
            group: groupField.text.trim(),
            host: host,
            user: userField.text.trim(),
            port: port,
            identityKey: root.selectedIdentityKey,
            identityFile: root.selectedIdentityKey.length > 0 ? "" : identityField.text.trim(),
            proxyJump: proxyJumpField.text.trim(),
            tags: tagValues,
            forwardX11: x11Check.checked,
            agentForwarding: agentCheck.checked,
            compression: compressionCheck.checked
            , authenticationMode: root.selectedAuthenticationMode
            , backspaceMode: backspaceCombo.currentValue || "default"
            , connection: connectionChoice.toSelection()
        }

        let passwordAction = root.removeStoredPassword || root.selectedAuthenticationMode !== "password" ? "remove" : "keep"
        if (root.selectedAuthenticationMode === "password" && rememberPasswordCheck.checked
                && root.canRememberPassword && passwordField.text.length > 0) passwordAction = "replace"
        const index = SessionStore.saveSessionWithPassword(editIndex, JSON.parse(JSON.stringify(values)),
            passwordAction === "replace" ? passwordField.text : "", passwordAction)
        if (index < 0) {
            formError = SessionStore.lastError.length > 0
                        ? SessionStore.lastError : "Could not save the session."
            return
        }
        editIndex = index
        editSessionId = SessionStore.get(index).id
        passwordField.text = ""
        if (SessionStore.lastError.length > 0) {
            formError = SessionStore.lastError
            return
        }
        close()
        sessionSaved(index)
    }

    onClosed: {
        passwordField.text = ""
        rememberPasswordCheck.checked = false
        hasStoredPassword = false
        removeStoredPassword = false
    }

    Overlay.modal: Rectangle { color: "#a0080b10" }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 57
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.color: Theme.borderSoft

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12

            Column {
                Layout.fillWidth: true
                spacing: 1
                Text { textFormat: Text.PlainText;
                    text: root.editIndex < 0 ? "New SSH session" : "Edit SSH session"
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(14)
                    font.weight: Font.DemiBold
                }
                Text { textFormat: Text.PlainText;
                    text: "Save connection details and optional device-protected passwords"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
            AppButton {
                text: ""
                iconText: "×"
                compact: true
                implicitWidth: 30
                onClicked: root.close()
            }
        }
    }

    contentItem: ScrollView {
        id: formScroll
        clip: true
        leftPadding: 20
        rightPadding: 20
        topPadding: 16
        bottomPadding: 16
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: formScroll.availableWidth
            spacing: 9

            ConnectionChoice { id: connectionChoice; objectName: "sessionConnectionChoice"; Layout.fillWidth: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "DISPLAY NAME"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    AppTextField {
                        id: nameField
                        objectName: "sessionNameField"
                        Layout.fillWidth: true
                        placeholderText: "Production database"
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 190
                    Layout.maximumWidth: 190
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "FOLDER PATH"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    AppTextField {
                        id: groupField
                        Layout.fillWidth: true
                        placeholderText: "Production / Oracle / DC1"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderSoft
            }

            Text { textFormat: Text.PlainText;
                text: "CONNECTION"
                color: Theme.accent
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
                font.bold: true
                font.letterSpacing: 0.8
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "HOST"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    AppTextField {
                        id: hostField
                        objectName: "sessionHostField"
                        Layout.fillWidth: true
                        placeholderText: "server.example.com"
                        font.family: Theme.monoFont
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 92
                    Layout.maximumWidth: 92
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "PORT"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    AppTextField {
                        id: portField
                        Layout.fillWidth: true
                        text: "22"
                        horizontalAlignment: Text.AlignHCenter
                        validator: IntValidator { bottom: 1; top: 65535 }
                        font.family: Theme.monoFont
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "DIRECT SSH USERNAME"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                AppTextField {
                    id: userField
                    objectName: "sessionUserField"
                    Layout.fillWidth: true
                    enabled: connectionChoice.mode !== "route"
                    placeholderText: PersonalProfile.cyberArkEnabled
                                     ? "Optional for direct SSH; CyberArk uses Employee ID"
                                     : "Optional username for direct SSH"
                    font.family: Theme.monoFont
                }
            }

            Text { textFormat: Text.PlainText;
                text: "AUTHENTICATION & ROUTING"
                color: Theme.accent
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
                font.bold: true
                font.letterSpacing: 0.8
                Layout.topMargin: 4
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: "AUTHENTICATION"; textFormat: Text.PlainText
                    color: Theme.textMuted; font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9); font.bold: true
                }
                AppComboBox {
                    id: authenticationCombo
                    objectName: "sessionAuthenticationCombo"
                    Layout.fillWidth: true
                    model: [{text: "Automatic / SSH key", value: "auto"}, {text: "Password", value: "password"}]
                    textRole: "text"; valueRole: "value"
                    currentIndex: root.selectedAuthenticationMode === "password" ? 1 : 0
                    onActivated: {
                        root.selectedAuthenticationMode = currentValue
                        if (currentValue !== "password") passwordField.text = ""
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.selectedAuthenticationMode === "password"
                spacing: 6
                CheckBox {
                    id: rememberPasswordCheck
                    objectName: "sessionRememberPasswordCheck"
                    text: "Remember password on this device"
                    enabled: root.canRememberPassword
                    onToggled: if (!checked) {
                        passwordField.text = ""
                        root.removeStoredPassword = root.hasStoredPassword
                    }
                }
                AppTextField {
                    id: passwordField
                    objectName: "sessionPasswordField"
                    Layout.fillWidth: true
                    enabled: rememberPasswordCheck.checked && root.canRememberPassword
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    maximumLength: 4096
                    placeholderText: root.passwordInfo.stored && !root.removeStoredPassword
                                     ? "Saved on this device — leave blank to keep"
                                     : "Password to remember"
                    Accessible.name: "SSH password to save on this device"
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "sessionPasswordStatus"
                        Layout.fillWidth: true
                        text: root.removeStoredPassword ? "The saved password will be removed when you save."
                            : root.passwordInfo.reason || (root.passwordInfo.stored
                              ? "Saved password · " + root.passwordInfo.backend
                              : "Without a saved password, SSH asks when connecting.")
                        textFormat: Text.PlainText; wrapMode: Text.Wrap
                        color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(9)
                    }
                    AppButton {
                        objectName: "sessionForgetPasswordButton"
                        text: "Forget password"
                        compact: true
                        visible: root.hasStoredPassword && !root.removeStoredPassword
                        onClicked: {
                            root.removeStoredPassword = true
                            passwordField.text = ""
                            rememberPasswordCheck.checked = false
                            rememberPasswordCheck.forceActiveFocus()
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "Passwords stay on this device and are excluded from YAML and exports. Password mode skips SSH keys."
                    textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: Theme.textFaint; font.pixelSize: Theme.uiFontSize(8)
                }
            }

            ColumnLayout {
                objectName: "sessionKeyControls"
                visible: root.selectedAuthenticationMode === "auto"
                Layout.fillWidth: true
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Text { textFormat: Text.PlainText;
                        Layout.fillWidth: true
                        text: "SSH KEY"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    AppButton {
                        objectName: "manageSessionKeysButton"
                        compact: true
                        text: "Manage keys…"
                        onClicked: root.keysRequested()
                    }
                }
                AppComboBox {
                    id: identityCombo
                    objectName: "sessionIdentityCombo"
                    Layout.fillWidth: true
                    model: root.identityOptions
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: root.identityIndex(root.selectedIdentityKey)
                    onActivated: {
                        root.selectedIdentityKey = currentValue
                        // Choosing from the managed/default list is explicit;
                        // do not leave a hidden legacy external path in effect.
                        identityField.text = ""
                        root.externalIdentityOpen = false
                    }
                }
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: connectionChoice.mode === "route"
                          ? "The dedicated CyberArk key from Settings overrides this choice when configured."
                          : "The application default applies unless this session selects a managed key or OpenSSH defaults."
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                }
                AppButton {
                    compact: true
                    text: root.externalIdentityOpen ? "Hide external key path" : "Use external key path…"
                    iconText: root.externalIdentityOpen ? "⌃" : "⌄"
                    enabled: root.selectedIdentityKey.length === 0
                    onClicked: root.externalIdentityOpen = !root.externalIdentityOpen
                }
                AppTextField {
                    id: identityField
                    objectName: "sessionExternalIdentityField"
                    visible: root.externalIdentityOpen && root.selectedIdentityKey.length === 0
                    Layout.fillWidth: true
                    placeholderText: "~/.ssh/id_ed25519 (advanced, not copied)"
                    font.family: Theme.monoFont
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "PROXY JUMP"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                AppTextField {
                    id: proxyJumpField
                    Layout.fillWidth: true
                    placeholderText: "bastion.example.com (optional)"
                    font.family: Theme.monoFont
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                CheckBox {
                    id: x11Check
                    text: "X11 forwarding"
                }
                CheckBox {
                    id: agentCheck
                    text: "Agent forwarding (blocked; uncheck to connect)"
                    enabled: checked
                }
                CheckBox {
                    id: compressionCheck
                    text: "Compression"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "BACKSPACE SENDS"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                AppComboBox {
                    id: backspaceCombo
                    objectName: "sessionBackspaceCombo"
                    Layout.fillWidth: true
                    model: root.backspaceOptions
                    textRole: "text"
                    valueRole: "value"
                }
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: "Choose BS (^H) for Oracle or legacy tools only when DEL (^?) does not erase correctly."
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "TAGS"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                AppTextField {
                    id: tagsField
                    Layout.fillWidth: true
                    placeholderText: "database, production, oracle"
                }
            }

            Text { textFormat: Text.PlainText;
                visible: root.formError.length > 0
                Layout.fillWidth: true
                text: root.formError
                color: Theme.red
                wrapMode: Text.Wrap
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(10)
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 58
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.color: Theme.borderSoft

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.radius
            color: parent.color
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 9

            Text { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: root.editIndex < 0 ? "Details in YAML · passwords stay on this device" : "Changes apply next time you connect"
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
            }
            AppButton {
                objectName: "sessionCancelButton"
                text: "Cancel"
                onClicked: root.close()
            }
            AppButton {
                objectName: "sessionSaveButton"
                text: root.editIndex < 0 ? "Save session" : "Save changes"
                iconText: "✓"
                emphasized: true
                onClicked: root.saveSession()
            }
        }
    }
}
