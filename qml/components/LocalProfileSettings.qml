pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BitShell

ColumnLayout {
    id: root
    spacing: 12
    property string editingId: ""
    property string profileKind: "local"
    property string distribution: ""
    property bool detected: false
    property bool customized: false

    function indexOfId(id) {
        const profiles = LocalProfileStore.profiles
        for (let i = 0; i < profiles.length; ++i)
            if (profiles[i].id === id) return i
        return -1
    }
    function editProfile(id) {
        const profile = LocalProfileStore.profile(id)
        editingId = profile.id || ""
        profileKind = profile.kind || "local"
        distribution = profile.distribution || ""
        detected = Boolean(profile.detected)
        customized = Boolean(profile.customized)
        profileName.text = profile.name || ""
        profileProgram.text = profile.program || ""
        profileArguments.text = (profile.arguments || []).join("\n")
        profileDirectory.text = profile.workingDirectory || ""
    }
    Component.onCompleted: editProfile(LocalProfileStore.defaultProfileId)

    Text { text: "LOCAL PROFILES"; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
    Text {
        Layout.fillWidth: true
        text: "Choose what + opens. Use the arrow beside it to launch another shell or command. Changes apply to new terminals."
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(11)
    }
    Text { text: "Default profile"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    AppComboBox {
        objectName: "defaultLocalProfileCombo"
        Layout.fillWidth: true
        model: LocalProfileStore.profiles
        textRole: "name"; valueRole: "id"
        currentIndex: root.indexOfId(LocalProfileStore.defaultProfileId)
        onActivated: LocalProfileStore.setDefaultProfile(currentValue)
    }
    Text {
        Layout.fillWidth: true
        text: LocalProfileStore.profiles.map(function(profile) { return profile.name }).join("  ·  ")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.pixelSize: Theme.uiFontSize(10)
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            objectName: "refreshLocalProfiles"
            text: LocalProfileStore.discovering ? "Finding shells…" : "Refresh detected shells"
            enabled: !LocalProfileStore.discovering
            onClicked: LocalProfileStore.refresh()
        }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        visible: LocalProfileStore.discoveryMessage.length > 0
        text: LocalProfileStore.discoveryMessage
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10)
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
    Text { text: "Edit a profile"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    RowLayout {
        Layout.fillWidth: true
        AppComboBox {
            objectName: "editLocalProfileCombo"
            Layout.fillWidth: true
            model: LocalProfileStore.profiles
            textRole: "name"; valueRole: "id"
            currentIndex: root.indexOfId(root.editingId)
            onActivated: root.editProfile(currentValue)
        }
        AppButton { objectName: "addLocalProfile"; text: "Add custom"; onClicked: root.editProfile("") }
    }
    Text { text: "Name"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    AppTextField { id: profileName; objectName: "localProfileName"; Layout.fillWidth: true; maximumLength: 128; placeholderText: "e.g. SQL*Plus or PowerShell · Projects" }
    Text { text: root.profileKind === "wsl" ? "WSL distribution · " + root.distribution : "Executable"; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    RowLayout {
        Layout.fillWidth: true
        AppTextField {
            id: profileProgram; objectName: "localProfileProgram"; Layout.fillWidth: true
            readOnly: root.profileKind === "wsl"
            placeholderText: Qt.platform.os === "windows" ? "C:\\path\\to\\sqlplus.exe or sqlplus.exe" : "/usr/bin/bash"
        }
        AppButton { text: "Browse…"; visible: root.profileKind !== "wsl"; onClicked: executablePicker.open() }
    }
    Text { text: "Arguments · one per line"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    ScrollView {
        Layout.fillWidth: true; Layout.preferredHeight: 90; clip: true
        TextArea {
            id: profileArguments; objectName: "localProfileArguments"
            placeholderText: root.profileKind === "wsl" ? "Optional additional wsl.exe arguments" : "-NoLogo\n-NoProfile"
            textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
            color: Theme.text; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(11)
            selectByMouse: true
            background: Rectangle { color: Theme.canvas; border.color: profileArguments.activeFocus ? Theme.accent : Theme.border; radius: Theme.radiusSmall }
        }
    }
    Text {
        Layout.fillWidth: true
        text: "Each line is passed literally. A path containing spaces stays on one line, without surrounding quotes. For SQL*Plus you can use /nolog; enter credentials in the terminal."
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textFaint; font.pixelSize: Theme.uiFontSize(10)
    }
    Text { text: root.profileKind === "wsl" ? "Starting directory inside WSL" : "Starting directory"; color: Theme.text; font.pixelSize: Theme.uiFontSize(11) }
    RowLayout {
        Layout.fillWidth: true
        AppTextField {
            id: profileDirectory; objectName: "localProfileDirectory"; Layout.fillWidth: true
            placeholderText: root.profileKind === "wsl" ? "~ or /home/username/projects" : "Blank: inherit · ~: home · or an absolute folder path"
        }
        AppButton { text: "Browse…"; visible: root.profileKind !== "wsl"; onClicked: directoryPicker.open() }
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            objectName: "saveLocalProfile"; text: "Save profile"; emphasized: true
            onClicked: {
                const id = LocalProfileStore.saveProfile({
                    id: root.editingId, name: profileName.text, kind: root.profileKind,
                    distribution: root.distribution, program: profileProgram.text,
                    arguments: profileArguments.text.length ? profileArguments.text.replace(/\r/g, "").split("\n") : [],
                    workingDirectory: profileDirectory.text
                })
                if (id) root.editProfile(id)
            }
        }
        AppButton {
            text: "Duplicate"; enabled: root.editingId.length > 0
            onClicked: { root.editingId = ""; root.detected = false; root.customized = false; profileName.text += " copy" }
        }
        AppButton {
            objectName: "removeLocalProfile"; text: root.detected ? "Reset" : "Remove"; enabled: root.customized
            onClicked: if (LocalProfileStore.removeProfile(root.editingId)) root.editProfile(LocalProfileStore.defaultProfileId)
        }
    }
    Text {
        objectName: "localProfileError"
        Layout.fillWidth: true; visible: LocalProfileStore.lastError.length > 0
        text: LocalProfileStore.lastError; textFormat: Text.PlainText
        wrapMode: Text.Wrap; color: Theme.red; font.pixelSize: Theme.uiFontSize(11)
    }
    AppButton { text: "Reload profiles from disk"; onClicked: if (LocalProfileStore.reload()) root.editProfile(LocalProfileStore.defaultProfileId) }
    Text {
        Layout.fillWidth: true
        text: "Local profiles run with your current user permissions. Administrator terminals are not supported in this window."
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textFaint; font.pixelSize: Theme.uiFontSize(10)
    }
    FileDialog {
        id: executablePicker; title: "Choose a local executable"; fileMode: FileDialog.OpenFile
        onAccepted: profileProgram.text = LocalProfileStore.localPath(selectedFile.toString())
    }
    FolderDialog {
        id: directoryPicker; title: "Choose the starting directory"
        onAccepted: profileDirectory.text = LocalProfileStore.localPath(selectedFolder.toString())
    }
}
