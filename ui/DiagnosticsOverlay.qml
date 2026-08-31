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
        MetricLabel { text: "STATE" }
        MetricValue { text: root.metricsObject.transportState }
        MetricLabel { text: "DEVICE" }
        MetricValue { text: root.metricsObject.deviceName }
        MetricLabel { text: "REQUESTED" }
        MetricValue { text: root.metricsObject.requestedQuality }
        MetricLabel { text: "ACTUAL VIDEO" }
        MetricValue { text: root.metricsObject.videoCodec + " / " + root.metricsObject.sourceResolution; accent: root.metricsObject.videoCodec.indexOf("HEVC") >= 0 }
        MetricLabel { text: "EPOCH" }
        MetricValue { text: root.metricsObject.activeEpoch }
        MetricLabel { text: "LAST FLUSH"; visible: root.metricsObject.lastDiscontinuity.length > 0 }
        MetricValue { text: root.metricsObject.lastDiscontinuity; visible: root.metricsObject.lastDiscontinuity.length > 0 }
        MetricLabel { text: "HEALTH"; visible: root.metricsObject.healthDetail.length > 0 }
        MetricValue { text: root.metricsObject.healthDetail; visible: root.metricsObject.healthDetail.length > 0 }
        MetricLabel { text: "QUALITY"; visible: root.metricsObject.qualityStatus.length > 0 }
        MetricValue { text: root.metricsObject.qualityStatus; visible: root.metricsObject.qualityStatus.length > 0 }

        Rectangle { Layout.fillWidth: true; Layout.columnSpan: 2; Layout.preferredHeight: 1; color: "#273034" }

        MetricLabel { text: "SOURCE FPS" }
        MetricValue { text: root.metricsObject.sourceFps.toFixed(2) }
        MetricLabel { text: "DECODE FPS" }
        MetricValue { text: root.metricsObject.decodeFps.toFixed(2) }
        MetricLabel { text: "OUTPUT FPS" }
        MetricValue { text: root.metricsObject.renderFps.toFixed(2) }
        MetricLabel { text: "FRAMES S/D/R" }
        MetricValue {
            text: root.metricsObject.sourceFrames + " / " +
                  root.metricsObject.decodedFrames + " / " +
                  root.metricsObject.renderedFrames
        }
        MetricLabel { text: "DROPPED" }
        MetricValue { text: root.metricsObject.droppedFrames }

        Rectangle { Layout.fillWidth: true; Layout.columnSpan: 2; Layout.preferredHeight: 1; color: "#273034" }

        MetricLabel { text: "AUDIO BUFFER" }
        MetricValue {
            text: root.metricsObject.audioBufferValid ? root.metricsObject.audioBufferMs.toFixed(1) + " ms" : "--"
            accent: root.metricsObject.audioBufferValid && root.metricsObject.audioBufferMs <= 35
        }
        MetricLabel { text: "A/V OFFSET" }
        MetricValue {
            text: root.metricsObject.avSyncValid ? root.metricsObject.avSyncOffsetMs.toFixed(1) + " ms" : "--"
            accent: root.metricsObject.avSyncValid && Math.abs(root.metricsObject.avSyncOffsetMs) <= 100
        }
        MetricLabel { text: "AUDIO PACKETS" }
        MetricValue { text: root.metricsObject.audioPackets }
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
