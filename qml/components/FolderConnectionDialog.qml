import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root; objectName: "folderConnectionDialog"
    parent: Overlay.overlay; anchors.centerIn: parent
    width: Math.min(540, parent.width - 40); height: Math.min(580, parent.height - 40)
    title: "Folder connection rule"; modal: true
    property string path: ""
    property string error: ""
    function openForFolder(folder) { path = folder; error = ""; choice.load(SessionStore.folderConnection(folder)); open() }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
    contentItem: ScrollView {
        id: scroll; clip: true
        ColumnLayout {
            width: scroll.availableWidth; spacing: 12
            Text { Layout.fillWidth: true; text: root.path || "All root sessions"; textFormat: Text.PlainText; color: Theme.text; wrapMode: Text.Wrap }
            ConnectionChoice { id: choice; objectName: "folderConnectionChoice"; Layout.fillWidth: true; folderOnly: true }
            Text { Layout.fillWidth: true; text: root.error; textFormat: Text.PlainText; color: Theme.red; wrapMode: Text.Wrap }
        }
    }
    footer: DialogButtonBox {
        AppButton { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: root.close() }
        AppButton { objectName: "saveFolderConnectionButton"; text: "Save rule"; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; emphasized: true; onClicked: {
            if (SessionStore.assignFolderConnection(root.path, JSON.parse(JSON.stringify(choice.toSelection())))) root.close()
            else root.error = SessionStore.lastError
        } }
    }
}
