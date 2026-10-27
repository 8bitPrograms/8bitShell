import QtQuick
import QtQuick.Layouts
import BitShell

ColumnLayout {
    spacing: 6
    Text {
        textFormat: Text.PlainText
        text: "Text smoothing"
        color: Theme.text
        font.pixelSize: Theme.uiFontSize(10)
    }
    ThemePicker {
        objectName: "terminalAntialiasingCombo"
        Layout.fillWidth: true
        model: [
            { id: "system", name: "System / auto (default)" },
            { id: "grayscale", name: "Grayscale · smooth, no color fringes" },
            { id: "aliased", name: "Sharp pixels · no smoothing" }
        ]
        selectedId: AppSettings.terminalTextAntialiasing
        onChosen: function(mode) { AppSettings.terminalTextAntialiasing = mode }
        Accessible.name: "Terminal text smoothing"
    }
    Text {
        textFormat: Text.PlainText
        text: "Font hinting · align strokes to pixels"
        color: Theme.text
        font.pixelSize: Theme.uiFontSize(10)
    }
    ThemePicker {
        objectName: "terminalHintingCombo"
        Layout.fillWidth: true
        model: [
            { id: "default", name: "System default" },
            { id: "none", name: "None · natural outlines" },
            { id: "vertical", name: "Light · vertical alignment" },
            { id: "full", name: "Full · horizontal and vertical alignment" }
        ]
        selectedId: AppSettings.terminalFontHinting
        onChosen: function(mode) { AppSettings.terminalFontHinting = mode }
        Accessible.name: "Terminal font hinting"
    }
    Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        text: "Applies to all terminals immediately. System mode lets Qt and the OS choose; subpixel smoothing depends on the display/backend. Hinting support varies. Desktop font settings are not changed."
        color: Theme.textMuted
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
    }
}
