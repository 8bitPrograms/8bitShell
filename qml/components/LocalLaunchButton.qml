pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import BitShell

Rectangle {
    id: root
    signal launchDefault()
    signal launchProfile(string profileId)
    signal manageRequested()
    implicitWidth: 61; implicitHeight: 30
    radius: Theme.radiusSmall
    color: "transparent"; border.color: Theme.border
    clip: true
    AppButton {
        id: plus
        objectName: "newLocalTerminalButton"
        width: 36; height: parent.height
        iconText: "＋"; compact: true
        Accessible.name: "New terminal: " + (LocalProfileStore.defaultProfile.name || "default profile")
        onClicked: root.launchDefault()
        background: Rectangle { color: plus.down ? Theme.surfaceSelected : plus.hovered ? Theme.surfaceHover : "transparent"; border.color: plus.activeFocus ? Theme.accent : "transparent" }
        ToolTip.visible: hovered
        ToolTip.text: "New " + (LocalProfileStore.defaultProfile.name || "default profile")
    }
    Rectangle { x: 36; width: 1; height: parent.height; color: Theme.border }
    AppButton {
        id: arrow
        objectName: "localProfileMenuButton"
        x: 37; width: 24; height: parent.height
        iconText: "⌄"; compact: true
        Accessible.name: "Choose a local profile"
        onClicked: menu.openBelow(root, true)
        background: Rectangle { color: arrow.down || menu.visible ? Theme.surfaceSelected : arrow.hovered ? Theme.surfaceHover : "transparent"; border.color: arrow.activeFocus ? Theme.accent : "transparent" }
        ToolTip.visible: hovered; ToolTip.text: "Local profiles"
    }
    AppMenu {
        id: menu
        objectName: "localProfileMenu"
        implicitWidth: 280
        Instantiator {
            model: LocalProfileStore.profiles
            delegate: AppMenuItem {
                required property var modelData
                objectName: "launchLocalProfile:" + modelData.id
                text: modelData.name
                selected: modelData.id === LocalProfileStore.defaultProfileId
                onTriggered: root.launchProfile(modelData.id)
            }
            onObjectAdded: function(index, object) { menu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { menu.removeItem(object) }
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: LocalProfileStore.discovering ? "Finding WSL distributions…" : "Refresh detected shells"
            enabled: !LocalProfileStore.discovering
            onTriggered: LocalProfileStore.refresh()
        }
        AppMenuItem { objectName: "manageLocalProfiles"; text: "Manage local profiles…"; onTriggered: root.manageRequested() }
    }
}
