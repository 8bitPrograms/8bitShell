pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import BitShell

AppComboBox {
    id: control
    property string selectedId: ""
    signal chosen(string themeId)
    textRole: "name"
    valueRole: "id"
    implicitWidth: 165
    implicitHeight: 26
    minimumPopupWidth: 210
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize(11)
    onActivated: chosen(currentValue)
    Binding {
        target: control
        property: "currentIndex"
        value: Theme.themeIndex(control.selectedId, control.model, -1)
    }
    contentItem: Text { textFormat: Text.PlainText;
        leftPadding: 10
        rightPadding: 28
        text: control.displayText
        color: Theme.text
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(11)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusSmall
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
    // Inherit the one popup/delegate pair from AppComboBox. A second ListView
    // bound to the same delegateModel steals rows from the visible popup.
}
