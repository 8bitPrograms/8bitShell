pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Popup {
    id: root
    objectName: "commandPalette"
    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    palette: Theme.controlPalette

    // Results are supplied by Main. IDs identify actions or tabs; displayed
    // labels never become an executable command or a lookup key.
    property var results: []
    property alias query: search.text
    property int currentIndex: -1
    property string selectedId: ""
    property bool explicitSelection: false
    property string shortcutText: "Ctrl+Shift+P"
    readonly property bool activationPending: actionChosen
    property bool actionChosen: false
    property string pendingId: ""
    signal searchChanged(string text)
    signal chosen(string id)

    width: Math.max(0, Math.min(700, parent ? parent.width - 32 : 700))
    height: Math.max(0, Math.min(560, parent ? parent.height - 48 : 560, preferredHeight()))
    x: parent ? Math.max(0, (parent.width - width) / 2) : 0
    y: parent ? Math.max(0, Math.min(70, (parent.height - height) / 3)) : 0

    function preferredHeight() {
        if (!results.length) return 230
        let height = 122
        let category = ""
        for (let i = 0; i < Math.min(8, results.length); ++i) {
            const next = String(results[i].category || "Commands")
            if (i === 0 || next !== category) height += 24
            height += 50
            category = next
        }
        return height
    }
    function firstSelectable() {
        // Explicit-only choices remain available to arrows and clicks, but may
        // not become an accidental fallback when a saved connection is blocked.
        for (let i = 0; i < results.length; ++i)
            if (results[i].enabled !== false && results[i].autoSelect !== false) return i
        return -1
    }
    function reconcileSelection() {
        let found = -1
        if (selectedId.length) {
            for (let i = 0; i < results.length; ++i) {
                if (String(results[i].id) === selectedId && results[i].enabled !== false
                        && (results[i].autoSelect !== false || explicitSelection)) {
                    found = i
                    break
                }
            }
        }
        if (found < 0) explicitSelection = false
        currentIndex = found >= 0 ? found : firstSelectable()
        selectedId = currentIndex >= 0 ? String(results[currentIndex].id) : ""
        Qt.callLater(function() {
            if (root.currentIndex >= 0) resultList.positionViewAtIndex(root.currentIndex, ListView.Contain)
        })
    }
    function moveSelection(direction, focusRow) {
        if (!results.length) return
        let next = currentIndex
        for (let visited = 0; visited < results.length; ++visited) {
            next = next < 0 ? (direction < 0 ? results.length - 1 : 0)
                           : (next + direction + results.length) % results.length
            if (results[next].enabled !== false) {
                explicitSelection = true
                currentIndex = next
                if (focusRow) Qt.callLater(focusResult)
                return
            }
        }
    }
    function focusSearch() { search.forceActiveFocus(Qt.ShortcutFocusReason) }
    function focusResult() {
        if (currentIndex >= 0) {
            resultList.positionViewAtIndex(currentIndex, ListView.Contain)
            const item = resultList.itemAtIndex(currentIndex)
            if (item) {
                explicitSelection = true
                item.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
        focusSearch()
    }
    function showPalette(initialQuery) {
        actionChosen = false
        pendingId = ""
        explicitSelection = false
        selectedId = ""
        currentIndex = -1
        const next = initialQuery === undefined ? "" : String(initialQuery)
        if (query === next) {
            searchChanged(query)
            reconcileSelection()
        } else query = next
        open()
        Qt.callLater(function() { root.focusSearch(); search.selectAll() })
    }
    function choose(index) {
        if (!visible || actionChosen || index < 0 || index >= results.length
                || results[index].enabled === false) return
        const id = String(results[index].id || "")
        if (!id.length) return
        explicitSelection = true
        currentIndex = index
        pendingId = id
        actionChosen = true
        close()
    }
    function handleNavigation(event, focusRow) {
        if (event.modifiers !== Qt.NoModifier) return
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            moveSelection(event.key === Qt.Key_Up ? -1 : 1, focusRow)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            choose(currentIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            close()
            event.accepted = true
        }
    }

    onQueryChanged: {
        explicitSelection = false
        selectedId = ""
        currentIndex = -1
        searchChanged(query)
        reconcileSelection()
    }
    onResultsChanged: reconcileSelection()
    onCurrentIndexChanged: {
        selectedId = currentIndex >= 0 && currentIndex < results.length ? String(results[currentIndex].id) : ""
        Qt.callLater(function() {
            if (root.currentIndex >= 0) resultList.positionViewAtIndex(root.currentIndex, ListView.Contain)
        })
    }
    onOpened: focusSearch()
    onClosed: {
        const id = pendingId
        pendingId = ""
        if (actionChosen && id.length) chosen(id)
    }

    Overlay.modal: Rectangle { color: "#78080b10" }
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusLarge
        border.color: Theme.accent
        border.width: 1
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.radiusLarge
            height: 2
            visible: Theme.cyberpunkChrome
            color: Theme.secondaryAccent
        }
    }
    contentItem: FocusScope {
        clip: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "Search & commands"
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(12)
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: root.width > 430
                    text: root.shortcutText
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.uiFontSize(9)
                }
                AppButton {
                    id: closeButton
                    objectName: "commandPaletteClose"
                    compact: true
                    text: "Esc"
                    implicitHeight: 26
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "Close command palette"
                    onClicked: root.close()
                    Keys.onTabPressed: function(event) { root.focusSearch(); event.accepted = true }
                    Keys.onBacktabPressed: function(event) { root.focusResult(); event.accepted = true }
                }
            }
            AppTextField {
                id: search
                objectName: "commandPaletteSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                maximumLength: 256
                placeholderText: "Search an IP, tab, connection, setting or theme..."
                Accessible.name: "Search tabs, connections and commands"
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) { root.handleNavigation(event, false) }
                Keys.onTabPressed: function(event) {
                    if (root.currentIndex >= 0) root.focusResult()
                    else closeButton.forceActiveFocus(Qt.TabFocusReason)
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) { closeButton.forceActiveFocus(Qt.BacktabFocusReason); event.accepted = true }
            }
            ListView {
                id: resultList
                objectName: "commandPaletteResults"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                visible: root.results.length > 0
                model: root.visible ? root.results : []
                currentIndex: root.currentIndex
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar { focusPolicy: Qt.NoFocus }
                delegate: FocusScope {
                    id: resultItem
                    required property var modelData
                    required property int index
                    readonly property string category: String(modelData.category || "Commands")
                    readonly property bool startsCategory: index === 0
                        || String(root.results[index - 1].category || "Commands") !== category
                    width: resultList.width
                    height: resultButton.height + (startsCategory ? 24 : 0)
                    Text {
                        visible: resultItem.startsCategory
                        x: 8; y: 2
                        width: parent.width - 16; height: 20
                        text: resultItem.category
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(9)
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    ItemDelegate {
                        id: resultButton
                        objectName: "commandPaletteResult:" + String(resultItem.modelData.id)
                        y: resultItem.startsCategory ? 24 : 0
                        width: parent.width
                        height: 50
                        enabled: resultItem.modelData.enabled !== false
                        highlighted: resultItem.index === root.currentIndex
                        hoverEnabled: true
                        focus: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: String(resultItem.modelData.title || "")
                        Accessible.description: String(resultItem.modelData.reason || resultItem.modelData.subtitle || "")
                        onActiveFocusChanged: if (activeFocus) root.currentIndex = resultItem.index
                        onClicked: root.choose(resultItem.index)
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: function(event) { root.handleNavigation(event, true) }
                        Keys.onTabPressed: function(event) { closeButton.forceActiveFocus(Qt.TabFocusReason); event.accepted = true }
                        Keys.onBacktabPressed: function(event) { root.focusSearch(); event.accepted = true }
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: resultButton.highlighted ? Theme.surfaceSelected
                                 : resultButton.hovered ? Theme.surfaceHover : "transparent"
                            border.width: resultButton.activeFocus ? 2 : 0
                            border.color: Theme.accent
                            Rectangle {
                                visible: resultButton.highlighted
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                width: 3; height: parent.height - 12
                                color: Theme.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Text {
                                visible: Boolean(resultItem.modelData.icon)
                                Layout.preferredWidth: visible ? 24 : 0
                                text: String(resultItem.modelData.icon || "")
                                textFormat: Text.PlainText
                                color: Theme.accent
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.uiFontSize(12)
                                horizontalAlignment: Text.AlignHCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: String(resultItem.modelData.title || "")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: resultButton.enabled ? Theme.text : Theme.textMuted
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize(11)
                                    font.weight: resultButton.highlighted ? Font.DemiBold : Font.Normal
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: String(resultItem.modelData.enabled === false && resultItem.modelData.reason
                                                 ? resultItem.modelData.reason : resultItem.modelData.subtitle || "")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: Theme.textMuted
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize(9)
                                }
                            }
                        }
                        ToolTip {
                            id: resultTip
                            visible: resultButton.hovered && String(resultItem.modelData.subtitle || "").length > 0
                            delay: 600
                            width: Math.max(0, Math.min(520, root.width - 24))
                            text: String(resultItem.modelData.title || "") + "\n" + String(resultItem.modelData.subtitle || "")
                            contentItem: Text {
                                text: resultTip.text
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize(10)
                            }
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                visible: root.results.length === 0
                Text {
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 24)
                    text: "No matches. Try a host, connection name, settings or theme."
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(11)
                }
            }
            Text {
                Layout.fillWidth: true
                text: "↑ ↓ select   ·   Enter run   ·   Tab focus   ·   Esc close"
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize(9)
            }
        }
    }
}
