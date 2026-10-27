import QtQuick
import BitShell

Item {
    id: root
    property string richText: ""
    property string timestamp: ""
    implicitHeight: lineText.implicitHeight + 2
    implicitWidth: parent ? parent.width : 800

    Text { textFormat: Text.PlainText;
        id: timeText
        visible: root.timestamp.length > 0
        width: visible ? 78 : 0
        text: root.timestamp
        color: Theme.textFaint
        font.family: Theme.monoFont
        font.pixelSize: Theme.uiFontSize(11)

    }

    Text { textFormat: Text.PlainText;
        id: lineText
        anchors.left: timeText.right
        anchors.leftMargin: timeText.visible ? 8 : 0
        anchors.right: parent.right
        text: root.richText
        color: Theme.text
        font.family: Theme.monoFont
        font.pixelSize: Theme.uiFontSize(12)

        wrapMode: Text.NoWrap
    }
}
