import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root
    objectName: "aboutDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 540, 540)
    height: Math.min(parent ? parent.height - 48 : 370, 370)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20

    function openAbout() { open() }

    Overlay.modal: Rectangle { color: "#a0080b10" }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radiusLarge }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            BrandMark {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
            }
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "8bitShell"
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.uiFontSize(20)
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Version " + Qt.application.version
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(10)
                }
            }
            AppButton { text: ""; iconText: "×"; compact: true; implicitWidth: 30; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
        Text {
            Layout.fillWidth: true
            text: "A fast, programmable remote-access terminal for SSH, SFTP, split views, macros, optional enterprise routing, and team-sized session catalogs."
            textFormat: Text.PlainText
            color: Theme.textMuted
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: securityColumn.implicitHeight + 20
            radius: Theme.radius
            color: Theme.canvas
            border.color: Theme.borderSoft
            Column {
                id: securityColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4
                Text {
                    text: "SECURITY MODEL"
                    textFormat: Text.PlainText
                    color: Theme.accent
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: "System OpenSSH transport · strict host-key verification · optional device-protected passwords · private local configuration"
                    textFormat: Text.PlainText
                    color: Theme.text
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.uiFontSize(10)
                }
            }
        }
        AppButton {
            objectName: "aboutGithubButton"
            Layout.alignment: Qt.AlignLeft
            text: "github.com/8bitPrograms/8bitShell"
            iconText: "↗"
            onClicked: Qt.openUrlExternally("https://github.com/8bitPrograms/8bitShell")
        }
        Text {
            Layout.fillWidth: true
            text: "Built for Linux and Windows · configuration is local and human-readable"
            textFormat: Text.PlainText
            color: Theme.textFaint
            wrapMode: Text.Wrap
            font.pixelSize: Theme.uiFontSize(9)
        }
    }
}
