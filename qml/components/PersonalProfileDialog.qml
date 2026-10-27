import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

Dialog {
    id: root; objectName: "personalProfileDialog"
    parent: Overlay.overlay; anchors.centerIn: parent
    width: Math.min(520, parent.width - 40); height: Math.min(560, parent.height - 40)
    title: "Personal profile"; modal: true; focus: true
    function openProfile() { PersonalProfile.reload(); username.text = PersonalProfile.username; open() }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
    FileDialog {
        id: avatarPicker; objectName: "avatarFilePicker"
        title: "Choose a profile picture"; fileMode: FileDialog.OpenFile
        nameFilters: ["Profile pictures (*.png *.jpg *.jpeg)"]
        onAccepted: PersonalProfile.chooseAvatar(selectedFile)
    }
    contentItem: ScrollView {
        id: scroll; clip: true
        ColumnLayout {
            width: scroll.availableWidth; spacing: 12
            ProfileAvatar { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 64; Layout.preferredHeight: 64 }
            Text { text: "EMPLOYEE USERNAME"; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
            AppTextField { id: username; objectName: "personalUsernameField"; Layout.fillWidth: true; placeholderText: "Your employee ID"; maximumLength: 128 }
            Text { Layout.fillWidth: true; text: "Used by CyberArk routes. Kept only on this computer, outside session and settings exports. No password is stored here."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(11) }
            CheckBox { objectName: "avatarEnabledCheck"; text: "Show profile picture instead of initial"; checked: PersonalProfile.avatarEnabled; onToggled: PersonalProfile.avatarEnabled = checked }
            RowLayout {
                AppButton { objectName: "chooseAvatarButton"; text: "Choose picture…"; onClicked: avatarPicker.open() }
                AppButton { text: "Remove copy"; enabled: PersonalProfile.avatarUrl.toString().length > 0; onClicked: PersonalProfile.removeAvatar() }
            }
            Text { Layout.fillWidth: true; text: "PNG/JPEG, up to 8 MiB and 4096 × 4096. A small metadata-free copy is stored by the app; your original stays untouched. The picture is off by default."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(11) }
            AppButton { text: "Forget gateway approvals"; onClicked: PersonalProfile.clearRouteApprovals() }
            Text { Layout.fillWidth: true; text: PersonalProfile.lastError; visible: text.length > 0; textFormat: Text.PlainText; color: Theme.red; wrapMode: Text.Wrap }
        }
    }
    footer: DialogButtonBox {
        AppButton { text: "Close"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: root.close() }
        AppButton { objectName: "savePersonalProfileButton"; text: "Save username"; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; emphasized: true; onClicked: { if (PersonalProfile.setUsername(username.text)) root.close() } }
    }
}
