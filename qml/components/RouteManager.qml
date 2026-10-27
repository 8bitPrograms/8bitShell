import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Dialog {
    id: root; objectName: "routeManager"
    parent: Overlay.overlay; anchors.centerIn: parent
    width: Math.min(560, parent.width - 40); height: Math.min(670, parent.height - 40)
    title: "CyberArk connection routes"; modal: true; focus: true
    property string editingId: ""
    property string error: ""
    readonly property var routeIds: Object.keys(SessionStore.routes).sort()
    function edit(id) {
        editingId = id; error = ""
        const route = SessionStore.routes[id] || {}
        routeId.text = id || "company-psmp"
        routeName.text = route.name || "CyberArk PSMP"
        host.text = route.host || ""
        port.text = String(route.port || 22)
        accountEditor.load(route.account || {source: "identity", suffix: "SYS"})
        sftp.checked = !!route.allowSftp; monitor.checked = !!route.allowMonitoring
    }
    function openManager() { edit(routeIds.length ? routeIds[0] : ""); open() }
    function saveRoute() {
        if (!editingId && SessionStore.routes[routeId.text]) { error = "This route id already exists. Select it to edit, or use a different id."; return }
        const route = {type: "cyberark-psmp", name: routeName.text, host: host.text.trim(), port: Number(port.text),
            account: accountEditor.toAccount(), allowSftp: sftp.checked, allowMonitoring: monitor.checked}
        if (!SessionStore.saveRoute(routeId.text.trim(), JSON.parse(JSON.stringify(route)))) { error = SessionStore.lastError; return }
        editingId = routeId.text.trim(); error = "Saved. A new or changed route requires approval before connecting."
    }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
    contentItem: ScrollView {
        id: scroll; clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth; spacing: 9
            RowLayout {
                Layout.fillWidth: true
                AppComboBox { Layout.fillWidth: true; model: root.routeIds; onActivated: root.edit(currentText) }
                AppButton { text: "New"; onClicked: root.edit("") }
            }
            Text { Layout.fillWidth: true; text: "Shared rules only. Your Employee ID stays in local Authorization settings."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(11) }
            AppTextField { id: routeId; objectName: "routeIdField"; Layout.fillWidth: true; enabled: !root.editingId; placeholderText: "Stable route id" }
            AppTextField { id: routeName; Layout.fillWidth: true; placeholderText: "Display name" }
            RowLayout {
                Layout.fillWidth: true
                AppTextField { id: host; objectName: "routeHostField"; Layout.fillWidth: true; placeholderText: "PSMP gateway" }
                AppTextField { id: port; Layout.preferredWidth: 85; placeholderText: "Port" }
            }
            Text { text: "DEFAULT TARGET ACCOUNT"; textFormat: Text.PlainText; color: Theme.accent; font.pixelSize: Theme.uiFontSize(10); font.bold: true }
            AccountFields { id: accountEditor; Layout.fillWidth: true }
            CheckBox { id: sftp; text: "Allow SFTP for this route (key/agent authentication only)" }
            CheckBox { id: monitor; text: "Allow monitoring commands for this route" }
            Text { Layout.fillWidth: true; text: "Auxiliary protocols are off unless explicitly permitted. PSMP policy must support them; terminal MFA is not reused. Monitoring is also off by default in each tab."; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(11) }
            Text { Layout.fillWidth: true; text: root.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.accent; font.pixelSize: Theme.uiFontSize(11) }
        }
    }
    footer: DialogButtonBox {
        AppButton { text: "Close"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: root.close() }
        AppButton { objectName: "saveRouteButton"; text: "Save route"; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; emphasized: true; onClicked: root.saveRoute() }
    }
}
