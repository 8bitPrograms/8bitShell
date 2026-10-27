import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    required property var workspace
    readonly property bool reviewVisible: review.visible
    readonly property int targetCount: { const rows = workspace.broadcastRows; return workspace.broadcastTerminals().length }
    implicitHeight: 88; color: Theme.surfaceRaised; border.color: targetCount ? Theme.amber : Theme.border
    function cancelReview() { sender.cancel() }
    function reviewCommand() {
        if (workspace.securityPromptActive) return
        if (sender.prepare(commandInput.text, workspace.broadcastTerminals())) { targetPopup.close(); review.open() }
    }
    BroadcastController { id: sender; objectName: "broadcastController" }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 4
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            AppButton { objectName: "multiSshTargetsButton"; text: "Targets · " + root.targetCount; compact: true; onClicked: targetPopup.open() }
            AppComboBox {
                objectName: "broadcastScopeSelector"; Layout.preferredWidth: 155; implicitHeight: 28; font.pixelSize: Theme.uiFontSize(11)
                model: ["All open SSH", "Visible panes only"]
                currentIndex: workspace.broadcastScope === "all" ? 0 : 1
                onActivated: { workspace.broadcastScope = currentIndex === 0 ? "all" : "visible"; root.cancelReview() }
            }
            AppTextField {
                id: commandInput; objectName: "broadcastCommandField"; Layout.fillWidth: true; Layout.minimumWidth: 40
                maximumLength: 16384; placeholderText: "Command for selected SSH terminals…"
                onAccepted: root.reviewCommand()
                onTextEdited: root.cancelReview()
            }
            AppButton { objectName: "broadcastReviewButton"; text: "Review"; compact: true; emphasized: true; enabled: root.targetCount > 0 && commandInput.text.trim().length > 0; onClicked: root.reviewCommand() }
        }
        Text {
            Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.amber; font.pixelSize: Theme.uiFontSize(10)
            text: sender.status || "Select authenticated SSH shells only — never password/MFA prompts. New sessions are excluded."
        }
    }
    Popup {
        id: targetPopup; objectName: "broadcastTargetsPopup"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(640, parent.width - 40); height: Math.min(520, parent.height - 50)
        modal: true; focus: true; padding: 12
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: "Multi SSH targets"; textFormat: Text.PlainText; color: Theme.text; font.bold: true; font.pixelSize: Theme.uiFontSize(15) }
            Text { Layout.fillWidth: true; text: "Selecting a target confirms its login is complete and it is ready for commands. Local shells cannot be selected."; textFormat: Text.PlainText; color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.uiFontSize(11) }
            RowLayout {
                Layout.fillWidth: true
                AppComboBox {
                    objectName: "broadcastModeSelector"; Layout.fillWidth: true
                    model: ["Include checked", "Exclude checked"]
                    currentIndex: workspace.broadcastMode === "include" ? 0 : 1
                    onActivated: workspace.setBroadcastMode(currentIndex === 0 ? "include" : "exclude")
                }
                AppButton { objectName: "broadcastSelectAll"; text: "Target all"; compact: true; onClicked: workspace.selectBroadcastTargets(true) }
                AppButton { objectName: "broadcastSelectNone"; text: "Target none"; compact: true; onClicked: workspace.selectBroadcastTargets(false) }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: workspace.broadcastRows; spacing: 3; ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property var modelData
                    objectName: "broadcastTarget:" + modelData.id
                    width: ListView.view.width; height: 44
                    text: modelData.name + " · " + (modelData.remote ? modelData.running ? modelData.host : "disconnected" : "local — excluded")
                    checked: modelData.checked
                    enabled: modelData.remote && modelData.running && (workspace.broadcastScope === "all" || modelData.inView)
                    onToggled: workspace.setTargetChecked(modelData.id, checked)
                    indicator: Rectangle {
                        x: 6; y: (parent.height - height) / 2; width: 17; height: 17; radius: 3
                        color: parent.checked ? Theme.accentDim : Theme.canvas
                        border.color: parent.checked ? Theme.accent : Theme.border
                        Text { anchors.centerIn: parent; text: parent.parent.checked ? "✓" : ""; textFormat: Text.PlainText; color: Theme.accentBright; font.pixelSize: Theme.uiFontSize(13) }
                    }
                    contentItem: Text {
                        leftPadding: 30; textFormat: Text.PlainText; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        text: parent.text
                        color: parent.enabled ? Theme.text : Theme.textFaint; font.pixelSize: Theme.uiFontSize(11)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; textFormat: Text.PlainText; text: root.targetCount + " effective targets · switching mode clears selection"; color: Theme.amber; font.pixelSize: Theme.uiFontSize(10) }
                AppButton { objectName: "broadcastTargetsDone"; text: "Done"; emphasized: true; onClicked: targetPopup.close() }
            }
        }
    }
    Dialog {
        id: review; objectName: "broadcastReviewDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(700, parent.width - 40); height: Math.min(580, parent.height - 40)
        title: "Review multi SSH command"; modal: true; focus: true
        footer: Rectangle {
            implicitHeight: 54; color: Theme.canvas
            RowLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    id: cancelSend; objectName: "broadcastCancelButton"; text: "Cancel"
                    onClicked: review.reject()
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
                AppButton {
                    objectName: "broadcastSendButton"; text: "Send to " + sender.targets.length + " terminals"
                    emphasized: true; enabled: sender.pending
                    onClicked: review.accept()
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
            }
        }
        onOpened: cancelSend.forceActiveFocus()
        onAccepted: sender.commit()
        onRejected: sender.cancel()
        background: Rectangle { color: Theme.surface; border.color: Theme.amber; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; text: "This pastes the command and presses Enter in every target below. Verify the shell prompt in each terminal. Execution across servers is not atomic and cannot be rolled back here."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.amber; font.pixelSize: Theme.uiFontSize(12) }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 140; clip: true
                TextArea { objectName: "broadcastCommandPreview"; text: sender.command; textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap; color: Theme.text; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(12) }
            }
            Text { text: "EXACT TARGETS · " + sender.targets.length; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(11); font.bold: true }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: sender.targets; ScrollBar.vertical: ScrollBar {}
                delegate: Text { required property string modelData; width: ListView.view.width; height: implicitHeight + 12; text: modelData; textFormat: Text.PlainText; color: Theme.text; wrapMode: Text.WrapAnywhere; font.pixelSize: Theme.uiFontSize(11) }
            }
        }
    }
}
