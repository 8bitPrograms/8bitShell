pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root
    // Keep the custom themed surface inside the application on every desktop.
    popupType: Popup.Item
    objectName: "macroManager"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 850)
    height: Math.min(parent.height - 40, 790)
    modal: true
    padding: 16
    title: "Macro groups"
    property string groupId: ""
    property string folderPath: ""
    property string errorText: ""
    property int buttonIndex: -1
    ListModel { id: buttons }
    function loadGroup(id) {
        const group = MacroStore.group(id)
        groupId = id
        groupName.text = group.name || ""
        buttons.clear()
        const entries = group.buttons || []
        for (let i = 0; i < entries.length; ++i)
            buttons.append({label: entries[i].label, text: entries[i].text, enter: entries[i].enter === true})
        buttonIndex = -1
        buttonLabel.text = ""
        buttonText.text = ""
        sendEnter.checked = false
        errorText = ""
    }
    function openForFolder(path) {
        folderPath = path || ""
        loadGroup(MacroStore.assignedGroup(folderPath))
        open()
    }
    function editButton(index) {
        buttonIndex = index
        const button = buttons.get(index)
        buttonLabel.text = button.label
        buttonText.text = button.text
        sendEnter.checked = button.enter
    }
    function applyButton() {
        if (!buttonLabel.text.trim() || !buttonText.text) {
            errorText = "Give the button a label and some text."
            return false
        }
        const value = { label: buttonLabel.text.trim(), text: buttonText.text, enter: sendEnter.checked }
        if (buttonIndex < 0) { buttons.append(value); buttonIndex = buttons.count - 1 }
        else buttons.set(buttonIndex, value)
        errorText = ""
        return true
    }
    function save() {
        if ((buttonLabel.text.length || buttonText.text.length) && !applyButton()) return false
        const values = []
        for (let i = 0; i < buttons.count; ++i) {
            const b = buttons.get(i)
            values.push({label: b.label, text: b.text, enter: b.enter})
        }
        const id = MacroStore.saveGroup(groupId, groupName.text, values)
        if (id) { groupId = id; errorText = "Group saved." }
        else errorText = MacroStore.lastError
        return id.length > 0
    }
    background: Rectangle { color: Theme.surface; radius: Theme.radiusLarge; border.color: Theme.border }
    contentItem: ColumnLayout {
        spacing: 8
        YamlFileActions {
            objectName: "macrosYamlActions"
            Layout.fillWidth: true
            storage: MacroStore; label: "Macro groups"; macroGroups: true; groupId: root.groupId
            prepareExport: function() {
                return (root.groupId || groupName.text || buttonLabel.text || buttonText.text) ? root.save() : true
            }
            onReloaded: root.loadGroup(root.groupId)
        }
        RowLayout {
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; text: "FOLDER"; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10) }
            AppComboBox {
                id: folderChoice
                objectName: "macroFolderCombo"
                Layout.fillWidth: true
                model: SessionStore.revision >= 0 ? ["(All sessions / root)"].concat(SessionStore.folderPaths()) : []
                currentIndex: Math.max(0, model.indexOf(root.folderPath || "(All sessions / root)"))
                onActivated: root.folderPath = currentIndex === 0 ? "" : currentText
            }
            AppComboBox {
                id: assignment
                objectName: "macroAssignmentCombo"
                Layout.preferredWidth: 210
                textRole: "name"; valueRole: "id"
                model: [{id: "", name: "Inherit from parent"}].concat(MacroStore.groups)
                currentIndex: MacroStore.revision >= 0 ? Theme.themeIndex(MacroStore.assignedGroup(root.folderPath), model) : 0
                onActivated: MacroStore.assignFolder(root.folderPath, currentValue)
            }
        }
        Text { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: "The nearest non-empty group wins. Empty or unassigned folders inherit from their parent."
            color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10); wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 14
            ColumnLayout {
                Layout.preferredWidth: 185; Layout.maximumWidth: 210; Layout.fillHeight: true
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; model: MacroStore.groups
                    delegate: ItemDelegate {
                        id: groupEntry
                        required property var modelData
                        width: ListView.view.width
                        text: modelData.name
                        highlighted: modelData.id === root.groupId
                        onClicked: root.loadGroup(modelData.id)
                        background: Rectangle { radius: 5; color: groupEntry.highlighted ? Theme.surfaceSelected : groupEntry.hovered ? Theme.surfaceHover : "transparent" }
                        contentItem: Text { textFormat: Text.PlainText; text: groupEntry.text; color: Theme.text; font.pixelSize: Theme.uiFontSize(11); verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
                AppButton { text: "New group"; iconText: "+"; onClicked: root.loadGroup("") }
                AppButton { text: "Delete group"; enabled: root.groupId.length > 0; onClicked: deleteGroup.open() }
            }
            ScrollView {
                id: editorScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: editorScroll.availableWidth
                height: Math.max(410, editorScroll.availableHeight)
                spacing: 8
                AppTextField { id: groupName; objectName: "macroGroupName"; Layout.fillWidth: true; placeholderText: "Group name, e.g. Oracle" }
                ListView {
                    id: buttonList
                    Layout.fillWidth: true; Layout.preferredHeight: 132
                    clip: true; model: buttons
                    delegate: ItemDelegate {
                        id: buttonEntry
                        required property int index
                        required property string label
                        required property bool enter
                        width: ListView.view.width
                        height: 32
                        text: label + (enter ? "  ↵" : "")
                        highlighted: index === root.buttonIndex
                        onClicked: root.editButton(index)
                        background: Rectangle { radius: 5; color: buttonEntry.highlighted ? Theme.surfaceSelected : buttonEntry.hovered ? Theme.surfaceHover : "transparent" }
                        contentItem: Text { textFormat: Text.PlainText; text: buttonEntry.text; color: Theme.text; font.pixelSize: Theme.uiFontSize(11); verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
                RowLayout {
                    AppButton { text: "Add button"; compact: true; onClicked: { root.buttonIndex = -1; buttonLabel.text = ""; buttonText.text = ""; sendEnter.checked = false } }
                    AppButton { text: "↑"; compact: true; enabled: root.buttonIndex > 0; onClicked: { buttons.move(root.buttonIndex, root.buttonIndex - 1, 1); --root.buttonIndex } }
                    AppButton { text: "↓"; compact: true; enabled: root.buttonIndex >= 0 && root.buttonIndex < buttons.count - 1; onClicked: { buttons.move(root.buttonIndex, root.buttonIndex + 1, 1); ++root.buttonIndex } }
                    AppButton { text: "Remove"; compact: true; enabled: root.buttonIndex >= 0; onClicked: { buttons.remove(root.buttonIndex); root.buttonIndex = -1; buttonLabel.text = ""; buttonText.text = "" } }
                }
                AppTextField { id: buttonLabel; objectName: "macroButtonLabel"; Layout.fillWidth: true; placeholderText: "Button label" }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 90
                    TextArea {
                        textFormat: TextEdit.PlainText
                        id: buttonText; objectName: "macroButtonText"
                        placeholderText: "Text to paste into the active terminal"
                        font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(12); wrapMode: TextEdit.Wrap
                    }
                }
                RowLayout {
                    CheckBox { id: sendEnter; text: "Send Enter after pasting"; checked: false }
                    Item { Layout.fillWidth: true }
                    AppButton { text: "Apply button"; objectName: "applyMacroButton"; compact: true; onClicked: root.applyButton() }
                }
            }
            }
        }
        Text { textFormat: Text.PlainText; text: root.errorText || MacroStore.lastError; visible: text.length > 0; color: Theme.accent; font.pixelSize: Theme.uiFontSize(11); Layout.fillWidth: true; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; text: "Export saves the edited group first. Macros are plain text; keep passwords out."; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            AppButton { text: "Close"; onClicked: root.close() }
            AppButton { text: "Save group"; objectName: "saveMacroGroup"; onClicked: root.save() }
            AppButton { text: "Save & assign"; objectName: "saveAssignMacroGroup"; emphasized: true; onClicked: { if (root.save()) MacroStore.assignFolder(root.folderPath, root.groupId) } }
        }
    }
    Dialog {
        id: deleteGroup
        anchors.centerIn: parent
        title: "Delete this macro group?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: "Folder assignments will inherit from their parents."; color: Theme.text }
        onAccepted: { MacroStore.removeGroup(root.groupId); root.loadGroup("") }
    }
}
