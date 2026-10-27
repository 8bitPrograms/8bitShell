import QtQuick
import QtQuick.Controls.Basic
import BitShell

Menu {
    id: control
    // Keep the custom themed surface inside the application on every desktop.
    popupType: Popup.Item

    function openBelow(anchor, rightAligned) {
        const popupX = rightAligned ? anchor.width - implicitWidth : 0
        popup(anchor, popupX, anchor.height + 4)
    }

    modal: false
    dim: false
    cascade: true
    margins: 8
    padding: 5
    delegate: AppMenuItem {
        objectName: subMenu ? "appSubmenuItem:" + text : ""
    }
    implicitWidth: Math.max(204,
                            implicitContentWidth + leftPadding + rightPadding)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        implicitWidth: 204
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 65 }
    }
}
