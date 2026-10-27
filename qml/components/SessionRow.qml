import QtQuick
import QtQuick.Controls.Basic
import BitShell

ItemDelegate {
    id: control
    property int sessionIndex: -1
    property string displayName: ""
    property string subtitle: ""
    property color stateColor: Theme.textFaint
    property bool active: false
    property bool menuEnabled: false

    signal editRequested(int index)
    signal duplicateRequested(int index)
    signal removeRequested(int index, string name)

    width: ListView.view ? ListView.view.width : 260
    implicitHeight: 48
    hoverEnabled: true
    leftPadding: 10
    rightPadding: 9

    background: Rectangle {
        radius: Theme.radius
        color: control.active
               ? Theme.surfaceSelected
               : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.active ? Qt.rgba(0.27, 0.76, 0.69, 0.25) : "transparent"

        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Row {
        spacing: 10

        Rectangle {
            width: 7
            height: 7
            radius: 4
            color: control.stateColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Column {
            width: parent.width - 50
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter

            Text { textFormat: Text.PlainText;
                width: parent.width
                text: control.displayName
                color: Theme.text
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(12)
                font.weight: control.active ? Font.DemiBold : Font.Medium
            }

            Text { textFormat: Text.PlainText;
                width: parent.width
                text: control.subtitle
                color: Theme.textMuted
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(10)
            }
        }

        Text { textFormat: Text.PlainText;
            text: "⋮"
            color: control.hovered ? Theme.textMuted : "transparent"
            font.pixelSize: Theme.uiFontSize(17)
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: control.menuEnabled
        onTapped: contextMenu.popup()
    }

    AppMenu {
        id: contextMenu
        AppMenuItem {
            text: "Edit session"
            onTriggered: control.editRequested(control.sessionIndex)
        }
        AppMenuItem {
            text: "Duplicate"
            onTriggered: control.duplicateRequested(control.sessionIndex)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Delete"
            onTriggered: control.removeRequested(control.sessionIndex, control.displayName)
        }
    }
}
