import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

ItemDelegate {
    id: control
    objectName: "sessionTreeRow:" + (folderNode ? "folder:" : "session:")
                + nodePath + ":" + nodeName

    required property var modelData

    property bool catalogDragActive: false
    property string catalogDragKind: ""
    property string catalogDragPath: ""
    property int catalogDragIndex: -1
    property bool dragInProgress: false
    property bool selectionMode: false
    property bool nodeSelected: false
    signal selectionToggleRequested(var node)
    signal deleteSelectionRequested()

    readonly property bool folderNode: modelData.kind === "folder"
    readonly property int nodeDepth: Number(modelData.depth || 0)
    readonly property string nodeName: String(modelData.name || "")
    readonly property string nodePath: String(modelData.path || modelData.group || "")
    readonly property int sourceIndex: Number(modelData.sourceIndex ?? -1)
    readonly property bool menuOpen: folderMenu.visible || sessionMenu.visible
    readonly property bool acceptsDrop: {
        if (!folderNode || !catalogDragActive)
            return false
        if (catalogDragKind === "session")
            return catalogDragIndex >= 0 && catalogDragPath !== nodePath
        if (catalogDragKind !== "folder" || catalogDragPath.length === 0)
            return false
        const source = catalogDragPath.toLocaleLowerCase()
        const destination = nodePath.toLocaleLowerCase()
        return destination !== source && !destination.startsWith(source + "/")
    }

    signal folderToggleRequested(string path)
    signal folderMacrosRequested(string path)
    signal folderConnectionRequested(string path)
    signal folderAddSessionRequested(string path)
    signal folderAddRequested(string path)
    signal folderRenameRequested(string path, string name)
    signal folderRemoveRequested(string path, string name, int totalCount)
    signal sessionOpenRequested(int sourceIndex)
    signal sessionEditRequested(int sourceIndex)
    signal sessionDuplicateRequested(int sourceIndex)
    signal sessionRemoveRequested(int sourceIndex, string name)
    signal nodeMoveRequested(string kind, string path, int sourceIndex,
                             string destinationPath)
    signal dragActivityChanged(bool active, string kind, string path,
                               int sourceIndex, string name)
    signal dragPositionChanged(real viewportY)
    signal inspectRequested(var node)

    property bool inspected: false

    function openContextMenu() {
        if (folderNode)
            folderMenu.openBelow(moreButton, true)
        else
            sessionMenu.openBelow(moreButton, true)
    }

    function openContextMenuAt(position) {
        if (folderNode)
            folderMenu.popup(control, position.x, position.y)
        else
            sessionMenu.popup(control, position.x, position.y)
    }

    function triggerMenuButton() {
        moreButton.click()
    }

    function updateDragPosition() {
        if (!dragInProgress || !Overlay.overlay)
            return
        const overlayPoint = control.mapToItem(Overlay.overlay,
                                               dragHandler.centroid.position.x,
                                               dragHandler.centroid.position.y)
        dragAnchor.x = overlayPoint.x
        dragAnchor.y = overlayPoint.y
        const view = control.ListView.view
        if (view) {
            const viewPoint = view.mapFromItem(Overlay.overlay,
                                               overlayPoint.x,
                                               overlayPoint.y)
            dragPositionChanged(viewPoint.y)
        }
    }

    width: ListView.view ? ListView.view.width : 260
    implicitHeight: 22
    opacity: dragInProgress ? 0.46 : 1
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 4
    rightPadding: 2
    topPadding: 0
    bottomPadding: 0

    onHoveredChanged: if (hovered) inspectRequested(modelData)
    onClicked: {
        inspectRequested(modelData)
        if (selectionMode)
            selectionToggleRequested(modelData)
        else if (folderNode)
            folderToggleRequested(nodePath)
        else
            sessionOpenRequested(sourceIndex)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Delete) {
            if (selectionMode) deleteSelectionRequested()
            else if (folderNode) folderRemoveRequested(nodePath, nodeName, Number(modelData.totalCount || 0))
            else sessionRemoveRequested(sourceIndex, nodeName)
            event.accepted = true
        } else if (event.key === Qt.Key_F2) {
            if (folderNode)
                folderRenameRequested(nodePath, nodeName)
            else
                sessionEditRequested(sourceIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Menu) {
            openContextMenu()
            event.accepted = true
        }
    }

    background: Rectangle {
        radius: 2
        color: folderDrop.containsDrag
               ? (control.acceptsDrop ? Theme.accentDim
                                      : Qt.rgba(0.95, 0.27, 0.34, 0.13))
               : control.nodeSelected || control.inspected || control.pressed ? Theme.surfaceSelected
               : control.hovered || control.activeFocus ? Theme.surfaceHover
                                                         : "transparent"
        border.color: folderDrop.containsDrag
                      ? (control.acceptsDrop ? Theme.accent : Theme.red)
                      : control.activeFocus ? Theme.accent : "transparent"
        Behavior on color { ColorAnimation { duration: 75 } }
    }

    contentItem: RowLayout {
        spacing: 4

        CheckBox {
            id: nodeCheckbox
            objectName: "catalogNodeCheckbox"
            visible: control.selectionMode
            checked: control.nodeSelected
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            padding: 0
            indicator: Rectangle {
                width: 13
                height: 13
                x: 1
                y: (nodeCheckbox.height - height) / 2
                radius: 3
                color: control.nodeSelected ? Theme.accent : Theme.canvas
                border.color: nodeCheckbox.activeFocus || control.nodeSelected ? Theme.accent : Theme.border
                border.width: nodeCheckbox.activeFocus ? 2 : 1
                Text {
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: control.nodeSelected ? "✓" : ""
                    color: Theme.canvas
                    font.pixelSize: Theme.uiFontSize(12)
                }
            }
            contentItem: Item {}
            onClicked: control.selectionToggleRequested(control.modelData)
            Accessible.name: "Select " + control.nodeName
        }

        Item { Layout.preferredWidth: control.nodeDepth * 10 }

        Item {
            Layout.preferredWidth: 9
            Layout.preferredHeight: 14

            Text { textFormat: Text.PlainText;
                visible: control.folderNode
                anchors.centerIn: parent
                text: "›"
                rotation: control.modelData.expanded ? 90 : 0
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(12)
                Behavior on rotation { NumberAnimation { duration: 90 } }
            }
            TapHandler {
                enabled: control.selectionMode && control.folderNode
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.WithinBounds
                onTapped: control.folderToggleRequested(control.nodePath)
            }
        }

        Item {
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13

            Item {
                visible: control.folderNode
                anchors.fill: parent

                Rectangle {
                    x: 1
                    y: 1
                    width: 6
                    height: 3
                    radius: 1
                    color: control.hovered ? Theme.accentBright : Theme.accent
                    opacity: 0.82
                }
                Rectangle {
                    x: 0
                    y: 3
                    width: 12
                    height: 8
                    radius: 1
                    color: control.hovered ? Theme.accent : Theme.accentDim
                    border.color: Theme.accent
                }
            }

            Rectangle {
                visible: !control.folderNode
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: 2
                color: Theme.accentDim
                border.color: Qt.rgba(0.27, 0.76, 0.69, 0.45)

                Text { textFormat: Text.PlainText;
                    anchors.centerIn: parent
                    text: ">"
                    color: Theme.accentBright
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(9)
                    font.bold: true
                }
            }
        }

        Text { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: control.nodeName
            color: Theme.text
            elide: Text.ElideMiddle
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize(10)
            font.weight: control.folderNode ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            visible: control.folderNode
            Layout.preferredWidth: Math.max(16, countText.implicitWidth + 8)
            Layout.preferredHeight: 14
            radius: 7
            color: control.modelData.expanded ? Theme.accentDim : Theme.surfaceRaised

            Text { textFormat: Text.PlainText;
                id: countText
                anchors.centerIn: parent
                text: String(control.modelData.totalCount || 0)
                color: control.modelData.expanded ? Theme.accentBright : Theme.textFaint
                font.family: Theme.monoFont
                font.pixelSize: Theme.uiFontSize(8)
            }
        }

        AbstractButton {
            id: moreButton
            objectName: "treeMenuButton"
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            opacity: control.hovered || control.activeFocus || hovered || control.menuOpen
                     ? 1 : 0

            background: Rectangle {
                radius: 5
                color: moreButton.down ? Theme.surfaceSelected
                                       : moreButton.hovered ? Theme.surfaceRaised
                                                            : "transparent"
                border.color: moreButton.activeFocus ? Theme.accent : "transparent"
            }

            contentItem: Text { textFormat: Text.PlainText;
                text: "⋮"
                color: Theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(12)
            }

            onClicked: control.openContextMenu()

            ToolTip.visible: hovered && !control.menuOpen
            ToolTip.delay: 500
            ToolTip.text: control.folderNode ? "Folder actions" : "Session actions"
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(eventPoint) {
            control.openContextMenuAt(eventPoint.position)
        }
    }

    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        enabled: !control.selectionMode && !moreButton.down && !control.menuOpen

        onActiveChanged: {
            if (active) {
                folderMenu.close()
                sessionMenu.close()
                control.dragInProgress = true
                control.updateDragPosition()
                control.dragActivityChanged(true,
                                            control.folderNode ? "folder" : "session",
                                            control.nodePath,
                                            control.sourceIndex,
                                            control.nodeName)
            } else {
                // Drag.drop() is what turns the final DragMove into a real
                // DropArea.dropped event. Merely making Drag.active false
                // cancels the drag instead.
                dragAnchor.Drag.drop()
                control.dragInProgress = false
                dragAnchor.x = 0
                dragAnchor.y = 0
                control.dragActivityChanged(false, "", "", -1, "")
            }
        }
        onCentroidChanged: control.updateDragPosition()
    }

    Item {
        id: dragAnchor
        parent: Overlay.overlay
        width: 1
        height: 1
        z: 100

        Drag.active: control.dragInProgress
        Drag.source: control
        Drag.keys: ["bitshell/catalog-node"]
        Drag.supportedActions: Qt.MoveAction
        Drag.hotSpot.x: 0
        Drag.hotSpot.y: 0
    }

    Rectangle {
        parent: Overlay.overlay
        visible: control.dragInProgress
        x: dragAnchor.x + 10
        y: dragAnchor.y + 8
        z: 101
        width: Math.min(190, control.width - 18)
        height: 22
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.color: Theme.accent
        opacity: 0.96

        Row {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 9
            spacing: 7

            Item {
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter

                Item {
                    visible: control.folderNode
                    anchors.fill: parent
                    Rectangle {
                        x: 2
                        y: 3
                        width: 8
                        height: 5
                        radius: 2
                        color: Theme.accentBright
                    }
                    Rectangle {
                        x: 1
                        y: 6
                        width: 14
                        height: 9
                        radius: 2
                        color: Theme.accentDim
                        border.color: Theme.accentBright
                    }
                }

                Rectangle {
                    visible: !control.folderNode
                    anchors.fill: parent
                    radius: 4
                    color: Theme.accentDim
                    border.color: Theme.accentBright
                    Text { textFormat: Text.PlainText;
                        anchors.centerIn: parent
                        text: ">"
                        color: Theme.accentBright
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.uiFontSize(8)
                        font.bold: true
                    }
                }
            }
            Text { textFormat: Text.PlainText;
                width: parent.width - 26
                anchors.verticalCenter: parent.verticalCenter
                text: control.nodeName
                color: Theme.text
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(10)
                font.weight: Font.DemiBold
            }
        }
    }

    DropArea {
        id: folderDrop
        objectName: "folderDropArea"
        anchors.fill: parent
        enabled: control.folderNode && control.catalogDragActive
        keys: ["bitshell/catalog-node"]

        onEntered: function(drag) {
            if (!control.acceptsDrop) {
                drag.accepted = false
            }
        }
        onDropped: function(drop) {
            if (!control.acceptsDrop) {
                drop.accepted = false
                return
            }
            drop.acceptProposedAction()
            control.nodeMoveRequested(control.catalogDragKind,
                                      control.catalogDragPath,
                                      control.catalogDragIndex,
                                      control.nodePath)
        }
    }

    ToolTip.visible: control.hovered && !moreButton.hovered && !control.menuOpen
                         && !control.dragInProgress
    ToolTip.delay: 900
    ToolTip.text: Theme.tooltip(control.folderNode
                  ? control.nodePath
                  : (control.nodePath.length > 0 ? control.nodePath + "\n" : "")
                    + String(control.modelData.user || "")
                    + (String(control.modelData.user || "").length > 0 ? "@" : "")
                    + String(control.modelData.host || ""))

    AppMenu {
        id: folderMenu
        objectName: "folderContextMenu"

        AppMenuItem {
            text: "Assign macro group…"
            onTriggered: control.folderMacrosRequested(control.nodePath)
        }
        AppMenuItem {
            objectName: "folderConnectionMenuItem"
            text: "Connection rule…"
            visible: PersonalProfile.cyberArkEnabled
            onTriggered: control.folderConnectionRequested(control.nodePath)
        }

        AppMenuItem {
            objectName: "newSessionInFolderMenuItem"
            text: "New SSH session here"
            onTriggered: control.folderAddSessionRequested(control.nodePath)
        }
        AppMenuItem {
            text: "New subfolder"
            onTriggered: control.folderAddRequested(control.nodePath)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "renameFolderMenuItem"
            text: "Rename folder"
            onTriggered: control.folderRenameRequested(control.nodePath, control.nodeName)
        }
        AppMenuItem {
            text: "Move to root"
            enabled: control.nodePath.includes("/")
            onTriggered: control.nodeMoveRequested("folder", control.nodePath, -1, "")
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "deleteFolderMenuItem"
            text: "Delete folder and contents…"
            onTriggered: control.folderRemoveRequested(
                             control.nodePath,
                             control.nodeName,
                             Number(control.modelData.totalCount || 0))
        }
    }

    AppMenu {
        id: sessionMenu
        objectName: "sessionContextMenu"

        AppMenuItem {
            text: "Connect"
            onTriggered: control.sessionOpenRequested(control.sourceIndex)
        }
        AppMenuItem {
            objectName: "editSessionMenuItem"
            text: "Edit session"
            onTriggered: control.sessionEditRequested(control.sourceIndex)
        }
        AppMenuItem {
            text: "Duplicate"
            onTriggered: control.sessionDuplicateRequested(control.sourceIndex)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Move to root"
            enabled: control.nodePath.length > 0
            onTriggered: control.nodeMoveRequested("session", control.nodePath,
                                                   control.sourceIndex, "")
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "deleteSessionMenuItem"
            text: "Delete…"
            onTriggered: control.sessionRemoveRequested(control.sourceIndex,
                                                        control.nodeName)
        }
    }
}
