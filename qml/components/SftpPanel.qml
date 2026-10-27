pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    objectName: "fileBrowser"
    color: Theme.surface
    border.color: Theme.borderSoft
    implicitWidth: 365

    property var profile: ({})
    property string sessionKey: ""
    readonly property bool routeAllowsSftp: !profile.psmp || !!profile.psmp.route.allowSftp
    readonly property string profileKey: JSON.stringify(profile || {})
    property var sessionStates: ({})
    property string appliedSessionKey: ""
    property string appliedProfileKey: ""
    property bool remoteWanted: false
    property bool pendingSessionChange: false
    property int downloadRow: -1

    function stateFor(key) {
        return sessionStates[key] || ({localPath: "", remotePath: "", remoteWanted: false})
    }
    function saveCurrentState() {
        if (!appliedSessionKey) return
        const next = Object.assign({}, sessionStates)
        const old = Object.assign({}, stateFor(appliedSessionKey))
        old.localPath = localFiles.currentPath
        old.remotePath = sftp.currentPath || old.remotePath || ""
        old.remoteWanted = remoteWanted
        next[appliedSessionKey] = old
        sessionStates = next
    }
    function applyActiveSession() {
        const key = sessionKey || "local-default"
        if (key === appliedSessionKey && profileKey === appliedProfileKey) {
            pendingSessionChange = false
            return
        }
        if (sftp.busy) { pendingSessionChange = true; return }
        pendingSessionChange = false
        saveCurrentState()
        sftp.disconnectFromHost()
        appliedSessionKey = key
        appliedProfileKey = profileKey
        const state = stateFor(key)
        remoteWanted = !!state.remoteWanted && !!profile.host
        if (state.localPath) localFiles.changeDirectory(state.localPath)
        else localFiles.goHome()
        if (visible && remoteWanted && root.routeAllowsSftp)
            sftp.connectToProfileAt(profile, state.remotePath || "")
    }
    function connectRemote() {
        if (!profile.host || !routeAllowsSftp || sftp.busy) return
        remoteWanted = true
        saveCurrentState()
        sftp.connectToProfileAt(profile, stateFor(appliedSessionKey).remotePath || "")
    }
    function disconnectRemote() {
        remoteWanted = false
        saveCurrentState()
        sftp.disconnectFromHost()
    }
    function forgetSession(key) {
        if (!key || !sessionStates[key]) return
        const next = Object.assign({}, sessionStates)
        delete next[key]
        sessionStates = next
    }

    onSessionKeyChanged: { pendingSessionChange = true; sessionSwitch.restart() }
    onProfileKeyChanged: { pendingSessionChange = true; sessionSwitch.restart() }
    onVisibleChanged: {
        if (visible) {
            sessionSwitch.restart()
            if (appliedSessionKey === (sessionKey || "local-default") && remoteWanted
                    && !sftp.connected && !sftp.busy && profile.host && routeAllowsSftp)
                sftp.connectToProfileAt(profile, stateFor(appliedSessionKey).remotePath || "")
        }
    }
    Timer { id: sessionSwitch; interval: 0; onTriggered: root.applyActiveSession() }

    SftpController { id: sftp; objectName: "sftpController" }
    LocalFileModel { id: localFiles; objectName: "localFileModel"; active: root.visible }
    Connections {
        target: sftp
        function onStateChanged() {
            if (root.pendingSessionChange && !sftp.busy) sessionSwitch.restart()
            if (!sftp.busy) root.saveCurrentState()
        }
        function onCurrentPathChanged() { root.saveCurrentState() }
        function onTransferCompleted() { localFiles.refresh() }
    }
    Connections {
        target: localFiles
        function onChanged() { if (!localFiles.busy) root.saveCurrentState() }
    }

    FileDialog {
        id: uploadDialog; title: "Choose files to upload"; fileMode: FileDialog.OpenFiles
        onAccepted: sftp.uploadFiles(selectedFiles)
    }
    FolderDialog {
        id: downloadDialog; title: "Copy remote item to this local folder"
        onAccepted: sftp.downloadTo(root.downloadRow, selectedFolder)
    }

    SplitView {
        anchors.fill: parent
        enabled: !root.pendingSessionChange
        orientation: Qt.Vertical
        handle: Rectangle { implicitHeight: 4; color: SplitHandle.hovered ? Theme.accent : Theme.borderSoft }

        FilePane {
            id: remotePane
            objectName: "remoteFilePane"
            controller: sftp
            remote: true
            visible: sftp.connected
            SplitView.fillHeight: true
            SplitView.minimumHeight: 180
            onCopyRequested: function(row) { sftp.downloadTo(row, localFiles.directoryUrl()) }
            onChooseDestinationRequested: function(row) { root.downloadRow = row; downloadDialog.open() }
            onDisconnectRequested: root.disconnectRemote()
        }

        Rectangle {
            objectName: "remoteFilesConnectView"
            visible: !sftp.connected && !!root.profile.host
            SplitView.preferredHeight: 142
            SplitView.minimumHeight: 120
            color: Theme.canvas
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 28, 330)
                spacing: 7
                Text {
                    Layout.fillWidth: true
                    text: "Remote files are off for this session"
                    textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter
                    color: Theme.text; font.pixelSize: Theme.uiFontSize(11); font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: !root.routeAllowsSftp ? "SFTP is disabled for this CyberArk route."
                          : sftp.statusText
                    textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter
                    color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(9)
                }
                AppButton {
                    objectName: "connectRemoteFilesButton"
                    Layout.alignment: Qt.AlignHCenter
                    text: sftp.busy ? "Connecting…" : "Connect remote files"
                    iconText: "↗"
                    emphasized: true
                    enabled: !sftp.busy && root.routeAllowsSftp
                    onClicked: root.connectRemote()
                }
                ProgressBar { visible: sftp.busy; Layout.fillWidth: true; indeterminate: true }
            }
        }

        FilePane {
            id: localPane
            objectName: "localFilePane"
            controller: localFiles
            canCopy: sftp.connected && !sftp.busy
            SplitView.fillHeight: !sftp.connected
            SplitView.preferredHeight: 310
            SplitView.minimumHeight: 180
            onCopyRequested: function(row) { if (sftp.connected && !sftp.busy) sftp.upload(localFiles.entryUrl(row)) }
            onChooseFilesRequested: uploadDialog.open()
        }
    }

    Text {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 8; visible: sftp.lastTransferText.length > 0
        text: sftp.lastTransferText; textFormat: Text.PlainText
        color: Theme.textMuted; wrapMode: Text.WrapAnywhere; font.pixelSize: Theme.uiFontSize(9)
        maximumLineCount: 3; elide: Text.ElideMiddle
    }
}
