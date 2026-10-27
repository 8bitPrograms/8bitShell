import QtQuick
import QtQuick.Controls.Basic
import BitShell

AppButton {
    id: control
    property bool panelVisible: false
    property string helpText: ""
    compact: true
    Accessible.name: helpText
    Accessible.checkable: true
    Accessible.checked: panelVisible
    ToolTip.visible: hovered
    ToolTip.delay: 400
    ToolTip.text: helpText
    // Reflect persisted/actual panel state, not an optimistic local check state.
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.surfaceSelected
               : control.hovered ? Theme.surfaceHover
               : control.panelVisible ? Theme.accentDim : "transparent"
        border.color: control.activeFocus ? Theme.accent
                      : control.panelVisible ? Theme.accentDim : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
