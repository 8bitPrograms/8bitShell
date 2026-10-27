import QtQuick
import QtQuick.Window
Item {
    implicitWidth: 28
    implicitHeight: 28
    Image {
        anchors.fill: parent
        source: "qrc:/qt/qml/BitShell/assets/bitshell.svg"
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
