import QtQuick
import QtQuick.Controls.Basic
import BitShell

Item {
    id: root
    implicitWidth: 13
    implicitHeight: 15
    property color color: Theme.textMuted
    property color backgroundColor: Theme.surface
    // Protocol indicator, deliberately not a green "authenticated" badge.
    readonly property string explanation: "SSH session · host key verification is required. This icon is not a live connection status; check authentication in the terminal."
    Accessible.role: Accessible.StaticText
    Accessible.name: explanation
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 1
        width: 7
        height: 8
        radius: 3
        color: "transparent"
        border.width: 1
        border.color: root.color
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 6
        width: 11
        height: 8
        radius: 2
        color: root.backgroundColor
        border.width: 1
        border.color: root.color
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 3
            color: root.color
        }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 500
    ToolTip.text: explanation
}
