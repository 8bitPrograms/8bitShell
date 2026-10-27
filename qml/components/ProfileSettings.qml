import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

ColumnLayout {
    id: root
    spacing: 8

    FileDialog {
        id: avatarPicker
        objectName: "avatarFilePicker"
        title: "Choose a profile picture"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Profile pictures (*.png *.jpg *.jpeg)"]
        onAccepted: PersonalProfile.chooseAvatar(selectedFile)
    }

    Text {
        text: "PROFILE"
        textFormat: Text.PlainText
        color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ProfileAvatar { Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
        ColumnLayout {
            Layout.fillWidth: true
            Text { text: PersonalProfile.displayName; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(12); font.weight: Font.DemiBold }
            Text { text: "Local 8bitShell profile"; textFormat: Text.PlainText; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10) }
        }
    }
    Text { text: "Display name"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
    AppTextField {
        id: displayName
        objectName: "profileDisplayNameField"
        Layout.fillWidth: true
        text: PersonalProfile.displayName
        placeholderText: "berry"
        maximumLength: 128
    }
    AppButton {
        objectName: "saveDisplayNameButton"
        text: "Save profile name"
        emphasized: true
        Layout.alignment: Qt.AlignLeft
        onClicked: PersonalProfile.setDisplayName(displayName.text)
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
    AppSwitch {
        objectName: "avatarEnabledCheck"
        Layout.fillWidth: true
        text: "Show profile picture instead of initial"
        checked: PersonalProfile.avatarEnabled
        onToggled: PersonalProfile.avatarEnabled = checked
    }
    RowLayout {
        AppButton { objectName: "chooseAvatarButton"; text: "Choose picture…"; onClicked: avatarPicker.open() }
        AppButton { text: "Remove app copy"; enabled: PersonalProfile.avatarUrl.toString().length > 0; onClicked: PersonalProfile.removeAvatar() }
    }
    Text {
        Layout.fillWidth: true
        text: "Pictures are optional and off by default. 8bitShell stores a small metadata-free PNG copy; the original stays untouched."
        textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
    }
    Text {
        visible: PersonalProfile.lastError.length > 0
        Layout.fillWidth: true
        text: PersonalProfile.lastError
        textFormat: Text.PlainText; color: Theme.red; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
    }
}
