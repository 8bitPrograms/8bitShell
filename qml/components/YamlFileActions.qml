import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs as FileDialogs
import BitShell

ColumnLayout {
    id: root
    required property var storage
    required property string label
    property bool macroGroups: false
    property bool settings: false
    property string groupId: ""
    property string exportGroupId: ""
    property string pendingSource: ""
    property string status: ""
    property var prepareExport: null
    property bool failed: false
    signal imported()
    signal reloaded()
    spacing: 6

    function importFrom(path) {
        let ok
        if (macroGroups) ok = storage.importYaml(path, includeFolders.checked) >= 0
        else if (settings) ok = storage.importYaml(path)
        else ok = storage.importYaml(path) >= 0
        failed = !ok
        status = ok ? (settings ? "Settings imported and applied." : storage.lastImportSummary) : storage.lastError
        if (ok) imported()
        return ok
    }
    function exportTo(path) {
        const ok = macroGroups ? storage.exportYaml(path, exportGroupId) : storage.exportYaml(path)
        failed = !ok
        status = ok ? "Exported YAML · " + path : storage.lastError
        return ok
    }
    function reloadFromDisk() {
        const ok = storage.reload()
        failed = !ok
        status = ok ? label + " reloaded from YAML." : storage.lastError
        if (ok) reloaded()
        return ok
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6
        AppButton {
            text: "Import YAML…"; compact: true
            objectName: root.objectName + "Import"
            onClicked: importDialog.open()
        }
        AppButton {
            text: root.macroGroups ? "Export all…" : "Export YAML…"; compact: true
            objectName: root.objectName + "Export"
            onClicked: {
                if (root.prepareExport && !root.prepareExport()) return
                root.exportGroupId = ""; exportDialog.open()
            }
        }
        AppButton {
            visible: root.macroGroups
            enabled: root.groupId.length > 0
            text: "Export group…"; compact: true
            objectName: root.objectName + "ExportGroup"
            onClicked: {
                if (root.prepareExport && !root.prepareExport()) return
                root.exportGroupId = root.groupId; exportDialog.open()
            }
        }
        AppButton {
            text: "Reload YAML"; compact: true
            objectName: root.objectName + "Reload"
            onClicked: root.macroGroups ? confirmReload.open() : root.reloadFromDisk()
            ToolTip.visible: hovered
            ToolTip.text: "Read changes made in your text editor"
        }
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: root.storage.storagePath
        color: Theme.textFaint; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(9)
        elide: Text.ElideMiddle
    }
    Text { textFormat: Text.PlainText;
        Layout.fillWidth: true
        text: root.storage.lastError || root.status
        visible: text.length > 0
        color: root.storage.lastError || root.failed ? Theme.red : Theme.accent
        font.pixelSize: Theme.uiFontSize(11); wrapMode: Text.Wrap
    }
    FileDialogs.FileDialog {
        id: importDialog
        objectName: root.objectName + "ImportDialog"
        title: "Import " + root.label.toLowerCase() + " from YAML"
        fileMode: FileDialogs.FileDialog.OpenFile
        nameFilters: ["YAML files (*.yaml *.yml)", "All files (*)"]
        onAccepted: { root.pendingSource = selectedFile.toString(); confirmImport.open() }
    }
    FileDialogs.FileDialog {
        id: exportDialog
        objectName: root.objectName + "ExportDialog"
        title: root.exportGroupId ? "Export selected macro group" : "Export " + root.label.toLowerCase()
        fileMode: FileDialogs.FileDialog.SaveFile
        defaultSuffix: "yaml"
        nameFilters: ["YAML files (*.yaml *.yml)"]
        onAccepted: root.exportTo(selectedFile.toString())
    }
    Dialog {
        id: confirmReload
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "Reload macro YAML?"
        modal: true
        width: Math.min(parent.width - 40, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.radiusLarge; border.color: Theme.border }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; text: "Unsaved edits in the group editor will be discarded. Saved groups will be reloaded from disk."; color: Theme.text; wrapMode: Text.Wrap }
        }
        onAccepted: root.reloadFromDisk()
    }
    Dialog {
        id: confirmImport
        objectName: root.objectName + "ConfirmImport"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 460)
        modal: true
        title: "Import " + root.label.toLowerCase() + "?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.radiusLarge; border.color: Theme.border }
        contentItem: ColumnLayout {
            Text { textFormat: Text.PlainText;
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text; font.pixelSize: Theme.uiFontSize(12)
                text: root.settings
                      ? "Fields in the file will replace matching settings and take effect immediately. Logging settings can start recording, change output paths, or replace existing log files. Import settings only from a trusted source. Unspecified settings stay unchanged."
                      : root.macroGroups
                        ? "New groups will be added. Different groups with an existing id become separate copies. Imported macros never run automatically; review their text before using them."
                        : "New sessions and folders will be added. Existing session ids will be skipped, not overwritten."
            }
            CheckBox {
                id: includeFolders
                visible: root.macroGroups
                checked: false
                text: "Also import folder assignments"
                contentItem: Text { textFormat: Text.PlainText; text: includeFolders.text; color: Theme.text; leftPadding: includeFolders.indicator.width + 6; verticalAlignment: Text.AlignVCenter }
            }
        }
        onAccepted: root.importFrom(root.pendingSource)
    }
}
