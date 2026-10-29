import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Item {
    id: root
    required property var terminal
    property bool blocked: false
    readonly property bool securityPromptVisible: gatewayReview.visible || x11Review.visible || errorDialog.visible
    readonly property var query: terminal ? terminal.localSshQuery : ({})
    property var candidates: []
    property int selectedIndex: -1
    property var pending: ({})
    readonly property bool offered: Boolean(query.host) && candidates.length > 0 && !blocked && !securityPromptVisible
    readonly property bool showing: offered && terminal.activeFocus
    objectName: "localSshSuggestions"

    function rowsFor(value) {
        if (!value.host) return []
        const saved = SessionStore.sshCandidates(value.host, value.user || "", value.port || 0,
                          PersonalProfile.cyberArkEnabled, PersonalProfile.routingIdentity, 40).results || []
        const routes = SessionStore.sshRouteCandidates(value.host, value.user || "", value.port || 0,
                          PersonalProfile.cyberArkEnabled, PersonalProfile.routingIdentity, 40).results || []
        return saved.concat(routes)
    }
    function refresh() {
        const previous = candidates[selectedIndex]
        const rows = rowsFor(query)
        candidates = rows
        let chosen = -1
        if (previous) chosen = rows.findIndex(row => row.kind === previous.kind
                        && (row.kind === "session" ? row.id === previous.id : row.routeId === previous.routeId))
        // Saved connections take precedence. A new CyberArk route always needs
        // an explicit selection; ordinary Enter retains the native SSH command.
        selectedIndex = chosen >= 0 ? chosen : (rows.length && rows[0].kind === "session" ? 0 : -1)
        Qt.callLater(function() {
            if (root.selectedIndex >= 0) choices.positionViewAtIndex(root.selectedIndex, ListView.Contain)
        })
    }
    function moveSelection(delta) {
        if (!candidates.length) return
        selectedIndex = selectedIndex < 0 ? (delta < 0 ? candidates.length - 1 : 0)
                      : (selectedIndex + delta + candidates.length) % candidates.length
        choices.positionViewAtIndex(selectedIndex, ListView.Contain)
    }
    function fail(message) {
        errorDialog.message = message
        errorDialog.open()
    }
    function activate(row, expectedQuery, approvedX11Profile) {
        if (!row || !terminal) return
        // Resolve again after every modal. Never execute a stale imported row,
        // removed route, changed target or stale input line.
        if (JSON.stringify(query) !== JSON.stringify(expectedQuery)) {
            fail("The terminal command changed. Select the connection again.")
            return
        }
        const fresh = rowsFor(expectedQuery).find(candidate => candidate.kind === row.kind
                     && (row.kind === "session" ? candidate.id === row.id : candidate.routeId === row.routeId))
        if (!fresh) { fail("This connection has changed or was removed. Select it again."); return }
        if (!fresh.valid) { fail(fresh.reason || "This connection is unavailable."); return }
        const source = fresh.kind === "session" ? SessionStore.getById(fresh.id)
                     : {host: expectedQuery.host, port: expectedQuery.port || 22,
                        connection: {mode: "route", route: fresh.routeId}}
        const result = SessionStore.prepareProfile(source, PersonalProfile.routingIdentity)
        if (result.error) { fail(result.error); return }
        pending = {row: fresh, query: expectedQuery, result: result, approvedX11Profile: approvedX11Profile || ""}
        if (result.fingerprint && !PersonalProfile.routeApproved(result.fingerprint)) {
            gatewayReview.open()
            return
        }
        if (result.profile.forwardX11 && approvedX11Profile !== JSON.stringify(result.profile)) {
            x11Review.open()
            return
        }
        terminal.runLocalSsh(result.profile, expectedQuery)
        pending = ({})
    }
    function acceptSelection() {
        if (selectedIndex < 0 || selectedIndex >= candidates.length || !offered) return
        activate(candidates[selectedIndex], JSON.parse(JSON.stringify(query)), "")
    }
    function authentication(row) {
        if (row.authenticationMode === "password") return row.authentication || "Password"
        let keyId = row.identityKey || ""
        if (row.connectionMode === "cyberark" && SshKeyStore.cyberArkKeyId)
            keyId = SshKeyStore.cyberArkKeyId
        if (!keyId && !row.identityFile) keyId = SshKeyStore.defaultKeyId
        if (keyId && keyId !== "none") {
            const key = SshKeyStore.keys.find(entry => entry.id === keyId)
            return key && key.name ? "Key: " + key.name : "Saved SSH key"
        }
        if (keyId !== "none" && row.identityFile) return "Key file"
        return "Native SSH authentication"
    }
    function focusPrompt() {
        if (gatewayReview.visible) gatewayReview.standardButton(Dialog.Cancel).forceActiveFocus()
        else if (x11Review.visible) x11Review.standardButton(Dialog.Cancel).forceActiveFocus()
        else if (errorDialog.visible) errorDialog.standardButton(Dialog.Ok).forceActiveFocus()
        else terminal.forceActiveFocus()
    }

    onQueryChanged: { selectedIndex = -1; candidates = []; refresh() }
    Component.onCompleted: refresh()
    Binding { target: root.terminal; property: "localSshSuggestionsActive"; value: root.showing }
    Binding { target: root.terminal; property: "localSshSelectionReady"; value: root.showing && root.selectedIndex >= 0 }
    Connections {
        target: root.terminal
        function onLocalSshNavigate(delta) { root.moveSelection(delta) }
        function onLocalSshAccept() { root.acceptSelection() }
        function onLocalSshError(message) { root.fail(message) }
    }
    Connections { target: SessionStore; function onRevisionChanged() { root.refresh() } }
    Connections {
        target: PersonalProfile
        function onChanged() { root.refresh() }
    }

    Rectangle {
        id: panel
        objectName: "localSshSuggestionPanel"
        visible: root.showing
        z: 15
        width: Math.min(620, Math.max(0, root.width - 16))
        height: Math.min(36 + Math.min(6, root.candidates.length) * 52 + 27, Math.max(0, root.height - 16))
        x: Math.max(8, Math.min(root.terminal.cursorX, root.width - width - 8))
        readonly property real below: root.terminal.y + root.terminal.cursorY + root.terminal.cursorHeight + 6
        y: Math.max(8, Math.min(root.height - height - 8,
                below + height <= root.height - 8 ? below : root.terminal.y + root.terminal.cursorY - height - 6))
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.accent
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Text {
                Layout.fillWidth: true; Layout.preferredHeight: 34; Layout.leftMargin: 12; Layout.rightMargin: 12
                text: "SSH · " + (root.query.host || "")
                textFormat: Text.PlainText; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter
                color: Theme.accent; font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(11); font.weight: Font.DemiBold
            }
            ListView {
                id: choices
                objectName: "localSshCandidateList"
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; model: root.candidates; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: choices.width; height: 52
                    color: index === root.selectedIndex ? Theme.surfaceSelected : "transparent"
                    Rectangle { width: 3; height: parent.height; color: Theme.accent; visible: index === root.selectedIndex }
                    Column {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; anchors.topMargin: 7; spacing: 4
                        Text {
                            width: parent.width; textFormat: Text.PlainText; elide: Text.ElideRight
                            text: (modelData.account || "Default account") + "  ·  " + modelData.name
                                  + (modelData.group ? " · " + modelData.group : "")
                            color: modelData.valid ? Theme.text : Theme.textMuted
                            font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(11); font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width; textFormat: Text.PlainText; elide: Text.ElideRight
                            text: !modelData.valid ? modelData.reason
                                  : (modelData.kind === "session" ? "Saved connection" : "CyberArk route")
                                    + (modelData.connectionMode === "cyberark" ? " · via " + modelData.gateway : " · direct")
                                    + " · " + root.authentication(modelData)
                                    + (modelData.port !== 22 ? " · port " + modelData.port : "")
                            color: Theme.textMuted; font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(9)
                        }
                    }
                    MouseArea {
                        anchors.fill: parent; hoverEnabled: true
                        onPositionChanged: root.selectedIndex = index
                        onClicked: { root.selectedIndex = index; root.acceptSelection() }
                    }
                }
            }
            Text {
                Layout.fillWidth: true; Layout.preferredHeight: 25; Layout.leftMargin: 12; Layout.rightMargin: 12
                text: "↑ ↓ / Tab select   ·   Enter connect here   ·   Esc dismiss"
                textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                color: Theme.textMuted; font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(9)
            }
        }
    }

    Dialog {
        id: gatewayReview
        objectName: "localSshGatewayReview"
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Review CyberArk gateway"; modal: true; focus: true
        width: Math.min(580, parent.width - 40)
        standardButtons: Dialog.Ok | Dialog.Cancel
        footer: AppDialogButtonBox { }
        property bool approved: false
        onOpened: { approved = false; standardButton(Dialog.Cancel).forceActiveFocus() }
        onAccepted: {
            approved = PersonalProfile.approveRoute(root.pending.result.fingerprint)
            if (!approved) root.fail(PersonalProfile.lastError)
        }
        onClosed: {
            const snapshot = root.pending
            if (approved) { approved = false; root.activate(snapshot.row, snapshot.query, snapshot.approvedX11Profile) }
            root.focusPrompt()
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.text
                text: "This sends your employee identity to the gateway below. Approve only a gateway you recognize. The connection will run in this local terminal." }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.accent
                text: root.pending.result && root.pending.result.profile.psmp
                      ? "Target: " + root.pending.result.profile.host + "\nGateway: " + root.pending.result.profile.psmp.route.host
                        + ":" + (root.pending.result.profile.psmp.route.port || 22) + "\nLogin: " + root.pending.result.login : "" }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted
                text: "SSH will verify the gateway using this environment's known hosts. Passwords and key passphrases are entered directly into SSH." }
        }
    }
    Dialog {
        id: x11Review
        objectName: "localSshX11Review"
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Allow remote graphical applications?"; modal: true; focus: true
        width: Math.min(520, parent.width - 40); standardButtons: Dialog.Ok | Dialog.Cancel
        footer: AppDialogButtonBox { }
        property bool approved: false
        onOpened: { approved = false; standardButton(Dialog.Cancel).forceActiveFocus() }
        onAccepted: approved = true
        onClosed: {
            const snapshot = root.pending
            if (approved) { approved = false; root.activate(snapshot.row, snapshot.query, JSON.stringify(snapshot.result.profile)) }
            root.focusPrompt()
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.text
                text: "This saved connection enables untrusted X11 forwarding (-X). It gives the remote host graphical access through your local X server. Continue only if you trust this host." }
        }
    }
    Dialog {
        id: errorDialog
        objectName: "localSshErrorDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        property string message: ""
        title: "Connection unavailable"; modal: true; focus: true
        width: Math.min(540, parent.width - 40); standardButtons: Dialog.Ok
        footer: AppDialogButtonBox { }
        onOpened: standardButton(Dialog.Ok).forceActiveFocus()
        onClosed: root.focusPrompt()
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; text: errorDialog.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.text }
        }
    }
}
