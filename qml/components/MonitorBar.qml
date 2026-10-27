pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

Rectangle {
    id: root
    objectName: "monitorBar"
    property var profile: ({})
    property bool monitoring: false
    property bool sessionRunning: false
    signal closeRequested()
    implicitHeight: 34
    color: Theme.surfaceRaised
    border.color: Theme.borderSoft
    HostMonitor {
        id: monitor
        objectName: "hostMonitor"
        profile: root.profile
        enabled: root.monitoring && root.sessionRunning
    }
    readonly property var m: monitor.metrics
    function diskLabels() {
        const rows = []
        for (const disk of (m.disks || [])) {
            const used = Number(disk.usedGB)
            rows.push(String(disk.mount) + " "
                      + (used >= 10 ? used.toFixed(0) : used.toFixed(1))
                      + " GB · " + Number(disk.percent).toFixed(0) + "%")
        }
        if (!rows.length) rows.push("/ " + (m.disk || "—"))
        return rows
    }
    readonly property var labels: Object.keys(m).length === 0
        ? [monitor.status || "Waiting for session…"]
        : ["CPU " + (m.cpuPercent < 0 ? "…" : Number(m.cpuPercent).toFixed(0) + "%"),
           "RAM " + Number(m.memoryPercent).toFixed(0) + "%",
           "NET ↓" + Number(m.rxKB).toFixed(1) + " ↑" + Number(m.txKB).toFixed(1) + " KB/s",
           "Load " + (m.load || "—")].concat(diskLabels()).concat([
           "Up " + Math.floor(Number(m.uptime) / 3600) + "h"])
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 5; spacing: 8
        Flickable {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: values.implicitWidth; contentHeight: height
            clip: true; boundsBehavior: Flickable.StopAtBounds
            Row {
                id: values; height: parent.height; spacing: 18
                Repeater {
                    model: root.labels
                    delegate: Text { textFormat: Text.PlainText;
                        required property string modelData
                        height: values.height; verticalAlignment: Text.AlignVCenter
                        text: modelData; color: Theme.textMuted
                        font.family: Theme.monoFont; font.pixelSize: Theme.uiFontSize(10)
                    }
                }
            }
        }
        AppButton {
            objectName: "hideMonitoringButton"
            text: "×"; compact: true; implicitWidth: 26
            onClicked: root.closeRequested()
            ToolTip.visible: hovered
            ToolTip.text: Theme.tooltip("Stop monitoring · " + (root.m.host || "") + "\n" + monitor.status)
        }
    }
}
