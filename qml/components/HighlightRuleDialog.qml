import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 560, 560)
    height: Math.min(parent ? parent.height - 48 : 550, 550)
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    property int editIndex: -1
    property string formError: ""

    function validColor(value, allowEmpty) {
        const trimmed = value.trim()
        return (allowEmpty && trimmed.length === 0)
                || /^#[0-9a-fA-F]{6}$/.test(trimmed)
    }

    function openCreate() {
        editIndex = -1
        nameField.text = ""
        patternField.text = ""
        foregroundField.text = "#e8eef7"
        backgroundField.text = ""
        priorityBox.value = 50
        enabledCheck.checked = true
        caseCheck.checked = false
        boldCheck.checked = false
        underlineCheck.checked = false
        formError = ""
        open()
        nameField.forceActiveFocus()
    }

    function openEdit(index) {
        const rule = HighlightRuleStore.get(index)
        if (!rule || !rule.pattern)
            return
        editIndex = index
        nameField.text = rule.name || ""
        patternField.text = rule.pattern || ""
        foregroundField.text = rule.foreground || "#e8eef7"
        backgroundField.text = rule.background || ""
        priorityBox.value = rule.priority || 0
        enabledCheck.checked = Boolean(rule.enabled)
        caseCheck.checked = Boolean(rule.caseSensitive)
        boldCheck.checked = Boolean(rule.bold)
        underlineCheck.checked = Boolean(rule.underline)
        formError = ""
        open()
        nameField.forceActiveFocus()
    }

    function saveRule() {
        if (patternField.text.trim().length === 0) {
            formError = "A regular expression is required."
            patternField.forceActiveFocus()
            return
        }
        if (!validColor(foregroundField.text, false)) {
            formError = "Foreground must use #RRGGBB format."
            foregroundField.forceActiveFocus()
            return
        }
        if (!validColor(backgroundField.text, true)) {
            formError = "Background must be empty or use #RRGGBB format."
            backgroundField.forceActiveFocus()
            return
        }

        const values = {
            name: nameField.text.trim().length > 0
                  ? nameField.text.trim() : "Custom rule",
            pattern: patternField.text,
            foreground: foregroundField.text.trim(),
            background: backgroundField.text.trim(),
            priority: priorityBox.value,
            enabled: enabledCheck.checked,
            caseSensitive: caseCheck.checked,
            bold: boldCheck.checked,
            underline: underlineCheck.checked
        }
        const result = editIndex < 0
                       ? HighlightRuleStore.addRule(values)
                       : HighlightRuleStore.updateRule(editIndex, values)
                         ? editIndex : -1
        if (result < 0) {
            formError = HighlightRuleStore.lastError.length > 0
                        ? HighlightRuleStore.lastError : "Could not save the rule."
            return
        }
        close()
    }

    Overlay.modal: Rectangle { color: "#a0080b10" }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 57
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.color: Theme.borderSoft

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            Column {
                Layout.fillWidth: true
                spacing: 1
                Text { textFormat: Text.PlainText;
                    text: root.editIndex < 0 ? "New highlight rule" : "Edit highlight rule"
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(14)
                    font.weight: Font.DemiBold
                }
                Text { textFormat: Text.PlainText;
                    text: "Rendering only — logs and copied text remain unchanged"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
            AppButton {
                text: ""
                iconText: "×"
                compact: true
                implicitWidth: 30
                onClicked: root.close()
            }
        }
    }

    contentItem: ScrollView {
        id: formScroll
        clip: true
        leftPadding: 20
        rightPadding: 20
        topPadding: 17
        bottomPadding: 17
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: formScroll.availableWidth
            spacing: 13

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "DISPLAY NAME"
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Oracle errors"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { textFormat: Text.PlainText;
                    text: "REGULAR EXPRESSION"
                    color: Theme.accent
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
                TextArea {
                    textFormat: TextEdit.PlainText
                    id: patternField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    placeholderText: "\\b(ORA-[0-9]+|ERROR|FAILED)\\b"
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(10)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.canvas
                        border.color: patternField.activeFocus ? Theme.accent : Theme.border
                    }
                }
                Text { textFormat: Text.PlainText;
                    text: "Qt regular-expression syntax; matches are evaluated per terminal line."
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "FOREGROUND"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            radius: Theme.radiusSmall
                            color: root.validColor(foregroundField.text, false)
                                   ? foregroundField.text : "transparent"
                            border.color: Theme.border
                        }
                        TextField {
                            id: foregroundField
                            Layout.fillWidth: true
                            font.family: Theme.monoFont
                            placeholderText: "#e8eef7"
                            maximumLength: 7
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "BACKGROUND (OPTIONAL)"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            radius: Theme.radiusSmall
                            color: root.validColor(backgroundField.text, true)
                                   && backgroundField.text.length > 0
                                   ? backgroundField.text : Theme.canvas
                            border.color: Theme.border
                        }
                        TextField {
                            id: backgroundField
                            Layout.fillWidth: true
                            font.family: Theme.monoFont
                            placeholderText: "transparent"
                            maximumLength: 7
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                CheckBox { id: enabledCheck; text: "Enabled" }
                CheckBox { id: caseCheck; text: "Case sensitive" }
                CheckBox { id: boldCheck; text: "Bold" }
                CheckBox { id: underlineCheck; text: "Underline" }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 130
                    Layout.minimumWidth: 130
                    Layout.maximumWidth: 130
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "PRIORITY"
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.bold: true
                    }
                    SpinBox {
                        id: priorityBox
                        Layout.fillWidth: true
                        from: -10000
                        to: 10000
                        editable: true
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 220
                    implicitHeight: 58
                    radius: Theme.radius
                    color: backgroundField.text.length > 0
                           && root.validColor(backgroundField.text, true)
                           ? backgroundField.text : Theme.canvas
                    border.color: Theme.borderSoft
                    Row {
                        anchors.centerIn: parent
                        spacing: 7
                        Text { textFormat: Text.PlainText;
                            text: "PREVIEW"
                            color: Theme.textFaint
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.uiFontSize(10)
                        }
                        Text { textFormat: Text.PlainText;
                            text: "ERROR ORA-12541"
                            color: root.validColor(foregroundField.text, false)
                                   ? foregroundField.text : Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.uiFontSize(11)
                            font.bold: boldCheck.checked
                            font.underline: underlineCheck.checked
                        }
                    }
                }
            }

            Rectangle {
                visible: root.formError.length > 0
                Layout.fillWidth: true
                implicitHeight: errorText.implicitHeight + 18
                radius: Theme.radiusSmall
                color: "#2f1b24"
                border.color: Theme.red
                Text { textFormat: Text.PlainText;
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 9
                    text: root.formError
                    color: Theme.red
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 57
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Text { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: "Higher priority wins overlapping matches"
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(8)
            }
            AppButton { text: "Cancel"; onClicked: root.close() }
            AppButton {
                text: "Save rule"
                iconText: "✓"
                emphasized: true
                onClicked: root.saveRule()
            }
        }
    }
}
