import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

// Create, edit and delete saved credentials. Secrets are typed into password
// fields and go straight to the keychain; nothing here ever shows one.
Dialog {
    id: root
    objectName: "credentialManager"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 700, 700)
    height: Math.min(parent ? parent.height - 44 : 720, 720)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    property string editId: ""
    property bool editHasPassphrase: false
    property bool clearPassphrase: false
    property string pendingDeleteId: ""
    property string pendingDeleteName: ""
    property string statusText: ""
    property bool statusIsError: false
    readonly property bool editing: editId.length > 0
    readonly property bool keyType: typeCombo.currentValue === "key"

    function openManager() {
        CredentialStore.reload()
        resetForm()
        statusText = CredentialStore.lastError
        statusIsError = statusText.length > 0
        open()
        nameField.forceActiveFocus()
    }

    function resetForm() {
        editId = ""
        editHasPassphrase = false
        clearPassphrase = false
        nameField.text = ""
        usernameField.text = ""
        typeCombo.currentIndex = 0
        clearSecrets()
    }

    function clearSecrets() {
        passwordField.text = ""
        passphraseField.text = ""
        keyFileField.text = ""
    }

    function startEdit(entry) {
        resetForm()
        editId = entry.id
        nameField.text = entry.name
        usernameField.text = entry.username
        typeCombo.currentIndex = entry.type === "key" ? 1 : 0
        editHasPassphrase = Boolean(entry.passphrase)
        statusText = ""
        nameField.forceActiveFocus()
    }

    function save() {
        const values = {id: editId, name: nameField.text.trim(), type: typeCombo.currentValue,
            username: usernameField.text.trim()}
        if (keyType) {
            values.keyFile = keyFileField.text
            values.passphrase = passphraseField.text
            values.clearPassphrase = clearPassphrase
        } else {
            values.password = passwordField.text
        }
        const id = CredentialStore.saveCredential(values)
        // Secrets leave the form whatever the outcome.
        clearSecrets()
        if (id.length === 0) {
            statusText = CredentialStore.lastError
            statusIsError = true
            return
        }
        statusText = (editing ? "Saved changes to " : "Saved ") + values.name + " in " + CredentialStore.backendName + "."
        statusIsError = false
        resetForm()
    }

    Overlay.modal: Rectangle { color: "#a0080b10" }
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 62
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
                Text {
                    text: "Credentials"
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(14)
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Names and usernames only · secrets stay in " + CredentialStore.backendName
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
            AppButton { text: ""; iconText: "×"; compact: true; implicitWidth: 30; onClicked: root.close() }
        }
    }

    contentItem: ColumnLayout {
        spacing: 11
        anchors.margins: 18

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: formColumn.implicitHeight + 20
            radius: Theme.radius
            color: Theme.canvas
            border.color: root.editing ? Theme.accent : Theme.borderSoft
            ColumnLayout {
                id: formColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Text {
                    text: root.editing ? "EDIT CREDENTIAL" : "NEW CREDENTIAL"
                    textFormat: Text.PlainText
                    color: Theme.accent
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                    font.letterSpacing: 0.8
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppTextField {
                        id: nameField
                        objectName: "credentialNameField"
                        Layout.fillWidth: true
                        placeholderText: "Name, e.g. prod-admin"
                        maximumLength: 64
                    }
                    AppComboBox {
                        id: typeCombo
                        objectName: "credentialTypeCombo"
                        Layout.preferredWidth: 190
                        enabled: !root.editing
                        model: [{text: "Username + password", value: "password"}, {text: "Username + SSH key", value: "key"}]
                        textRole: "text"; valueRole: "value"
                        onActivated: root.clearSecrets()
                    }
                }
                AppTextField {
                    id: usernameField
                    objectName: "credentialUsernameField"
                    Layout.fillWidth: true
                    placeholderText: "Username"
                    maximumLength: 256
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
                AppTextField {
                    id: passwordField
                    objectName: "credentialPasswordField"
                    visible: !root.keyType
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    maximumLength: 4096
                    placeholderText: root.editing ? "Saved on this device · leave blank to keep" : "Password"
                    Accessible.name: "Password to save in the keychain"
                }
                RowLayout {
                    visible: root.keyType
                    Layout.fillWidth: true
                    spacing: 8
                    AppTextField {
                        id: keyFileField
                        objectName: "credentialKeyFileField"
                        Layout.fillWidth: true
                        readOnly: true
                        placeholderText: root.editing ? "Saved on this device · choose a file to replace" : "Private key file (OpenSSH or PEM)"
                    }
                    AppButton {
                        compact: true
                        text: "Choose…"
                        onClicked: keyPicker.open()
                    }
                }
                RowLayout {
                    visible: root.keyType
                    Layout.fillWidth: true
                    spacing: 8
                    AppTextField {
                        id: passphraseField
                        objectName: "credentialPassphraseField"
                        Layout.fillWidth: true
                        enabled: !root.clearPassphrase
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        maximumLength: 4096
                        placeholderText: root.editHasPassphrase && !root.clearPassphrase
                                         ? "Passphrase saved on this device · leave blank to keep"
                                         : "Key passphrase · optional"
                        Accessible.name: "Key passphrase to save in the keychain"
                    }
                    AppButton {
                        compact: true
                        visible: root.editHasPassphrase
                        text: root.clearPassphrase ? "Keep passphrase" : "Forget passphrase"
                        onClicked: {
                            root.clearPassphrase = !root.clearPassphrase
                            passphraseField.text = ""
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.keyType
                          ? "The key is copied into the keychain. For each login it is written to a private temporary file and deleted afterwards."
                          : "Verification codes and other one-time prompts are never filled in. You type those yourself."
                    textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: Theme.textFaint; font.pixelSize: Theme.uiFontSize(8)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    AppButton {
                        visible: root.editing
                        compact: true
                        text: "Cancel edit"
                        onClicked: { root.resetForm(); root.statusText = "" }
                    }
                    AppButton {
                        objectName: "saveCredentialButton"
                        compact: true
                        emphasized: true
                        enabled: CredentialStore.available && nameField.text.trim().length > 0
                        text: root.editing ? "Save changes" : "Add credential"
                        iconText: root.editing ? "" : "+"
                        onClicked: root.save()
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0 || !CredentialStore.available
            text: CredentialStore.available ? root.statusText
                  : CredentialStore.backendName + " is not available, so credentials cannot be saved."
            textFormat: Text.PlainText
            color: root.statusIsError || !CredentialStore.available ? Theme.red : Theme.accent
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(10)
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: parent.width
                spacing: 8

                Rectangle {
                    visible: CredentialStore.credentials.length === 0
                    Layout.fillWidth: true
                    implicitHeight: 96
                    radius: Theme.radius
                    color: Theme.canvas
                    border.color: Theme.borderSoft
                    Column {
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "No saved credentials"
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: Theme.uiFontSize(12)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Add one above, then attach it to a session or use it in a tab."
                            textFormat: Text.PlainText
                            color: Theme.textMuted
                            font.pixelSize: Theme.uiFontSize(10)
                        }
                    }
                }

                Repeater {
                    model: CredentialStore.credentials
                    delegate: Rectangle {
                        required property var modelData
                        property var entry: modelData
                        objectName: "credentialRow:" + entry.id
                        Layout.fillWidth: true
                        implicitHeight: credentialRow.implicitHeight + 20
                        radius: Theme.radius
                        color: Theme.canvas
                        border.color: entry.id === root.editId ? Theme.accent : Theme.borderSoft
                        RowLayout {
                            id: credentialRow
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8
                            Column {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: entry.name
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize(11)
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: entry.username + " · " + (entry.type === "key"
                                          ? (entry.passphrase ? "SSH key with passphrase" : "SSH key")
                                          : "password") + " · saved on this device"
                                    textFormat: Text.PlainText
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize(9)
                                }
                            }
                            AppButton {
                                compact: true
                                text: "Edit"
                                onClicked: root.startEdit(entry)
                            }
                            AppButton {
                                compact: true
                                text: "Delete"
                                destructive: true
                                onClicked: {
                                    root.pendingDeleteId = entry.id
                                    root.pendingDeleteName = entry.name
                                    deleteConfirm.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 54
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.color: Theme.borderSoft
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Text {
                Layout.fillWidth: true
                text: "Use one in the current tab from the command palette: Use credential…"
                textFormat: Text.PlainText
                color: Theme.textFaint
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
            }
            AppButton { text: "Done"; emphasized: true; onClicked: root.close() }
        }
    }

    onClosed: clearSecrets()

    FileDialog {
        id: keyPicker
        objectName: "credentialKeyFilePicker"
        title: "Choose an SSH private key"
        nameFilters: ["SSH private keys (*.pem *.key id_*)", "All files (*)"]
        onAccepted: keyFileField.text = String(selectedFile)
    }

    Dialog {
        id: deleteConfirm
        objectName: "deleteCredentialConfirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 40 : 420, 420)
        height: 220
        modal: true
        title: "Delete " + root.pendingDeleteName + "?"
        standardButtons: Dialog.NoButton
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: Text {
            width: 360
            text: "Its secrets are removed from " + CredentialStore.backendName + ". Sessions that use it will ask you to choose another before connecting."
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
        }
        footer: DialogButtonBox {
            AppButton { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: deleteConfirm.close() }
            AppButton {
                objectName: "confirmDeleteCredentialButton"
                text: "Delete credential"
                destructive: true
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    const ok = CredentialStore.removeCredential(root.pendingDeleteId)
                    root.statusText = ok ? "Deleted " + root.pendingDeleteName + "." : CredentialStore.lastError
                    root.statusIsError = !ok
                    if (ok && root.editId === root.pendingDeleteId) root.resetForm()
                    root.pendingDeleteId = ""
                    deleteConfirm.close()
                }
            }
        }
    }
}
