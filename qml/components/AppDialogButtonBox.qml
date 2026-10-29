import QtQuick
import QtQuick.Controls.Basic
import BitShell

DialogButtonBox {
    id: root
    spacing: 8
    padding: 12
    alignment: Qt.AlignRight

    function focusNeighbour(current, direction) {
        const buttons = []
        for (let index = 0; index < count; ++index) {
            const button = itemAt(index)
            if (button && button.visible && button.enabled) buttons.push(button)
        }
        // Follow the actual visual order, including platform/RTL button order.
        buttons.sort((left, right) => left.mapToItem(root, 0, 0).x - right.mapToItem(root, 0, 0).x)
        const index = buttons.indexOf(current)
        if (index < 0 || buttons.length === 0) return
        buttons[(index + direction + buttons.length) % buttons.length].forceActiveFocus(Qt.TabFocusReason)
    }

    delegate: AppButton {
        id: actionButton
        implicitWidth: Math.max(88, implicitContentWidth + 24)
        Keys.onLeftPressed: function(event) { root.focusNeighbour(actionButton, -1); event.accepted = true }
        Keys.onRightPressed: function(event) { root.focusNeighbour(actionButton, 1); event.accepted = true }
        // Leave Tab/Shift+Tab to Qt's modal focus traversal so editable fields
        // and scrollable review text remain reachable as well as the buttons.
    }
    background: Rectangle {
        implicitHeight: 58
        color: Theme.surface
        Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }
    }
}
