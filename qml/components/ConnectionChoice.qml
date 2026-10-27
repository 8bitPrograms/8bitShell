import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

ColumnLayout {
    id: root
    property string mode: "inherit"
    property string routeId: ""
    property bool folderOnly: false
    property bool advancedOpen: false
    readonly property var options: {
        const routes = SessionStore.routes
        const rows = Object.keys(routes).sort().map(function(id) {
            return {text: (routes[id].name || id) + " · " + routes[id].host, value: id}
        })
        if (routeId && !routes[routeId])
            rows.unshift({text: routeId + " (missing — connection blocked)", value: routeId})
        return rows
    }

    function toSelection() {
        const result = {mode: root.mode}
        if (root.mode === "route") result.route = root.routeId
        if (root.mode !== "direct" && overrideAccount.checked
                && !(root.folderOnly && root.mode === "inherit"))
            result.account = accountEditor.toAccount()
        return result
    }

    function load(selection) {
        mode = selection.mode || "direct"
        routeId = selection.route || ""
        overrideAccount.checked = !!selection.account
        accountEditor.load(selection.account || {source: "identity", suffix: "SYS"})
        advancedOpen = folderOnly
    }

    function useCyberArk(enabled) {
        if (enabled) {
            mode = "route"
            if (!routeId && options.length > 0) routeId = options[0].value
        } else if (mode === "route") {
            mode = folderOnly ? "inherit" : "direct"
        }
    }

    RowLayout {
        visible: PersonalProfile.cyberArkEnabled
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.folderOnly ? "FOLDER CONNECTION" : "CONNECTION ROUTE"
            textFormat: Text.PlainText
            color: Theme.accent
            font.pixelSize: Theme.uiFontSize(10)
            font.bold: true
        }
        AppButton {
            objectName: "connectionAdvancedToggle"
            visible: !root.folderOnly
            compact: true
            text: root.advancedOpen ? "Less" : "Advanced"
            iconText: root.advancedOpen ? "⌃" : "⌄"
            onClicked: root.advancedOpen = !root.advancedOpen
        }
    }

    Rectangle {
        visible: PersonalProfile.cyberArkEnabled && !root.folderOnly
        Layout.fillWidth: true
        implicitHeight: cyberArkRow.implicitHeight + 18
        radius: Theme.radius
        color: root.mode === "route" ? Theme.accentDim : Theme.canvas
        border.color: root.mode === "route" ? Theme.accent : Theme.borderSoft

        RowLayout {
            id: cyberArkRow
            anchors.fill: parent
            anchors.margins: 9
            spacing: 10
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "Use CyberArk routing"
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Connect through the configured PSMP gateway and its assigned key"
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
            }
            AppSwitch {
                objectName: "cyberArkRoutingSwitch"
                checked: root.mode === "route"
                onToggled: root.useCyberArk(checked)
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: PersonalProfile.cyberArkEnabled && (root.folderOnly || root.advancedOpen)
        spacing: 5
        Text {
            text: "CONNECTION BEHAVIOR"
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.pixelSize: Theme.uiFontSize(9)
            font.bold: true
        }
        AppComboBox {
            objectName: "connectionModeCombo"
            Layout.fillWidth: true
            model: ["Inherit from folder", "Direct SSH", "CyberArk route"]
            currentIndex: root.mode === "inherit" ? 0 : root.mode === "direct" ? 1 : 2
            onActivated: {
                root.mode = ["inherit", "direct", "route"][currentIndex]
                if (root.mode === "route" && !root.routeId && root.options.length > 0)
                    root.routeId = root.options[0].value
            }
        }
    }

    AppComboBox {
        objectName: "connectionRouteCombo"
        Layout.fillWidth: true
        visible: PersonalProfile.cyberArkEnabled && root.mode === "route"
        model: root.options
        textRole: "text"
        valueRole: "value"
        currentIndex: root.options.findIndex(function(row) { return row.value === root.routeId })
        onActivated: root.routeId = currentValue
    }
    Text {
        Layout.fillWidth: true
        visible: PersonalProfile.cyberArkEnabled && root.mode === "route" && root.options.length === 0
        text: "No CyberArk route is configured. Open Settings → Authorization to add one; saving this session is blocked until a route exists."
        textFormat: Text.PlainText
        color: Theme.red
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
    }
    CheckBox {
        id: overrideAccount
        objectName: "overrideAccountCheck"
        visible: PersonalProfile.cyberArkEnabled && root.mode !== "direct" && !(root.folderOnly && root.mode === "inherit")
        text: "Override the route's target account"
    }
    AccountFields {
        id: accountEditor
        Layout.fillWidth: true
        visible: overrideAccount.visible && overrideAccount.checked
    }
    Text {
        Layout.fillWidth: true
        visible: PersonalProfile.cyberArkEnabled
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.uiFontSize(10)
        wrapMode: Text.Wrap
        text: root.mode === "direct"
              ? "Direct SSH uses this session's host and username."
              : root.mode === "inherit"
                ? "The nearest explicit folder rule wins."
                : "CyberArk routing is explicit. Missing identity, route, approval, or selected key blocks the connection; there is no direct fallback."
    }
}
