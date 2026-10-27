import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

MenuItem {
    id: control

    property bool showSwatch: false
    property color swatchColor: "transparent"
    property bool selected: false
    hoverEnabled: true

    // Qt's native cascade bookkeeping still owns placement and closing. Opening
    // the associated submenu here makes pointer and keyboard highlighting
    // deterministic across Basic, Plasma and Windows styles.
    onHoveredChanged: {
        if (hovered && subMenu && !subMenu.visible)
            subMenu.open()
    }
    onHighlightedChanged: {
        if (highlighted && subMenu && !subMenu.visible)
            subMenu.open()
    }

    implicitWidth: 194
    implicitHeight: 34
    leftPadding: 10
    rightPadding: control.subMenu ? 28 : 10
    topPadding: 0
    bottomPadding: 0
    indicator: null
    arrow: Text {
        x: control.mirrored ? control.leftPadding
                            : control.width - width - 10
        y: (control.height - height) / 2
        visible: control.subMenu
        text: control.mirrored ? "‹" : "›"
        textFormat: Text.PlainText
        color: control.enabled ? Theme.textMuted : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(17)
    }

    contentItem: RowLayout {
        spacing: 8

        Rectangle {
            visible: control.showSwatch
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            radius: 6
            color: control.swatchColor
            border.width: 1
            border.color: control.swatchColor.a > 0
                          ? Qt.rgba(control.swatchColor.r, control.swatchColor.g,
                                    control.swatchColor.b, 0.95)
                          : Theme.textFaint
            Rectangle {
                visible: control.swatchColor.a === 0
                anchors.centerIn: parent
                width: 1
                height: 10
                rotation: 45
                color: Theme.textFaint
            }
        }

        Text {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: control.text
            color: control.enabled ? Theme.text : Theme.textFaint
            opacity: control.enabled ? 1 : 0.62
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
            font.weight: Font.Medium
        }

        Text {
            visible: control.selected
            text: "✓"
            textFormat: Text.PlainText
            color: Theme.accentBright
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(12)
            font.bold: true
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.highlighted
               ? Theme.surfaceSelected
               : control.down ? Theme.surfaceHover : "transparent"
        border.color: control.highlighted
                      ? Qt.rgba(0.27, 0.76, 0.69, 0.22)
                      : "transparent"
    }
}
