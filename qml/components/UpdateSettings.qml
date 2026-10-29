pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell
ColumnLayout {
    id: root
    property var updater: null
    spacing: 12
    Text { text: "Updates"; color: Theme.text; font.bold: true; font.pixelSize: Theme.uiFontSize(14) }
    Text {
        Layout.fillWidth: true
        text: root.updater ? "Current version: " + root.updater.currentVersion : "Updates are optional."
        textFormat: Text.PlainText; color: Theme.text; wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        text: root.updater ? root.updater.status : ""
        textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap
    }
    CheckBox {
        Layout.fillWidth: true; text: "Check automatically for new versions"
        checked: root.updater ? root.updater.autoCheck : false
        enabled: root.updater !== null; palette.windowText: Theme.text
        onToggled: if (root.updater) root.updater.autoCheck = checked
    }
    CheckBox {
        Layout.fillWidth: true; text: "Install updates when I close the app"
        checked: root.updater ? root.updater.autoUpdate : false
        enabled: root.updater ? root.updater.canInstall : false
        palette.windowText: Theme.text
        onToggled: if (root.updater) root.updater.autoUpdate = checked
    }
    Text {
        Layout.fillWidth: true
        visible: root.updater ? !root.updater.canInstall : false
        text: "This copy is updated manually or by your system package manager."
        color: Theme.textMuted; wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: root.updater ? root.updater.downloading : false
        value: root.updater ? root.updater.progress : 0
    }
    AppButton {
        objectName: "checkUpdatesButton"; text: "Check now"
        enabled: root.updater ? !root.updater.checking && !root.updater.downloading && !root.updater.ready : false
        onClicked: root.updater.check()
    }
    AppButton {
        text: "Download update"
        enabled: root.updater ? root.updater.available && root.updater.canDownload && !root.updater.downloading && !root.updater.ready : false
        onClicked: root.updater.download()
    }
    AppButton {
        text: "Install and restart"
        visible: root.updater ? root.updater.canInstall : false
        enabled: root.updater ? root.updater.ready : false
        onClicked: root.updater.install()
    }
    AppButton {
        text: "Show download"
        visible: root.updater ? !root.updater.canInstall : false
        enabled: root.updater ? root.updater.ready : false
        onClicked: root.updater.showDownload()
    }
    AppButton {
        text: "Skip this version"
        enabled: root.updater ? root.updater.available || root.updater.ready : false
        onClicked: root.updater.skip()
    }
    AppButton { text: "Release notes and downloads"; enabled: root.updater !== null; onClicked: root.updater.openReleasePage() }
    TextArea {
        Layout.fillWidth: true; Layout.preferredHeight: 180
        text: root.updater ? root.updater.releaseNotes : ""
        textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap
        color: Theme.text
        background: Rectangle { color: Theme.canvas; border.color: Theme.borderSoft }
    }
}
