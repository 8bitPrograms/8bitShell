pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    color: Theme.surface
    border.color: Theme.borderSoft
    implicitWidth: 345
    Layout.minimumWidth: 300
    Layout.preferredWidth: 345
    Layout.maximumWidth: 520

    property int pendingDeleteIndex: -1
    property string pendingDeleteName: ""
    property string noticeText: ""

    function openNewRule() {
        ruleDialog.openCreate()
    }

    function showNotice(text) {
        noticeText = text
        noticeTimer.restart()
    }

    Timer {
        id: noticeTimer
        interval: 3200
        onTriggered: root.noticeText = ""
    }

    HighlightRuleDialog {
        id: ruleDialog
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: Theme.surfaceRaised
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 8
                spacing: 7

                Column {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { textFormat: Text.PlainText;
                        text: "HIGHLIGHT RULES"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(10)
                        font.bold: true
                        font.letterSpacing: 0.7
                    }
                    Text { textFormat: Text.PlainText;
                        text: HighlightRuleStore.count + " customisable rules"
                        color: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(8)
                    }
                }
                AppButton {
                    id: ruleCatalogButton
                    text: ""
                    iconText: "⋯"
                    compact: true
                    implicitWidth: 30
                    onClicked: actionsMenu.openBelow(ruleCatalogButton, true)
                    ToolTip.visible: hovered
                    ToolTip.text: "Rule catalog actions"
                }
                AppButton {
                    text: ""
                    iconText: "＋"
                    compact: true
                    implicitWidth: 30
                    emphasized: true
                    onClicked: ruleDialog.openCreate()
                    ToolTip.visible: hovered
                    ToolTip.text: "New highlight rule"
                }

                AppMenu {
                    id: actionsMenu
                    AppMenuItem {
                        text: "Restore default rules"
                        onTriggered: {
                            const restored = HighlightRuleStore.resetDefaults()
                            root.showNotice(restored ? "Default rules restored"
                                                     : HighlightRuleStore.lastError)
                        }
                    }
                    AppMenuItem {
                        text: "Reload saved rules"
                        onTriggered: {
                            const loaded = HighlightRuleStore.reload()
                            root.showNotice(loaded ? "Rules reloaded"
                                                   : HighlightRuleStore.lastError)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 61
            color: Theme.canvas
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 11
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 15
                    color: Theme.accentDim
                    Text { textFormat: Text.PlainText;
                        anchors.centerIn: parent
                        text: "✦"
                        color: Theme.accentBright
                        font.pixelSize: Theme.uiFontSize(13)
                    }
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { textFormat: Text.PlainText;
                        text: "Default profile"
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(10)
                        font.weight: Font.DemiBold
                    }
                    Text { textFormat: Text.PlainText;
                        text: "Applied live to every open terminal"
                        color: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(8)
                    }
                }
                Text { textFormat: Text.PlainText;
                    text: AppSettings.highlightByDefault ? "ON" : "PER TAB"
                    color: AppSettings.highlightByDefault ? Theme.green : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                    font.bold: true
                }
            }
        }

        ListView {
            id: rulesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: HighlightRuleStore
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: ruleDelegate
                required property int index
                required property string name
                required property string pattern
                required property string foreground
                required property string background
                required property bool bold
                required property bool underline
                required property bool enabledRule
                required property bool caseSensitive
                required property int priority

                width: ListView.view.width
                height: 72
                color: ruleHover.hovered ? Theme.surfaceHover : "transparent"
                border.color: Theme.borderSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 8
                    spacing: 9

                    Rectangle {
                        Layout.preferredWidth: 9
                        Layout.preferredHeight: 38
                        radius: 4
                        color: ruleDelegate.foreground
                        opacity: ruleDelegate.enabledRule ? 1 : 0.35
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text { textFormat: Text.PlainText;
                            Layout.fillWidth: true
                            text: ruleDelegate.name
                            color: ruleDelegate.enabledRule ? Theme.text : Theme.textMuted
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(10)
                            font.weight: Font.DemiBold
                        }
                        Text { textFormat: Text.PlainText;
                            Layout.fillWidth: true
                            text: ruleDelegate.pattern
                            color: Theme.textFaint
                            elide: Text.ElideRight
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.uiFontSize(8)
                        }
                        Text { textFormat: Text.PlainText;
                            text: "priority " + ruleDelegate.priority
                                  + (ruleDelegate.caseSensitive ? " · Aa" : " · aA")
                            color: Theme.textFaint
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(8)
                        }

                        HoverHandler {
                            id: ruleHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: ruleDialog.openEdit(ruleDelegate.index)
                        }
                    }

                    AppSwitch {
                        id: ruleSwitch
                        checked: ruleDelegate.enabledRule
                        implicitWidth: 38
                        implicitHeight: 24
                        onToggled: {
                            if (!HighlightRuleStore.setRuleEnabled(ruleDelegate.index, checked))
                                root.showNotice(HighlightRuleStore.lastError)
                        }
                    }

                    AppButton {
                        id: ruleActionsButton
                        text: ""
                        iconText: "⋯"
                        compact: true
                        implicitWidth: 28
                        onClicked: ruleMenu.openBelow(ruleActionsButton, true)
                    }
                    AppMenu {
                        id: ruleMenu
                        AppMenuItem {
                            text: "Edit"
                            onTriggered: ruleDialog.openEdit(ruleDelegate.index)
                        }
                        AppMenuItem {
                            text: "Duplicate"
                            onTriggered: {
                                const row = HighlightRuleStore.duplicateRule(ruleDelegate.index)
                                root.showNotice(row >= 0 ? "Rule duplicated"
                                                         : HighlightRuleStore.lastError)
                            }
                        }
                        AppMenuSeparator {}
                        AppMenuItem {
                            text: "Delete"
                            onTriggered: {
                                root.pendingDeleteIndex = ruleDelegate.index
                                root.pendingDeleteName = ruleDelegate.name
                                deleteDialog.open()
                            }
                        }
                    }
                }
            }

            Column {
                visible: HighlightRuleStore.count === 0
                anchors.centerIn: parent
                width: parent.width - 36
                spacing: 8
                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: "No highlight rules"
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Add rule"
                    iconText: "＋"
                    emphasized: true
                    compact: true
                    onClicked: ruleDialog.openCreate()
                }
            }
        }

        Rectangle {
            visible: root.noticeText.length > 0
            Layout.fillWidth: true
            implicitHeight: 31
            color: Theme.accentDim
            border.color: Theme.accent
            Text { textFormat: Text.PlainText;
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: Text.AlignVCenter
                text: root.noticeText
                color: Theme.accentBright
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 112
            color: Theme.canvas
            border.color: Theme.borderSoft

            Column {
                anchors.fill: parent
                anchors.margins: 11
                spacing: 7
                Text { textFormat: Text.PlainText;
                    text: "LIVE PREVIEW"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(8)
                    font.bold: true
                }
                Row {
                    spacing: 5
                    Text { textFormat: Text.PlainText;
                        text: "11:40:25"
                        color: Theme.textMuted
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(10)
                    }
                    Text { textFormat: Text.PlainText;
                        text: "ERROR"
                        color: Theme.red
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(10)
                        font.bold: true
                    }
                    Text { textFormat: Text.PlainText;
                        text: "timeout from"
                        color: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(10)
                    }
                    Text { textFormat: Text.PlainText;
                        text: "10.28.81.22"
                        color: Theme.blue
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(10)
                        font.underline: true
                    }
                }
                Text { textFormat: Text.PlainText;
                    text: "Rendering only; copied output stays untouched."
                    color: Theme.accent
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                Text { textFormat: Text.PlainText;
                    width: parent.width
                    text: HighlightRuleStore.storagePath
                    color: Theme.textFaint
                    elide: Text.ElideMiddle
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(8)
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete highlight rule?"
        standardButtons: Dialog.Yes | Dialog.Cancel

        Text { textFormat: Text.PlainText;
            width: 320
            text: "‘" + root.pendingDeleteName + "’ will stop applying to every terminal."
            color: Theme.text
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(11)
        }
        onAccepted: {
            const removed = HighlightRuleStore.removeRule(root.pendingDeleteIndex)
            root.showNotice(removed ? "Rule deleted" : HighlightRuleStore.lastError)
            root.pendingDeleteIndex = -1
        }
    }
}
