pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

ColumnLayout {
    id: root
    spacing: 10
    property bool showMonitoring: true
    readonly property var actions: [
        {id: "copy", label: "Copy"}, {id: "paste", label: "Paste"},
        {id: "pasteSelection", label: "Paste primary selection"},
        {id: "find", label: "Find"}, {id: "newTab", label: "New terminal"},
        {id: "closeTab", label: "Close terminal"}, {id: "nextTab", label: "Next terminal"},
        {id: "quickConnect", label: "Quick connect"},
        {id: "zoomIn", label: "Zoom in"}, {id: "zoomOut", label: "Zoom out"}
    ]
    AppSwitch {
        objectName: "copyOnSelectSwitch"
        Layout.fillWidth: true
        text: "Copy selection automatically"
        checked: AppSettings.copyOnSelect
        onToggled: AppSettings.copyOnSelect = checked
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: "Off by default. Right-click opens terminal actions. Hold Shift to select or open the menu in mouse-aware apps."
        wrapMode: Text.WordWrap
        color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10)
    }
    AppSwitch {
        visible: root.showMonitoring
        objectName: "defaultMonitoringSwitch"
        Layout.fillWidth: true
        text: "Enable monitoring in new terminals"
        checked: AppSettings.monitoringByDefault
        onToggled: AppSettings.monitoringByDefault = checked
    }
    Text { textFormat: Text.PlainText;
        visible: root.showMonitoring
        Layout.fillWidth: true
        text: "Off by default. Linux metrics use a separate, read-only SSH command channel with your key or agent. Toggle per tab with Monitor in the terminal's bottom bar."
        wrapMode: Text.WordWrap
        color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10)
    }
    Text { textFormat: Text.PlainText; text: "KEYBOARD SHORTCUTS"; color: Theme.accent; font.bold: true; font.pixelSize: Theme.uiFontSize(10) }
    RowLayout {
        AppButton { text: "Linux defaults"; compact: true; onClicked: AppSettings.applyKeyPreset("linux") }
        AppButton { text: "Insert keys"; compact: true; onClicked: AppSettings.applyKeyPreset("insert") }
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: "Click a shortcut, then press your keys. × disables it. Ctrl + mouse wheel always zooms; Shift + mouse selects in terminal apps."
        color: Theme.textMuted; wrapMode: Text.WordWrap; font.pixelSize: Theme.uiFontSize(10)
    }
    Repeater {
        model: root.actions
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
            AppTextField {
                id: capture
                objectName: "shortcut:" + modelData.id
                Layout.preferredWidth: 166
                readOnly: true
                text: AppSettings.keyBindings[modelData.id] || ""
                placeholderText: activeFocus ? "Press shortcut…" : "Disabled"
                color: activeFocus ? Theme.accent : Theme.text
                font.pixelSize: Theme.uiFontSize(11)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) { focus = false; event.accepted = true; return }
                    const shortcut = AppSettings.shortcutFromKey(event.key, event.modifiers)
                    if (shortcut.length) AppSettings.setKeyBinding(modelData.id, shortcut)
                    event.accepted = true
                }
                // Keep global QML shortcuts from taking over while recording.
                Keys.onShortcutOverride: function(event) { event.accepted = true }
            }
            AppButton {
                text: "×"; compact: true; implicitWidth: 27
                onClicked: AppSettings.setKeyBinding(modelData.id, "")
            }
        }
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        visible: text.length > 0
        text: AppSettings.shortcutError
        color: Theme.red; wrapMode: Text.WordWrap; font.pixelSize: Theme.uiFontSize(11)
    }
}
