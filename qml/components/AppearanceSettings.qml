import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

ColumnLayout {
    id: root
    spacing: 12
    readonly property var modes: [
        { name: "Night · manual", id: "dark" },
        { name: "Day · manual", id: "light" },
        { name: "Auto · follow OS", id: "system" },
        { name: "Auto · schedule", id: "schedule" }
    ]

    Text { textFormat: Text.PlainText;
        text: "Appearance mode"
        color: Theme.text
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(11)
    }
    ThemePicker {
        objectName: "appearanceModeCombo"
        Layout.fillWidth: true
        model: root.modes
        selectedId: AppSettings.appearanceMode
        onChosen: function(themeId) { AppSettings.appearanceMode = themeId }
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: (AppSettings.darkAppearance ? "Night themes active" : "Day themes active")
              + (AppSettings.appearanceMode === "system"
                 ? (AppSettings.systemThemeAvailable ? " · following OS"
                    : " · OS preference unavailable; using night")
                 : AppSettings.appearanceMode === "schedule" ? " · local time" : " · manual")
        color: Theme.textMuted
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
    }

    RowLayout {
        visible: AppSettings.appearanceMode === "schedule"
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; text: "Day starts"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
            TextField {
                objectName: "dayStartField"
                Layout.fillWidth: true
                text: AppSettings.dayStart
                inputMask: "99:99"
                validator: RegularExpressionValidator { regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/ }
                onEditingFinished: {
                    if (acceptableInput) AppSettings.dayStart = text
                    text = Qt.binding(function() { return AppSettings.dayStart })
                }
                Accessible.name: "Day starts, local time HH:mm"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; text: "Night starts"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
            TextField {
                objectName: "nightStartField"
                Layout.fillWidth: true
                text: AppSettings.nightStart
                inputMask: "99:99"
                validator: RegularExpressionValidator { regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/ }
                onEditingFinished: {
                    if (acceptableInput) AppSettings.nightStart = text
                    text = Qt.binding(function() { return AppSettings.nightStart })
                }
                Accessible.name: "Night starts, local time HH:mm"
            }
        }
    }
    Text { textFormat: Text.PlainText;
        visible: AppSettings.appearanceMode === "schedule"
        Layout.fillWidth: true
        text: AppSettings.dayStart === AppSettings.nightStart
              ? "Matching times keep night themes on all day."
              : "24-hour local time. Overnight day schedules are supported."
        color: Theme.textMuted
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
    }

    AppSwitch {
        objectName: "separateTerminalThemeSwitch"
        Layout.fillWidth: true
        text: "Separate terminal & UI themes"
        checked: AppSettings.separateTerminalTheme
        onToggled: {
            AppSettings.separateTerminalTheme = checked
            checked = Qt.binding(function() { return AppSettings.separateTerminalTheme })
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 10
        rowSpacing: 10
        Item { Layout.preferredWidth: 64 }
        Text { textFormat: Text.PlainText; text: "DAY"; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
        Text { textFormat: Text.PlainText; text: "NIGHT"; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
        Text { textFormat: Text.PlainText; text: AppSettings.separateTerminalTheme ? "UI" : "Theme"; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
        ThemePicker {
            objectName: "uiLightThemeCombo"
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            model: Theme.selectableUiThemes(AppSettings.uiLightThemeId)
            selectedId: AppSettings.uiLightThemeId
            onChosen: function(themeId) { AppSettings.uiLightThemeId = themeId }
            Accessible.name: AppSettings.separateTerminalTheme ? "UI day theme" : "UI and terminal day theme"
        }
        ThemePicker {
            objectName: "uiThemeCombo"
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            model: Theme.selectableUiThemes(AppSettings.uiThemeId)
            selectedId: AppSettings.uiThemeId
            onChosen: function(themeId) { AppSettings.uiThemeId = themeId }
            Accessible.name: AppSettings.separateTerminalTheme ? "UI night theme" : "UI and terminal night theme"
        }
        Text { visible: AppSettings.separateTerminalTheme; textFormat: Text.PlainText; text: "Terminal"; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
        ThemePicker {
            objectName: "terminalLightThemeCombo"
            visible: AppSettings.separateTerminalTheme
            enabled: visible
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            model: Theme.selectableTerminalThemes(AppSettings.terminalLightThemeId)
            selectedId: AppSettings.terminalLightThemeId
            onChosen: function(themeId) { AppSettings.terminalLightThemeId = themeId }
            Accessible.name: "Terminal day theme"
        }
        ThemePicker {
            objectName: "terminalThemeCombo"
            visible: AppSettings.separateTerminalTheme
            enabled: visible
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            model: Theme.selectableTerminalThemes(AppSettings.terminalThemeId)
            selectedId: AppSettings.terminalThemeId
            onChosen: function(themeId) { AppSettings.terminalThemeId = themeId }
            Accessible.name: "Terminal night theme"
        }
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: AppSettings.separateTerminalTheme
              ? "Choose UI and terminal themes independently. To keep the terminal dark all day, use a dark terminal theme in both columns."
              : "One theme changes the UI and terminal together. Your separate terminal choices are remembered."
        color: Theme.textMuted
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 48
        radius: Theme.radius
        color: Theme.terminalBackground
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 7
            Text { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: "berry@host:~$ theme preview"
                color: Theme.activeTerminalTheme.foreground
                font.family: Theme.monoFont
                font.pixelSize: Theme.uiFontSize(10)
                elide: Text.ElideRight
            }
            Repeater {
                model: Theme.activeTerminalTheme.ansi.slice(1, 7)
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: 10
                    implicitHeight: 10
                    radius: 5
                    color: modelData
                }
            }
        }
    }
}
