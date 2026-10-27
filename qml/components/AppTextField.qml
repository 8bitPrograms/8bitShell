import QtQuick
import QtQuick.Controls.Basic
import BitShell

TextField {
    id: control
    implicitHeight: 26
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accentDim
    selectedTextColor: Theme.text
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize(11)
    leftPadding: 10
    rightPadding: 10
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.canvas
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
