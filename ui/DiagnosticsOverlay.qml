pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property var metricsObject
    property color accentColor: "#b7f34a"
    width: 292
    height: diagnostics.implicitHeight + 28
    radius: 6
    color: "#0b0e0fe8"
    border.color: "#334044"
    opacity: visible ? 1 : 0

    Behavior on opacity { NumberAnimation { duration: 140 } }

    GridLayout {
        id: diagnostics
        anchors.fill: parent
        anchors.margins: 14
        columns: 2
        columnSpacing: 18
        rowSpacing: 5

        MetricLabel { text: "PADMIRROR DIAGNOSTICS"; heading: true; Layout.columnSpan: 2 }
        MetricLabel { text: "TRANSPORT" }
        MetricValue { text: root.metricsObject.transport; accent: true }
        MetricLabel { text: "DEVICE" }
        MetricValue { text: root.metricsObject.deviceName }

        Rectangle { Layout.fillWidth: true; Layout.columnSpan: 2; Layout.preferredHeight: 1; color: "#273034" }

        MetricLabel { text: "SOURCE FPS" }
        MetricValue { text: root.metricsObject.sourceFps.toFixed(2) }
        MetricLabel { text: "DECODE FPS" }
        MetricValue { text: root.metricsObject.decodeFps.toFixed(2) }
        MetricLabel { text: "RENDER FPS" }
        MetricValue { text: root.metricsObject.renderFps.toFixed(2) }
        MetricLabel { text: "DROPPED" }
        MetricValue { text: root.metricsObject.droppedFrames }

        Rectangle { Layout.fillWidth: true; Layout.columnSpan: 2; Layout.preferredHeight: 1; color: "#273034" }

        MetricLabel { text: "AUDIO BUFFER" }
        MetricValue { text: root.metricsObject.audioBufferMs.toFixed(1) + " ms"; accent: root.metricsObject.audioBufferMs <= 15 }
        MetricLabel { text: "BACKEND" }
        MetricValue { text: root.metricsObject.audioBackend }
        MetricLabel { text: "UNDERRUNS" }
        MetricValue { text: root.metricsObject.audioUnderruns }
        MetricLabel { text: "RESYNCS" }
        MetricValue { text: root.metricsObject.audioResyncs }
    }

    component MetricLabel: Text {
        property bool heading: false
        color: heading ? root.accentColor : "#778287"
        font.family: "Bahnschrift"
        font.pixelSize: heading ? 12 : 10
        font.bold: heading
        font.letterSpacing: heading ? 1.2 : 0.7
    }

    component MetricValue: Text {
        property bool accent: false
        color: accent ? root.accentColor : "#e7eee7"
        font.family: "Bahnschrift"
        font.pixelSize: 11
        font.bold: true
        elide: Text.ElideRight
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
    }
}
