pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Drawer {
    id: root
    objectName: "settingsDrawer"
    edge: Qt.RightEdge
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(parent ? parent.width - 48 : 400, 400)
    height: parent ? parent.height : 760

    signal routesRequested()
    signal keysRequested()
    signal aboutRequested()

    property int currentSection: 0
    readonly property var sections: [
        { name: "Profile", icon: "●" },
        { name: "Appearance", icon: "◐" },
        { name: "Terminal", icon: ">_" },
        { name: "Shortcuts", icon: "⌨" },
        { name: "Authorization", icon: "◇" },
        { name: "Rules", icon: "✦" },
        { name: "Logging", icon: "●" },
        { name: "Data & About", icon: "i" },
        { name: "Local profiles", icon: ">_" }
    ]
    readonly property var availableFonts: {
        const families = Qt.fontFamilies().slice()
        const configured = AppSettings.terminalFontFamily
        if (configured.length > 0 && families.indexOf(configured) < 0)
            families.unshift(configured)
        return families
    }
    readonly property var backspaceOptions: [
        { text: "DEL · ^? · 0x7F (recommended)", value: "del" },
        { text: "BS · ^H · 0x08 (legacy / Oracle)", value: "bs" }
    ]

    function sectionIndex(name) {
        const wanted = String(name || "").toLowerCase()
        for (let index = 0; index < sections.length; ++index) {
            if (sections[index].name.toLowerCase() === wanted)
                return index
        }
        return 0
    }

    function optionIndex(options, value) {
        for (let index = 0; index < options.length; ++index) {
            if (options[index].value === value || options[index].id === value)
                return index
        }
        return 0
    }

    function openSection(name) {
        currentSection = sectionIndex(name)
        open()
    }

    function openRules(createRule) {
        currentSection = sectionIndex("Rules")
        open()
        if (createRule)
            Qt.callLater(function() { highlightPage.openNewRule() })
    }

    Overlay.modal: Rectangle { color: "#78080b10" }
    background: Rectangle { color: Theme.surface; border.color: Theme.border }

    ColumnLayout {
        id: panel
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: Theme.surfaceRaised
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 8
                Text {
                    text: "Settings"
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                AppButton {
                    text: ""
                    iconText: "×"
                    compact: true
                    implicitWidth: 28
                    implicitHeight: 24
                    onClicked: root.close()
                }
            }
        }

        Text {
            visible: AppSettings.lastError.length > 0
            Layout.fillWidth: true
            Layout.margins: visible ? 10 : 0
            text: AppSettings.lastError
            textFormat: Text.PlainText
            color: Theme.red
            font.pixelSize: Theme.uiFontSize(10)
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: sectionFlow.implicitHeight + 8
            color: Theme.canvas
            border.color: Theme.borderSoft

            Flow {
                id: sectionFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                spacing: 2

                Repeater {
                    model: root.sections
                    delegate: ItemDelegate {
                        id: sectionButton
                        required property var modelData
                        required property int index
                        objectName: "settingsSection:" + modelData.name
                        // Measure the bold label so selection cannot change the
                        // tab width and shove the row onto a different line.
                        width: sectionMeasure.implicitWidth + leftPadding + rightPadding
                        implicitHeight: 26
                        padding: 0
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 0
                        bottomPadding: 0
                        hoverEnabled: true
                        onClicked: root.currentSection = index
                        background: Rectangle {
                            radius: 2
                            color: sectionButton.index === root.currentSection
                                   ? Theme.surfaceSelected
                                   : sectionButton.hovered ? Theme.surfaceHover : "transparent"
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: sectionButton.index === root.currentSection ? 2 : 0
                                color: Theme.accent
                            }
                        }
                        Text {
                            id: sectionMeasure
                            visible: false
                            text: sectionButton.modelData.name
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(12)
                            font.weight: Font.DemiBold
                        }
                        contentItem: Text {
                            id: sectionLabel
                            text: sectionButton.modelData.name
                            textFormat: Text.PlainText
                            color: sectionButton.index === root.currentSection
                                   ? Theme.text : Theme.textMuted
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(12)
                            font.weight: sectionButton.index === root.currentSection
                                         ? Font.DemiBold : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        StackLayout {
                id: pageStack
                objectName: "settingsPages"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentSection

                ScrollView {
                    id: profileScroll
                    objectName: "profileSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: profilePage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ProfileSettings { id: profilePage; width: profileScroll.availableWidth }
                }

                ScrollView {
                    id: appearanceScroll
                    objectName: "appearanceSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: appearancePage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    AppearanceSettings { id: appearancePage; width: appearanceScroll.availableWidth }
                }

                ScrollView {
                    id: terminalScroll
                    objectName: "terminalSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: terminalPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: terminalPage
                        width: terminalScroll.availableWidth
                        spacing: 8

                        Text {
                            text: "TERMINAL"
                            textFormat: Text.PlainText
                            color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Font family"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                            AppComboBox {
                                id: fontCombo
                                objectName: "terminalFontCombo"
                                Layout.fillWidth: true
                                model: root.availableFonts
                                onActivated: AppSettings.terminalFontFamily = currentText
                                Component.onCompleted: currentIndex = find(AppSettings.terminalFontFamily)
                                Connections {
                                    target: AppSettings
                                    function onTerminalFontFamilyChanged() {
                                        fontCombo.currentIndex = fontCombo.find(AppSettings.terminalFontFamily)
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Text { Layout.fillWidth: true; text: "Base font size"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                                Text { text: Math.round(fontSlider.value) + " px"; textFormat: Text.PlainText; color: Theme.textMuted; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(9) }
                            }
                            Slider {
                                id: fontSlider
                                Layout.fillWidth: true
                                from: 9; to: 22; stepSize: 1
                                value: AppSettings.terminalFontSize
                                onMoved: AppSettings.terminalFontSize = value
                            }
                        }

                        TerminalRenderingSettings { Layout.fillWidth: true }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Scrollback lines"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 100; to: 100000; stepSize: 1000; editable: true
                                value: AppSettings.scrollbackLines
                                onValueModified: AppSettings.scrollbackLines = value
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "History also has a 1 MiB memory budget per tab. Older lines are removed when either limit is reached. Enable session logging to keep a complete record."
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.pixelSize: Theme.uiFontSize(10)
                                wrapMode: Text.Wrap
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Backspace sends"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                            AppComboBox {
                                id: backspaceCombo
                                objectName: "defaultBackspaceCombo"
                                Layout.fillWidth: true
                                model: root.backspaceOptions
                                textRole: "text"
                                valueRole: "value"
                                onActivated: AppSettings.backspaceMode = currentValue
                                Component.onCompleted: currentIndex = root.optionIndex(root.backspaceOptions, AppSettings.backspaceMode)
                                Connections {
                                    target: AppSettings
                                    function onBackspaceModeChanged() {
                                        backspaceCombo.currentIndex = root.optionIndex(root.backspaceOptions, AppSettings.backspaceMode)
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "DEL matches modern Linux terminals. Choose BS for legacy targets such as some Oracle environments; a saved session can override this default."
                                textFormat: Text.PlainText
                                color: Theme.textFaint; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(9)
                            }
                        }

                        AppSwitch {
                            Layout.fillWidth: true
                            text: "Enable monitoring in new terminals"
                            checked: AppSettings.monitoringByDefault
                            onToggled: AppSettings.monitoringByDefault = checked
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Off by default. Remote metrics use a separate, read-only SSH command channel. You can also toggle Monitor per tab."
                            textFormat: Text.PlainText
                            color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                        }
                    }
                }

                ScrollView {
                    id: shortcutsScroll
                    objectName: "shortcutSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: shortcutsPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    InputSettings { id: shortcutsPage; width: shortcutsScroll.availableWidth; showMonitoring: false }
                }

                ScrollView {
                    id: authorizationScroll
                    objectName: "authorizationSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: authorizationPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: authorizationPage
                        width: authorizationScroll.availableWidth
                        spacing: 8

                        Text {
                            text: "AUTHORIZATION"
                            textFormat: Text.PlainText
                            color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: keySettingsRow.implicitHeight + 8
                            radius: Theme.radius
                            color: Theme.canvas
                            border.color: Theme.borderSoft
                            RowLayout {
                                id: keySettingsRow
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "SSH keys"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(11); font.weight: Font.DemiBold }
                                    Text {
                                        text: SshKeyStore.keys.length + (SshKeyStore.keys.length === 1 ? " managed key" : " managed keys")
                                        textFormat: Text.PlainText; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(9)
                                    }
                                }
                                AppButton {
                                    objectName: "openSshKeyManagerButton"
                                    text: "Manage…"; iconText: "⌁"
                                    onClicked: root.keysRequested()
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

                        AppSwitch {
                            objectName: "cyberArkSettingsToggle"
                            Layout.fillWidth: true
                            text: "Enable CyberArk routing"
                            checked: PersonalProfile.cyberArkEnabled
                            onToggled: PersonalProfile.cyberArkEnabled = checked
                        }
                        Text {
                            visible: !PersonalProfile.cyberArkEnabled
                            Layout.fillWidth: true
                            text: "Leave this off unless your organization uses a CyberArk PSMP gateway. No CyberArk fields or routes are shown while disabled."
                            textFormat: Text.PlainText
                            color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                        }

                        ColumnLayout {
                            objectName: "cyberArkSettingsDetails"
                            visible: PersonalProfile.cyberArkEnabled
                            Layout.fillWidth: true
                            spacing: 8

                            Text { text: "Employee ID"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                            AppTextField {
                                id: employeeIdField
                                objectName: "personalUsernameField"
                                Layout.fillWidth: true
                                text: PersonalProfile.employeeId
                                placeholderText: "FB010063"
                                maximumLength: 128
                            }
                            AppButton {
                                objectName: "saveEmployeeIdButton"
                                text: "Save employee ID"
                                emphasized: true
                                Layout.alignment: Qt.AlignLeft
                                onClicked: PersonalProfile.setEmployeeId(employeeIdField.text)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "The employee ID stays in your local personal profile. It is not written into exported sessions."
                                textFormat: Text.PlainText
                                color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                AppButton { text: "Connection routes…"; onClicked: root.routesRequested() }
                                AppButton { text: "Forget gateway approvals"; onClicked: PersonalProfile.clearRouteApprovals() }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: SshKeyStore.cyberArkKeyId.length > 0
                                      ? "A managed SSH key is assigned to CyberArk routes."
                                      : "No dedicated CyberArk key is selected. Choose one in SSH keys."
                                textFormat: Text.PlainText
                                color: SshKeyStore.cyberArkKeyId.length > 0 ? Theme.green : Theme.textFaint
                                wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(9)
                            }
                        }
                    }
                }

                HighlightPanel {
                    id: highlightPage
                    objectName: "settingsHighlightPanel"
                }

                ScrollView {
                    id: loggingScroll
                    objectName: "loggingSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: loggingPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: loggingPage
                        width: loggingScroll.availableWidth
                        spacing: 8

                        Text { text: "LOGGING"; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: "Record new terminals by default"
                            checked: AppSettings.logByDefault
                            onToggled: AppSettings.logByDefault = checked
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Logs are ordinary UTF-8 plain text. ANSI styling is removed; timestamps are optional."
                            textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                        }

                        Text { text: "Log directory"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                        RowLayout {
                            Layout.fillWidth: true
                            AppTextField {
                                Layout.fillWidth: true
                                text: AppSettings.logDirectory
                                font.family: Theme.monoFont
                                onEditingFinished: AppSettings.logDirectory = text
                            }
                            AppButton {
                                text: ""; iconText: "↗"; compact: true; implicitWidth: 31
                                onClicked: Qt.openUrlExternally("file://" + AppSettings.logDirectory)
                            }
                        }

                        Text { text: "File name template"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                        AppTextField {
                            Layout.fillWidth: true
                            text: AppSettings.logFileNameTemplate
                            font.family: Theme.monoFont
                            onEditingFinished: AppSettings.logFileNameTemplate = text
                        }
                        Text { text: "Tokens: {session}  {host}  {user}  {date}  {time}"; textFormat: Text.PlainText; color: Theme.textFaint; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(9) }

                        Text { text: "Session subfolder"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                        AppTextField {
                            Layout.fillWidth: true
                            text: AppSettings.logDirectoryTemplate
                            font.family: Theme.monoFont
                            onEditingFinished: AppSettings.logDirectoryTemplate = text
                        }

                        AppSwitch {
                            Layout.fillWidth: true
                            text: "Timestamp each output line"
                            checked: AppSettings.logTimestamps
                            onToggled: AppSettings.logTimestamps = checked
                        }
                        Text {
                            Layout.fillWidth: true
                            text: AppSettings.logTimestamps
                                  ? "Each completed line receives a local date and time prefix."
                                  : "Plain mode: terminal output is written without a prefix."
                            textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: "Append existing file"
                            checked: AppSettings.logAppend
                            onToggled: AppSettings.logAppend = checked
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: "Rotate after (MB)"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                                SpinBox {
                                    Layout.fillWidth: true; from: 0; to: 4096; editable: true
                                    value: AppSettings.logRotationMegabytes
                                    onValueModified: AppSettings.logRotationMegabytes = value
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: "Keep rotations"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                                SpinBox {
                                    Layout.fillWidth: true; from: 0; to: 50; editable: true
                                    value: AppSettings.logRetainedFiles
                                    onValueModified: AppSettings.logRetainedFiles = value
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: dataScroll
                    objectName: "dataSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: dataPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: dataPage
                        width: dataScroll.availableWidth
                        spacing: 8

                        Text { text: "CONFIGURATION · YAML"; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
                        Text {
                            Layout.fillWidth: true
                            text: "Readable UTF-8 files. Import only configuration you trust; passwords, passphrases and prompt answers are never exported."
                            textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(10)
                        }
                        YamlFileActions {
                            objectName: "settingsYamlActions"
                            Layout.fillWidth: true
                            storage: AppSettings; label: "Settings"; settings: true
                        }
                        Text { text: "Sessions"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(11); font.bold: true }
                        YamlFileActions {
                            objectName: "sessionsYamlActions"
                            Layout.fillWidth: true
                            storage: SessionStore; label: "Sessions"
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
                        Text { text: "LOCAL DATA"; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: dataColumn.implicitHeight + 20
                            radius: Theme.radius
                            color: Theme.canvas
                            border.color: Theme.borderSoft
                            Column {
                                id: dataColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 5
                                Text { width: parent.width; text: "Session catalog"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(10) }
                                Text { width: parent.width; text: SessionStore.storagePath; textFormat: Text.PlainText; color: Theme.textFaint; elide: Text.ElideMiddle; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(8) }
                                Text { width: parent.width; text: "Settings · " + AppSettings.storagePath; textFormat: Text.PlainText; color: Theme.textFaint; elide: Text.ElideMiddle; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(8) }
                                Text { width: parent.width; text: "No passwords, passphrases, or prompt answers are stored."; textFormat: Text.PlainText; color: Theme.green; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(9) }
                            }
                        }
                        AppButton {
                            Layout.alignment: Qt.AlignLeft
                            text: "Restore defaults"; iconText: "↻"
                            onClicked: AppSettings.restoreDefaults()
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
                        AppButton {
                            objectName: "openAboutButton"
                            Layout.alignment: Qt.AlignLeft
                            text: "About 8bitShell"; iconText: "i"
                            onClicked: root.aboutRequested()
                        }
                    }
                }
                ScrollView {
                    id: localProfilesScroll
                    objectName: "localProfilesSettingsScroll"
                    clip: true
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 12
                    implicitHeight: localProfilesPage.implicitHeight + topPadding + bottomPadding
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    LocalProfileSettings { id: localProfilesPage; width: localProfilesScroll.availableWidth }
                }
        }
    }
}
