import QtQuick
import BitShell

Rectangle {
    id: root
    implicitWidth: 32; implicitHeight: 32
    readonly property bool photo: PersonalProfile.avatarEnabled && picture.status === Image.Ready
    color: Theme.violet; radius: photo ? 4 : width / 2; clip: true
    Image {
        id: picture; objectName: "profileAvatarImage"
        anchors.fill: parent; anchors.margins: 2
        source: PersonalProfile.avatarEnabled ? PersonalProfile.avatarUrl : ""
        sourceSize.width: 256; sourceSize.height: 256
        fillMode: Image.PreserveAspectCrop; cache: false; asynchronous: true
        visible: root.photo
    }
    Text {
        anchors.centerIn: parent; visible: !root.photo
        text: PersonalProfile.initial; textFormat: Text.PlainText
        color: Theme.canvas; font.pixelSize: Math.round(root.width * 0.42); font.bold: true
    }
}
