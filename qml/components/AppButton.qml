import QtQuick
import QtQuick.Controls.Basic
import BitShell

AbstractButton {
    id: control
    property string iconText: ""
    property bool emphasized: false
    property bool compact: false
    property bool destructive: false
    readonly property color accentColor: destructive ? Theme.red : Theme.accent

    implicitHeight: compact ? 30 : 34
    implicitWidth: contentRow.implicitWidth + (compact ? 18 : 24)
    opacity: enabled ? 1 : 0.42
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // AbstractButton handles Space, but unlike Button it does not provide a
    // consistent Return/Enter activation across the platform styles.
    Keys.onReturnPressed: function(event) {
        if (!event.isAutoRepeat && control.enabled) control.clicked()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (!event.isAutoRepeat && control.enabled) control.clicked()
        event.accepted = true
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down
               ? (control.emphasized ? Qt.darker(control.accentColor, 1.25) : Theme.surfaceSelected)
               : control.hovered
                 ? (control.emphasized ? Qt.lighter(control.accentColor, 1.08) : Theme.surfaceHover)
                 : (control.emphasized ? control.accentColor : "transparent")
        border.color: control.activeFocus ? Theme.accent : control.emphasized ? "transparent" : Theme.border
        border.width: control.activeFocus ? 2 : 1

        Behavior on color {
            ColorAnimation { duration: 90 }
        }
    }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            spacing: 7
            anchors.centerIn: parent

        Text { textFormat: Text.PlainText;
            visible: control.iconText.length > 0
            text: control.iconText
            color: control.emphasized ? Theme.canvas : Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: control.compact ? 13 : 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Text { textFormat: Text.PlainText;
            visible: control.text.length > 0
            text: control.text
            color: control.emphasized ? Theme.canvas : control.destructive ? Theme.red : Theme.text
            font.family: Theme.uiFont
            font.pixelSize: control.compact ? 11 : 12
            font.weight: control.emphasized ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
        }
    }
}
