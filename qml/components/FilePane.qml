pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell
Rectangle {
    id: root
    required property var controller
    property bool remote: false
    property bool canCopy: true
    readonly property int selectedRow: files.currentIndex
    readonly property bool selectedDirectory: files.currentItem ? files.currentItem.isDirectory : false
    signal copyRequested(int row)
    signal chooseDestinationRequested(int row)
    signal chooseFilesRequested()
    signal disconnectRequested()
    color: Theme.surface
    border.color: Theme.borderSoft
    function receiveFiles(urls) {
        if (remote) controller.uploadFiles(urls)
        else controller.copyFiles(urls)
    }
    function openSelectedFolder() {
        if (selectedDirectory && !controller.busy) controller.openEntry(selectedRow)
    }
    // One click opens a folder. Swallow the second half of a double-click so
    // the replacement row at the same position cannot open another folder.
    property point lastFolderClick: Qt.point(-1, -1)
    Timer { id: folderClickGuard; interval: Qt.styleHints.mouseDoubleClickInterval }
    Connections {
        target: root.controller
        function onModelReset() { files.currentIndex = -1 }
        function onCurrentPathChanged() { pathField.text = root.controller.currentPath }
        function onChanged() { if (!pathField.activeFocus) pathField.text = root.controller.currentPath }
        ignoreUnknownSignals: true
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 7; spacing: 5
        RowLayout {
            Layout.fillWidth: true; spacing: 3
            AppButton { objectName: root.remote ? "remoteBack" : "localBack"; text: "←"; compact: true; implicitWidth: 29; enabled: root.controller.canGoBack && !root.controller.busy; onClicked: root.controller.goBack(); ToolTip.visible: hovered; ToolTip.text: "Back" }
            AppButton { objectName: root.remote ? "remoteForward" : "localForward"; text: "→"; compact: true; implicitWidth: 29; enabled: root.controller.canGoForward && !root.controller.busy; onClicked: root.controller.goForward(); ToolTip.visible: hovered; ToolTip.text: "Forward" }
            AppButton { objectName: root.remote ? "remoteUp" : "localUp"; text: "↑"; compact: true; implicitWidth: 29; enabled: !root.controller.busy; onClicked: root.controller.goUp(); ToolTip.visible: hovered; ToolTip.text: "Parent folder" }
            AppButton { objectName: root.remote ? "remoteOpenFolder" : "localOpenFolder"; text: "↓"; compact: true; implicitWidth: 29; enabled: root.selectedDirectory && !root.controller.busy; onClicked: root.openSelectedFolder(); ToolTip.visible: hovered; ToolTip.text: "Open selected folder · Enter" }
            AppButton { text: "Home"; compact: true; enabled: !root.controller.busy; onClicked: root.controller.goHome() }
            Text {
                visible: parent.width >= 315
                textFormat: Text.PlainText
                text: root.remote ? "REMOTE" : "THIS COMPUTER"
                color: Theme.accent; font.pixelSize: Theme.uiFontSize(8); font.bold: true
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: root.remote ? "copyRemoteButton" : "sendLocalButton"
                text: root.remote ? "↓ Copy" : "↑ Send"; compact: true
                enabled: root.remote
                         ? root.canCopy && root.selectedRow >= 0 && !root.controller.busy
                         : root.canCopy && !root.controller.busy
                onClicked: {
                    if (root.remote) root.copyRequested(root.selectedRow)
                    else if (root.selectedRow >= 0) root.copyRequested(root.selectedRow)
                    else root.chooseFilesRequested()
                }
                ToolTip.visible: hovered
                ToolTip.text: root.remote ? "Copy to the local folder shown below" : "Choose files to upload; select one row to send it directly"
            }
            AppButton {
                visible: root.remote; text: "To…"; compact: true
                enabled: root.selectedRow >= 0 && !root.controller.busy
                onClicked: root.chooseDestinationRequested(root.selectedRow)
            }
            AppButton { text: "↻"; compact: true; implicitWidth: 29; enabled: !root.controller.busy; onClicked: root.controller.refresh(); ToolTip.visible: hovered; ToolTip.text: "Refresh" }
            AppButton {
                objectName: "disconnectRemoteFilesButton"
                visible: root.remote
                text: "⏻"; compact: true; implicitWidth: 29
                enabled: !root.controller.busy
                onClicked: root.disconnectRequested()
                ToolTip.visible: hovered; ToolTip.text: "Disconnect remote files for this tab"
            }
        }
        AppTextField {
            id: pathField
            objectName: root.remote ? "remotePath" : "localPath"
            Layout.fillWidth: true
            implicitHeight: 30; font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(10)
            text: root.controller.currentPath
            color: Theme.text; selectByMouse: true
            enabled: !root.controller.busy
            onAccepted: root.controller.changeDirectory(text)
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ListView {
                id: files
                objectName: root.remote ? "remoteFileList" : "localFileList"
                anchors.fill: parent; clip: true
                model: root.controller
                currentIndex: -1
                activeFocusOnTab: true
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                Keys.onPressed: function(event) {
                    if (root.controller.busy) return
                    if (event.modifiers === Qt.NoModifier
                            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Right)) {
                        root.openSelectedFolder()
                    } else if ((event.modifiers === Qt.AltModifier && event.key === Qt.Key_Up)
                               || (event.modifiers === Qt.NoModifier && event.key === Qt.Key_Backspace)) {
                        root.controller.goUp()
                    } else if (event.modifiers === Qt.AltModifier && event.key === Qt.Key_Left) {
                        root.controller.goBack()
                    } else if (event.modifiers === Qt.AltModifier && event.key === Qt.Key_Right) {
                        root.controller.goForward()
                    } else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_F5) {
                        root.controller.refresh()
                    } else {
                        return
                    }
                    event.accepted = true
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string name
                    required property bool isDirectory
                    required property string sizeText
                    objectName: (root.remote ? "remoteFileRow:" : "localFileRow:") + name
                    width: files.width; height: 30; radius: 4
                    color: root.selectedRow === index ? Theme.surfaceSelected : pointer.containsMouse ? Theme.surfaceHover : "transparent"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 8; spacing: 7
                        Text { textFormat: Text.PlainText; text: row.isDirectory ? "▸" : "·"; color: row.isDirectory ? Theme.accent : Theme.textMuted; font.pixelSize: Theme.uiFontSize(15) }
                        Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: row.name; color: Theme.text; elide: Text.ElideMiddle; font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(10) }
                        Text { textFormat: Text.PlainText; text: row.sizeText; color: Theme.textFaint; font.pixelSize: Theme.uiFontSize(9) }
                    }
                    MouseArea {
                        id: pointer
                        objectName: "fileRowPointer"
                        anchors.fill: parent; hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property point down
                        property bool startedDrag: false
                        onPressed: function(mouse) {
                            if (root.controller.busy) { mouse.accepted = false; return }
                            files.currentIndex = row.index
                            files.forceActiveFocus(Qt.MouseFocusReason)
                            down = Qt.point(mouse.x, mouse.y); startedDrag = false
                            if (mouse.button === Qt.RightButton) fileMenu.popup(row, mouse.x, mouse.y)
                        }
                        onClicked: function(mouse) {
                            if (mouse.button !== Qt.LeftButton || mouse.modifiers !== Qt.NoModifier
                                    || startedDrag || !row.isDirectory || root.controller.busy) return
                            const position = pointer.mapToItem(root, mouse.x, mouse.y)
                            if (folderClickGuard.running
                                    && Math.abs(position.x - root.lastFolderClick.x) <= Qt.styleHints.startDragDistance
                                    && Math.abs(position.y - root.lastFolderClick.y) <= Qt.styleHints.startDragDistance) return
                            root.lastFolderClick = position
                            folderClickGuard.restart()
                            root.openSelectedFolder()
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || !(pressedButtons & Qt.LeftButton) || startedDrag || root.controller.busy) return
                            if (Math.abs(mouse.x - down.x) + Math.abs(mouse.y - down.y) > Qt.styleHints.startDragDistance) {
                                startedDrag = true
                                const sourceRow = row.index
                                Qt.callLater(function() { root.controller.startDrag(sourceRow) })
                            }
                        }
                    }
                    ToolTip.visible: pointer.containsMouse
                    ToolTip.delay: 1000
                    ToolTip.text: Theme.tooltip(row.name
                        + (row.isDirectory ? "\nClick to open · Ctrl+click to select for copying." : "")
                        + (root.remote ? "\nDrag to copy; remote files are prepared locally first." : "\nDrag to your desktop, a folder, or the remote browser."))
                }
            }
            DropArea {
                id: dropZone
                objectName: root.remote ? "remoteFileDropArea" : "localFileDropArea"
                anchors.fill: parent
                onEntered: function(drag) { drag.accepted = drag.hasUrls && !root.controller.busy }
                onDropped: function(drop) {
                    if (!drop.hasUrls || root.controller.busy) return
                    root.receiveFiles(drop.urls); drop.accept(Qt.CopyAction)
                }
            }
            Rectangle {
                visible: dropZone.containsDrag; anchors.fill: parent
                color: Theme.accentDim; opacity: 0.85; radius: 6
                border.color: Theme.accent; border.width: 2
                Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: root.remote ? "Upload into this folder" : "Copy into this folder"; color: Theme.text; font.pixelSize: Theme.uiFontSize(12) }
            }
            Text { textFormat: Text.PlainText; visible: root.controller.count === 0 && !root.controller.busy; anchors.centerIn: parent; text: "Empty folder · drop files here"; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10) }
        }
        ProgressBar { Layout.fillWidth: true; visible: root.controller.busy; indeterminate: true; implicitHeight: 3 }
        Text { textFormat: Text.PlainText;
            Layout.fillWidth: true; text: root.controller.statusText
            color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(9); elide: Text.ElideMiddle
            ToolTip.visible: statusHover.hovered; ToolTip.text: Theme.tooltip(text)
            HoverHandler { id: statusHover }
        }
    }
    AppMenu {
        id: fileMenu
        objectName: root.remote ? "remoteFileMenu" : "localFileMenu"
        AppMenuItem { text: "Open folder"; enabled: root.selectedDirectory && !root.controller.busy; onTriggered: root.openSelectedFolder() }
        AppMenuItem { text: root.remote ? "Copy to local folder" : "Upload to remote folder"; enabled: root.canCopy && root.selectedRow >= 0 && !root.controller.busy; onTriggered: root.copyRequested(root.selectedRow) }
        AppMenuItem { text: "Copy to…"; visible: root.remote; enabled: root.selectedRow >= 0 && !root.controller.busy; onTriggered: root.chooseDestinationRequested(root.selectedRow) }
        AppMenuSeparator {}
        AppMenuItem { text: "Refresh"; onTriggered: root.controller.refresh() }
    }
}
