import QtQuick

Item {
    id: root

    default property alias contentData: panelContent.data
    property bool expanded: true
    property int animationDuration: 180
    property real reveal: expanded ? 1 : 0
    readonly property bool animating: revealAnimation.running
    property bool initialized: false

    // Keep the layout slot until the panel has rolled up completely. Only the
    // visual position changes per frame; terminal and list sizes stay stable.
    visible: expanded || reveal > 0
    enabled: expanded
    clip: true

    readonly property Item contentItem: Item {
        id: panelContent
        parent: root
        width: root.width
        height: root.height
        y: (root.reveal - 1) * height
    }

    Behavior on reveal {
        enabled: root.initialized
        NumberAnimation {
            id: revealAnimation
            duration: root.animationDuration
            easing.type: Easing.OutCubic
        }
    }

    // Restore saved visibility immediately on startup, without an entrance
    // animation. Later reversals continue from the current visual position.
    Component.onCompleted: initialized = true
}
