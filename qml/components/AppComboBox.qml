import QtQuick
import QtQuick.Controls.Basic
import BitShell

ComboBox {
    id: control
    implicitHeight: 26
    property real minimumPopupWidth: 180
    property var outerFlickable: null
    property real outerContentY: 0

    function findOuterFlickable() {
        let item = control.parent
        while (item) {
            if (item.contentY !== undefined && item.contentHeight !== undefined)
                return item
            item = item.parent
        }
        return null
    }
    indicator: Item {
        implicitWidth: 12
        implicitHeight: 8
        x: control.mirrored ? 10 : control.width - width - 10
        y: (control.height - height) / 2
        opacity: control.enabled ? 1 : 0.4
        Rectangle { x: 1; y: 3; width: 6; height: 1; rotation: 40; color: Theme.textMuted }
        Rectangle { x: 5; y: 3; width: 6; height: 1; rotation: -40; color: Theme.textMuted }
    }
    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, control.minimumPopupWidth)
        height: Math.min(300, options.contentHeight + topPadding + bottomPadding)
        padding: 4
        margins: 8
        onAboutToShow: {
            control.outerFlickable = control.findOuterFlickable()
            control.outerContentY = control.outerFlickable
                                    ? control.outerFlickable.contentY : 0
        }
        onOpened: options.positionViewAtIndex(control.currentIndex, ListView.Contain)
        onClosed: {
            if (control.outerFlickable)
                control.outerFlickable.contentY = control.outerContentY
            control.outerFlickable = null
        }
        contentItem: ListView {
            id: options
            objectName: "comboPopupList"
            clip: true
            implicitHeight: contentHeight
            // A closed font/theme selector must not retain live delegates.
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius
        }
    }
    Binding {
        target: control.outerFlickable
        property: "contentY"
        value: control.outerContentY
        when: control.popup.visible && control.outerFlickable !== null
        restoreMode: Binding.RestoreNone
    }
    // Names in imported catalogs are data, never rich text or image URLs.
    delegate: ItemDelegate {
        id: option
        required property var model
        required property int index
        objectName: "comboOption:" + control.objectName + ":" + index
        width: ListView.view.width
        height: 26
        topPadding: 0
        bottomPadding: 0
        text: String(model[control.textRole] ?? "")
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted ? Theme.surfaceSelected : "transparent"
        }
        contentItem: Text {
            text: option.text
            textFormat: Text.PlainText
            color: Theme.text
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}
