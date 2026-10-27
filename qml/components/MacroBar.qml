pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    objectName: "macroBar"
    property var terminal: null
    property string folderPath: ""
    property string overrideGroup: ""
    signal manageRequested(string folderPath)
    readonly property var resolved: {
        return MacroStore.revision >= 0
            ? (overrideGroup ? MacroStore.group(overrideGroup) : MacroStore.effectiveGroup(folderPath)) : ({})
    }
    implicitHeight: 28
    color: Theme.surfaceRaised
    border.color: Theme.borderSoft
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; anchors.topMargin: 2; anchors.bottomMargin: 2; spacing: 4
        AppComboBox {
            objectName: "macroBarGroup"
            Layout.preferredWidth: Math.min(170, root.width * 0.28)
            implicitHeight: 22
            model: [{id:"", name: "Auto · " + (root.resolved.name || "no group")}].concat(MacroStore.groups)
            textRole: "name"; valueRole: "id"
            onActivated: root.overrideGroup = currentValue
            currentIndex: Theme.themeIndex(root.overrideGroup, model)
        }
        Flickable {
            id: buttonStrip
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: macroButtons.implicitWidth; contentHeight: height
            clip: true; flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: macroButtons
                spacing: 5
                Repeater {
                    model: root.resolved.buttons || []
                    delegate: AppButton {
                        required property var modelData
                        objectName: "macroAction:" + modelData.label
                        text: modelData.label + (modelData.enter ? " ↵" : "")
                        compact: true
                        implicitHeight: 22
                        enabled: root.terminal && root.terminal.running
                        onClicked: root.terminal.sendMacro(modelData.text, Boolean(modelData.enter))
                        ToolTip.visible: hovered
                        ToolTip.text: Theme.tooltip(modelData.text + (modelData.enter ? "\nSends Enter" : "\nPaste only"))
                    }
                }
            }
            ScrollBar.horizontal: ScrollBar {
                policy: ScrollBar.AsNeeded
                visible: buttonStrip.contentWidth > buttonStrip.width
                height: 6
            }
        }
        AppButton { text: "Edit"; compact: true; implicitHeight: 22; onClicked: root.manageRequested(root.folderPath) }
    }
}
