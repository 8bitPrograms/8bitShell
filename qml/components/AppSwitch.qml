import QtQuick
import QtQuick.Controls.Basic
import BitShell

Switch {
    id: control
    hoverEnabled: true
    implicitWidth: text.length > 0 ? label.implicitWidth : indicator.implicitWidth
    implicitHeight: 24
    spacing: 8

    indicator: Rectangle {
        id: indicator
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 36
        implicitHeight: 20
        radius: 10
        color: control.checked ? Theme.accentDim : Theme.canvas
        border.color: control.checked ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.45

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            radius: 7
            color: control.checked ? Theme.accentBright : Theme.textFaint

            Behavior on x {
                NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Text { textFormat: Text.PlainText;
        id: label
        leftPadding: control.text.length > 0
                     ? control.indicator.width + control.spacing : 0
        text: control.text
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(10)
    }
}
