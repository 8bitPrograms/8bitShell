import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

Dialog {
    id: root
    objectName: "sshKeyManager"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 700, 700)
    height: Math.min(parent ? parent.height - 44 : 690, 690)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    property string pendingReplaceId: ""
    property string pendingDeleteId: ""
    property string statusText: ""

    function openManager() {
        SshKeyStore.reload()
        statusText = SshKeyStore.lastError
        passphraseField.text = ""
        open()
    }

    function chooseImports() {
        pendingReplaceId = ""
        keyPicker.fileMode = FileDialog.OpenFiles
        keyPicker.open()
    }

    function chooseReplacement(id) {
        pendingReplaceId = id
        keyPicker.fileMode = FileDialog.OpenFile
        keyPicker.open()
    }

    function processFiles() {
        const secret = passphraseField.text
        if (pendingReplaceId.length > 0) {
            const ok = SshKeyStore.replaceKey(pendingReplaceId, String(keyPicker.selectedFile), secret)
            statusText = ok ? "Key replaced. Existing assignments still point to it." : SshKeyStore.lastError
        } else {
            const files = keyPicker.selectedFiles
            let imported = 0
            for (let index = 0; index < files.length; ++index) {
                if (SshKeyStore.importKey(String(files[index]), "", secret).length === 0) break
                ++imported
            }
            if (SshKeyStore.lastError.length > 0 && imported > 0)
                statusText = imported + (imported === 1 ? " key imported; stopped: " : " keys imported; stopped: ") + SshKeyStore.lastError
            else
                statusText = imported > 0
                        ? imported + (imported === 1 ? " key imported." : " keys imported.")
                        : SshKeyStore.lastError
        }
        passphraseField.text = ""
        pendingReplaceId = ""
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
                    text: "SSH key library"
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(14)
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Private local copies · readable metadata · no key material in YAML"
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
            implicitHeight: introColumn.implicitHeight + 20
            radius: Theme.radius
            color: Theme.canvas
            border.color: Theme.borderSoft
            Column {
                id: introColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4
                Text {
                    width: parent.width
                    text: "OpenSSH / PEM keys are copied directly after validation. PPK keys are converted once with PuTTYgen; all live connections still use hardened system OpenSSH."
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(10)
                }
                Text {
                    width: parent.width
                    text: SshKeyStore.ppkAvailable
                          ? "PPK conversion is available."
                          : "PPK conversion is unavailable in this build; use an official package or install PuTTYgen."
                    textFormat: Text.PlainText
                    color: SshKeyStore.ppkAvailable ? Theme.green : Theme.amber
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: "PPK PASSPHRASE · OPTIONAL"
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                AppTextField {
                    id: passphraseField
                    objectName: "ppkPassphraseField"
                    Layout.fillWidth: true
                    placeholderText: "Used for this import only — never saved"
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                }
            }
            AppButton {
                objectName: "importSshKeysButton"
                Layout.alignment: Qt.AlignBottom
                text: "Import keys…"
                iconText: "+"
                emphasized: true
                onClicked: root.chooseImports()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            text: root.statusText
            textFormat: Text.PlainText
            color: SshKeyStore.lastError.length > 0 ? Theme.red : Theme.accent
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
                    visible: SshKeyStore.keys.length === 0
                    Layout.fillWidth: true
                    implicitHeight: 116
                    radius: Theme.radius
                    color: Theme.canvas
                    border.color: Theme.borderSoft
                    Column {
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "No managed SSH keys"
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: Theme.uiFontSize(12)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Import one or more private keys, then choose defaults."
                            textFormat: Text.PlainText
                            color: Theme.textMuted
                            font.pixelSize: Theme.uiFontSize(10)
                        }
                    }
                }

                Repeater {
                    model: SshKeyStore.keys
                    delegate: Rectangle {
                        required property var modelData
                        property var entry: modelData
                        objectName: "sshKeyRow:" + entry.id
                        Layout.fillWidth: true
                        implicitHeight: keyColumn.implicitHeight + 20
                        radius: Theme.radius
                        color: Theme.canvas
                        border.color: PersonalProfile.cyberArkEnabled
                                      && entry.id === SshKeyStore.cyberArkKeyId
                                      ? Theme.accent : Theme.borderSoft
                        ColumnLayout {
                            id: keyColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
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
                                        text: entry.format.toUpperCase() + " · " + entry.type + " " + entry.bits
                                        textFormat: Text.PlainText
                                        color: Theme.textMuted
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.uiFontSize(9)
                                    }
                                }
                                AppButton {
                                    visible: PersonalProfile.cyberArkEnabled
                                    compact: true
                                    text: entry.id === SshKeyStore.defaultKeyId ? "Default ✓" : "Set default"
                                    emphasized: entry.id === SshKeyStore.defaultKeyId
                                    onClicked: {
                                        SshKeyStore.setDefaultKey(entry.id === SshKeyStore.defaultKeyId ? "" : entry.id)
                                        root.statusText = SshKeyStore.lastError
                                    }
                                }
                                AppButton {
                                    compact: true
                                    text: entry.id === SshKeyStore.cyberArkKeyId ? "CyberArk ✓" : "CyberArk"
                                    emphasized: entry.id === SshKeyStore.cyberArkKeyId
                                    onClicked: {
                                        SshKeyStore.setCyberArkKey(entry.id === SshKeyStore.cyberArkKeyId ? "" : entry.id)
                                        root.statusText = SshKeyStore.lastError
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.fingerprint
                                    textFormat: Text.PlainText
                                    color: Theme.textFaint
                                    elide: Text.ElideMiddle
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.uiFontSize(8)
                                }
                                AppButton {
                                    compact: true
                                    text: "Replace…"
                                    iconText: "↻"
                                    onClicked: root.chooseReplacement(entry.id)
                                }
                                AppButton {
                                    compact: true
                                    text: "Remove"
                                    destructive: true
                                    onClicked: {
                                        root.pendingDeleteId = entry.id
                                        deleteConfirm.open()
                                    }
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
                text: SshKeyStore.storagePath
                textFormat: Text.PlainText
                color: Theme.textFaint
                elide: Text.ElideMiddle
                font.family: Theme.monoFont
                font.pixelSize: Theme.uiFontSize(8)
            }
            AppButton { text: "Done"; emphasized: true; onClicked: root.close() }
        }
    }

    FileDialog {
        id: keyPicker
        objectName: "sshKeyFilePicker"
        title: root.pendingReplaceId.length > 0 ? "Replace SSH key" : "Import SSH private keys"
        nameFilters: ["SSH private keys (*.ppk *.pem *.key id_*)", "All files (*)"]
        onAccepted: root.processFiles()
    }

    Dialog {
        id: deleteConfirm
        objectName: "removeSshKeyConfirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 40 : 420, 420)
        height: 220
        modal: true
        title: "Remove managed key?"
        standardButtons: Dialog.NoButton
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: Text {
            width: 360
            text: "The application's private copy will be deleted. The original source file is not touched. Sessions that explicitly reference it will be blocked."
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
        }
        footer: DialogButtonBox {
            AppButton { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: deleteConfirm.close() }
            AppButton {
                text: "Remove key"
                destructive: true
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    const ok = SshKeyStore.removeKey(root.pendingDeleteId)
                    root.statusText = ok ? "Managed key removed." : SshKeyStore.lastError
                    root.pendingDeleteId = ""
                    deleteConfirm.close()
                }
            }
        }
    }
}
