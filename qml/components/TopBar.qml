import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BitShell

Rectangle {
    id: root
    objectName: "topBar"

    signal toggleFiles()
    signal connectRequested(string target)
    signal savedSessionRequested(string id)
    signal splitCountRequested(int count)
    signal settingsRequested(string section)
    signal commandPaletteRequested()
    property bool filesVisible: false
    property int splitCount: 1
    property real navigationWidth: 300
    property var searchData: ({results: [], total: 0})
    property string resultQuery: ""
    function refreshSearch() {
        searchData = SessionStore.search(quickConnect.text, 80)
        resultQuery = quickConnect.text
        resultsList.currentIndex = searchData.results.length ? 0 : -1
        if (quickConnect.activeFocus && quickConnect.text.trim().length > 0) resultsPopup.open()
        else resultsPopup.close()
    }
    function chooseResult(index) {
        if (resultQuery !== quickConnect.text) { refreshSearch(); return }
        if (index >= 0 && index < searchData.results.length) {
            const id = searchData.results[index].id
            resultsPopup.close(); quickConnect.clear()
            root.savedSessionRequested(id)
        }
    }
    Timer { id: searchDelay; interval: 90; onTriggered: root.refreshSearch() }
    Connections { target: SessionStore; function onRevisionChanged() { if (quickConnect.activeFocus) searchDelay.restart() } }

    function focusQuickConnect() {
        quickConnect.forceActiveFocus()
        quickConnect.selectAll()
    }
    function dismissSearch() { resultsPopup.close() }

    function buttonSurface(control, selected) {
        if (Theme.cyberpunkChrome && selected)
            return control.down ? Theme.surfaceRaised : control.hovered ? Theme.surface : Theme.canvas
        return control.down ? Theme.headerPressed : control.hovered ? Theme.headerHover
             : selected ? Theme.accentDim : "transparent"
    }
    function buttonBorder(control, selected) {
        if (control.activeFocus)
            return Theme.cyberpunkChrome && selected ? Theme.secondaryAccent : Theme.headerFocus
        return Theme.cyberpunkChrome ? Theme.headerControlBorder
             : selected ? Theme.accentDim : Theme.border
    }

    implicitHeight: 34
    color: Theme.headerSurface
    border.color: Theme.chromeBorder

    Rectangle {
        objectName: "headerAccentBand"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 3
        z: 1
        visible: Theme.cyberpunkChrome
        color: Theme.accent
    }

    // Empty parts of the bar move the window. Controls above this still take the click.
    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        // Buttons and fields keep the press. Only empty bar space starts a move.
        grabPermissions: PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active && root.Window.window) root.Window.window.startSystemMove()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: {
            const window = root.Window.window
            if (!window)
                return
            if (window.visibility === Window.Maximized)
                window.showNormal()
            else
                window.showMaximized()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.preferredWidth: root.navigationWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Rectangle {
                    objectName: "headerBrandBacking"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: Theme.cyberpunkChrome ? 3 : 0
                    color: Theme.cyberpunkChrome ? Theme.canvas : "transparent"
                    BrandMark {
                        anchors.fill: parent
                        anchors.margins: Theme.cyberpunkChrome ? 2 : 0
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { textFormat: Text.PlainText;
                        objectName: "headerTitle"
                        text: "8bitShell"
                        color: Theme.headerText
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize(11)
                        font.bold: true
                        font.letterSpacing: 0.2
                    }
                }

                PanelButton {
                    id: connectionsButton
                    objectName: "connectionsToggle"
                    implicitWidth: 26
                    implicitHeight: 24
                    panelVisible: AppSettings.sidebarVisible
                    helpText: panelVisible ? "Hide connections" : "Show connections"
                    onClicked: AppSettings.sidebarVisible = !AppSettings.sidebarVisible
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: root.buttonSurface(connectionsButton, connectionsButton.panelVisible)
                        border.color: root.buttonBorder(connectionsButton, connectionsButton.panelVisible)
                        border.width: connectionsButton.activeFocus ? 2 : 1
                    }
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 17
                            height: 15
                            radius: 2
                            color: "transparent"
                            border.color: connectionsButton.panelVisible ? Theme.headerActiveText : Theme.headerTextMuted
                            Rectangle {
                                x: 3
                                y: 3
                                width: 4
                                height: 9
                                radius: 1
                                color: connectionsButton.panelVisible ? Theme.headerActiveText : Theme.headerTextMuted
                                opacity: connectionsButton.panelVisible ? 1 : 0.45
                            }
                            Rectangle {
                                x: 8
                                y: 1
                                width: 1
                                height: 13
                                color: connectionsButton.panelVisible ? Theme.headerActiveText : Theme.headerTextMuted
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.headerDivider
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Rectangle {
                    id: searchBox
                    objectName: "quickConnectSurface"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 140
                    Layout.maximumWidth: 520
                    implicitHeight: 24
                    radius: Theme.radius
                    color: Theme.canvas
                    border.color: quickConnect.activeFocus
                                  ? (Theme.cyberpunkChrome ? Theme.secondaryAccent : Theme.accent) : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 6
                        spacing: 8

                        Text { textFormat: Text.PlainText;
                            text: ">_"
                            color: Theme.accent
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.uiFontSize(10)
                        }

                        TextField {
                            id: quickConnect
                            objectName: "quickConnectField"
                            Layout.fillWidth: true
                            placeholderText: "Connect or find a saved session…"
                            color: Theme.text
                            placeholderTextColor: Theme.textFaint
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.uiFontSize(10)
                            background: null
                            selectByMouse: true
                            maximumLength: 256
                            onTextEdited: searchDelay.restart()
                            onTextChanged: if (text.length === 0) resultsPopup.close()
                            onActiveFocusChanged: if (activeFocus) searchDelay.restart()
                            Keys.onDownPressed: {
                                if (!resultsPopup.visible) root.refreshSearch()
                                else resultsList.currentIndex = Math.min(root.searchData.results.length - 1, resultsList.currentIndex + 1)
                                resultsList.positionViewAtIndex(resultsList.currentIndex, ListView.Contain)
                            }
                            Keys.onUpPressed: {
                                resultsList.currentIndex = Math.max(0, resultsList.currentIndex - 1)
                                resultsList.positionViewAtIndex(resultsList.currentIndex, ListView.Contain)
                            }
                            Keys.onEscapePressed: { searchDelay.stop(); resultsPopup.close() }
                            onAccepted: {
                                searchDelay.stop()
                                // Refresh before resolving Enter; never use results from an older query.
                                const selected = resultsPopup.visible && root.resultQuery === text ? resultsList.currentIndex : 0
                                root.searchData = SessionStore.search(text, 80)
                                root.resultQuery = text
                                if (root.searchData.results.length) root.chooseResult(Math.max(0, Math.min(selected, root.searchData.results.length - 1)))
                                else root.refreshSearch() // Direct connection is an explicit separate action.
                            }
                        }

                        Rectangle {
                            implicitWidth: shortcutLabel.implicitWidth + 12
                            implicitHeight: 16
                            radius: 4
                            color: Theme.surfaceRaised
                            border.color: Theme.borderSoft
                            Text { textFormat: Text.PlainText;
                                id: shortcutLabel
                                anchors.centerIn: parent
                                text: AppSettings.keyBindings.quickConnect || ""
                                color: Theme.textFaint
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize(9)
                            }
                        }
                    }
                    Popup {
                        id: resultsPopup; objectName: "sessionSearchPopup"
                        y: searchBox.height + 6; width: Math.min(600, Overlay.overlay.width - 24)
                        height: Math.min(450, 98 + Math.max(1, root.searchData.results.length) * 66)
                        margins: 12; padding: 8; focus: false; modal: false
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }
                        contentItem: ColumnLayout {
                            spacing: 5
                            Text {
                                Layout.fillWidth: true; textFormat: Text.PlainText; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10)
                                text: root.searchData.total + " saved sessions" + (root.searchData.total > 80 ? " · showing first 80; refine your search" : " · ↑ ↓ to choose, Enter to open")
                            }
                            ListView {
                                id: resultsList; objectName: "sessionSearchResults"
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3
                                model: root.searchData.results; currentIndex: -1
                                ScrollBar.vertical: ScrollBar {}
                                delegate: ItemDelegate {
                                    id: resultRow; required property var modelData; required property int index
                                    objectName: "sessionSearchResult:" + modelData.id
                                    width: ListView.view.width; height: 63
                                    onClicked: root.chooseResult(index)
                                    background: Rectangle { color: resultRow.hovered || resultsList.currentIndex === resultRow.index ? Theme.surfaceSelected : "transparent"; radius: Theme.radiusSmall }
                                    contentItem: Column {
                                        spacing: 3
                                        Text { width: parent.width; textFormat: Text.PlainText; text: resultRow.modelData.name; color: Theme.accentBright; font.pixelSize: Theme.uiFontSize(13); elide: Text.ElideRight }
                                        Text { width: parent.width; textFormat: Text.PlainText; text: (resultRow.modelData.user ? resultRow.modelData.user + "@" : "") + resultRow.modelData.host; color: Theme.text; font.pixelSize: Theme.uiFontSize(10); elide: Text.ElideRight }
                                        Text { width: parent.width; textFormat: Text.PlainText; text: (resultRow.modelData.group || "Root") + (resultRow.modelData.tags.length ? " · " + resultRow.modelData.tags.join(", ") : ""); color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(10); elide: Text.ElideRight }
                                    }
                                }
                                Text { anchors.centerIn: parent; visible: root.searchData.results.length === 0; text: "No saved sessions match your search."; textFormat: Text.PlainText; color: Theme.textMuted; font.pixelSize: Theme.uiFontSize(12) }
                            }
                            AppButton {
                                objectName: "searchDirectConnect"; Layout.fillWidth: true
                                text: "Quick connect to: " + quickConnect.text
                                onClicked: { const target = quickConnect.text; resultsPopup.close(); root.connectRequested(target) }
                            }
                        }
                    }
                }

                AppButton {
                    id: commandPaletteButton
                    objectName: "commandPaletteButton"
                    text: "Search"
                    compact: true
                    implicitHeight: 24
                    onClicked: root.commandPaletteRequested()
                    Accessible.name: "Search tabs, connections and commands"
                    ToolTip.visible: hovered
                    ToolTip.text: "Search tabs, connections and commands"
                        + (AppSettings.keyBindings.commandPalette ? " · " + AppSettings.keyBindings.commandPalette : "")
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: root.buttonSurface(commandPaletteButton, false)
                        border.color: root.buttonBorder(commandPaletteButton, false)
                        border.width: commandPaletteButton.activeFocus ? 2 : 1
                    }
                    contentItem: Text {
                        text: commandPaletteButton.text; textFormat: Text.PlainText
                        color: Theme.headerText; font.family: Theme.uiFont; font.pixelSize: Theme.uiFontSize(10)
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                }
                Item { Layout.fillWidth: true }

                AppComboBox {
                    id: splitSelector
                    objectName: "splitLayoutSelector"
                    Layout.preferredWidth: 92
                    implicitHeight: 24
                    model: ["Tabs", "2 panes", "4 panes", "6 panes"]
                    currentIndex: [1, 2, 4, 6].indexOf(root.splitCount)
                    minimumPopupWidth: 128
                    hoverEnabled: true
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize(10)
                    // Keep Basic's existing public-theme styling and restore its
                    // bindings when leaving a saturated Cyberpunk header.
                    Binding {
                        target: splitSelector.background
                        property: "color"
                        value: root.buttonSurface(splitSelector, false)
                        when: Theme.cyberpunkChrome
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    Binding {
                        target: splitSelector.background
                        property: "border.color"
                        value: root.buttonBorder(splitSelector, false)
                        when: Theme.cyberpunkChrome
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    Binding {
                        target: splitSelector.contentItem
                        property: "color"
                        value: Theme.headerText
                        when: Theme.cyberpunkChrome
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    indicator: Item {
                        implicitWidth: 12
                        implicitHeight: 8
                        x: splitSelector.mirrored ? 10 : splitSelector.width - width - 10
                        y: (splitSelector.height - height) / 2
                        opacity: splitSelector.enabled ? 1 : 0.4
                        Rectangle { x: 1; y: 3; width: 6; height: 1; rotation: 40; color: Theme.headerTextMuted }
                        Rectangle { x: 5; y: 3; width: 6; height: 1; rotation: -40; color: Theme.headerTextMuted }
                    }
                    onActivated: root.splitCountRequested([1, 2, 4, 6][currentIndex])
                    Accessible.name: "Terminal layout"
                    ToolTip.visible: hovered
                    ToolTip.text: "Choose tabs or a split-terminal layout"
                }

                AppButton {
                    id: appearanceButton
                    objectName: "appearanceToggle"
                    text: ""
                    iconText: ""
                    compact: true
                    implicitWidth: 28
                    implicitHeight: 24
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: root.buttonSurface(appearanceButton, false)
                        border.color: root.buttonBorder(appearanceButton, false)
                        border.width: appearanceButton.activeFocus ? 2 : 1
                    }
                    onClicked: AppSettings.toggleAppearance()
                    Accessible.name: AppSettings.darkAppearance
                                     ? "Switch to day appearance" : "Switch to night appearance"
                    ToolTip.visible: hovered
                    ToolTip.text: "Switch to " + (AppSettings.darkAppearance ? "day" : "night")
                                  + " themes · manual mode"
                    contentItem: Item {
                        implicitWidth: 22
                        implicitHeight: 22

                        Text {
                            objectName: "appearanceSunIcon"
                            anchors.centerIn: parent
                            text: "☀"
                            textFormat: Text.PlainText
                            color: Theme.cyberpunkChrome ? Theme.headerText : Theme.accentBright
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(20)
                            opacity: AppSettings.darkAppearance ? 0 : 1
                            scale: AppSettings.darkAppearance ? 0.45 : 1
                            rotation: AppSettings.darkAppearance ? 85 : 0
                            Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                            Behavior on scale { NumberAnimation { duration: 230; easing.type: Easing.OutBack } }
                            Behavior on rotation { NumberAnimation { duration: 230; easing.type: Easing.OutCubic } }
                        }
                        Text {
                            objectName: "appearanceMoonIcon"
                            anchors.centerIn: parent
                            text: "☾"
                            textFormat: Text.PlainText
                            color: Theme.cyberpunkChrome ? Theme.headerText : Theme.accentBright
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(21)
                            opacity: AppSettings.darkAppearance ? 1 : 0
                            scale: AppSettings.darkAppearance ? 1 : 0.45
                            rotation: AppSettings.darkAppearance ? 0 : -85
                            Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                            Behavior on scale { NumberAnimation { duration: 230; easing.type: Easing.OutBack } }
                            Behavior on rotation { NumberAnimation { duration: 230; easing.type: Easing.OutCubic } }
                        }
                    }
                }
                PanelButton {
                    id: filesButton
                    objectName: "filesToggle"
                    text: "Files"
                    iconText: "▤"
                    implicitHeight: 24
                    implicitWidth: filesContent.implicitWidth + 18
                    panelVisible: root.filesVisible
                    helpText: panelVisible ? "Hide file browser" : "Show file browser"
                    onClicked: root.toggleFiles()
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: root.buttonSurface(filesButton, filesButton.panelVisible)
                        border.color: root.buttonBorder(filesButton, filesButton.panelVisible)
                        border.width: filesButton.activeFocus ? 2 : 1
                    }
                    contentItem: Item {
                        implicitWidth: filesContent.implicitWidth
                        implicitHeight: filesContent.implicitHeight
                        Row {
                            id: filesContent
                            anchors.centerIn: parent
                            spacing: 7
                            Text {
                                text: filesButton.iconText
                                textFormat: Text.PlainText
                                color: Theme.cyberpunkChrome
                                       ? (filesButton.panelVisible ? Theme.headerActiveText : Theme.headerTextMuted)
                                       : Theme.textMuted
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: filesButton.text
                                textFormat: Text.PlainText
                                color: Theme.cyberpunkChrome && filesButton.panelVisible ? Theme.headerActiveText : Theme.headerText
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                AbstractButton {
                    id: profileButton
                    objectName: "profileSettingsButton"
                    implicitHeight: 24
                    implicitWidth: Math.max(64, profileRow.implicitWidth + leftPadding + rightPadding)
                    Layout.leftMargin: 2
                    Layout.rightMargin: 4
                    leftPadding: 7
                    rightPadding: 8
                    topPadding: 2
                    bottomPadding: 2
                    clip: true
                    hoverEnabled: true
                    onClicked: root.settingsRequested("Profile")
                    background: Rectangle {
                        radius: Theme.radius
                        color: profileButton.hovered ? Theme.headerHover : "transparent"
                        border.color: profileButton.activeFocus ? Theme.headerFocus : Theme.headerDivider
                        border.width: profileButton.activeFocus ? 2 : 1
                    }
                    contentItem: RowLayout {
                        id: profileRow
                        spacing: 7
                        ProfileAvatar {
                            objectName: "topBarProfileAvatar"
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }
                        Text {
                            text: PersonalProfile.displayName
                            textFormat: Text.PlainText
                            color: Theme.headerText
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(10)
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.maximumWidth: 120
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Profile and settings"
                }

                Row {
                    Layout.fillHeight: true
                    Layout.rightMargin: 0
                    spacing: 0

                    component CaptionButton: AbstractButton {
                        property string glyph: ""
                        property bool danger: false
                        implicitWidth: 40
                        hoverEnabled: true
                        readonly property color dangerSurface: Theme.cyberpunkChrome ? "#b42339" : Theme.red
                        background: Rectangle {
                            color: parent.down
                                   ? (parent.danger ? Qt.darker(parent.dangerSurface, 1.2) : Theme.headerPressed)
                                   : parent.hovered
                                     ? (parent.danger ? parent.dangerSurface : Theme.headerHover)
                                     : "transparent"
                            border.color: Theme.headerText
                            border.width: Theme.cyberpunkChrome && parent.activeFocus ? 1 : 0
                        }
                        contentItem: Text {
                            text: parent.glyph
                            textFormat: Text.PlainText
                            color: parent.danger && (parent.hovered || parent.down) ? "#ffffff" : Theme.headerText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize(parent.danger ? 14 : 12)
                        }
                    }

                    CaptionButton {
                        objectName: "windowMinimizeButton"
                        glyph: "–"
                        height: parent.height
                        onClicked: if (root.Window.window) root.Window.window.showMinimized()
                        ToolTip.visible: hovered
                        ToolTip.text: "Minimize"
                    }
                    CaptionButton {
                        objectName: "windowMaximizeButton"
                        glyph: root.Window.window && root.Window.window.visibility === Window.Maximized ? "❐" : "□"
                        height: parent.height
                        onClicked: {
                            const window = root.Window.window
                            if (!window)
                                return
                            if (window.visibility === Window.Maximized)
                                window.showNormal()
                            else
                                window.showMaximized()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: root.Window.window && root.Window.window.visibility === Window.Maximized
                                      ? "Restore" : "Maximize"
                    }
                    CaptionButton {
                        objectName: "windowCloseButton"
                        glyph: "×"
                        danger: true
                        height: parent.height
                        onClicked: if (root.Window.window) root.Window.window.close()
                        ToolTip.visible: hovered
                        ToolTip.text: "Close"
                    }
                }
            }
        }
    }
}
