import QtQuick
import BitShell

Item {
    id: root
    implicitWidth: 14
    implicitHeight: 14
    property color color: Theme.textMuted

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = root.color
            context.lineWidth = 1.5
            context.lineCap = "round"
            context.beginPath()
            context.arc(5.8, 5.8, 3.7, 0, Math.PI * 2)
            context.moveTo(8.6, 8.6)
            context.lineTo(12.1, 12.1)
            context.stroke()
        }
        Connections {
            target: root
            function onColorChanged() {
                canvas.requestPaint()
            }
        }
    }
}
