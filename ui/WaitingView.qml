pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool wifiMode: false
    property color accentColor: "#b7f34a"

    Rectangle {
        anchors.fill: parent
        color: "#07090a"

        Repeater {
            model: Math.max(0, Math.ceil(parent.width / 48))
            Rectangle {
                required property int index
                x: index * 48
                width: 1
                height: parent.height
                color: "#121719"
            }
        }
        Repeater {
            model: Math.max(0, Math.ceil(parent.height / 48))
            Rectangle {
                required property int index
                y: index * 48
                width: parent.width
                height: 1
                color: "#121719"
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 680)
        spacing: 20

        Component.onCompleted: reveal.start()
        NumberAnimation on opacity {
            id: reveal
            from: 0
            to: 1
            duration: 420
            easing.type: Easing.OutCubic
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 52
                Layout.preferredHeight: 52
                radius: 8
                color: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.12)
                border.color: root.accentColor

                Text {
                    anchors.centerIn: parent
                    text: root.wifiMode ? "W" : "U"
                    color: root.accentColor
                    font.family: "Bahnschrift"
                    font.pixelSize: 24
                    font.bold: true
                }
            }

            Column {
                Text {
                    text: root.wifiMode ? "AIRPLAY ON LOCAL WI-FI" : "USB GAMING MODE"
                    color: "#eef3eb"
                    font.family: "Bahnschrift"
                    font.pixelSize: 24
                    font.bold: true
                    font.letterSpacing: 1.4
                }
                Text {
                    text: root.wifiMode ? "Convenience fallback" : "Lowest practical latency path"
                    color: "#899399"
                    font.family: "Bahnschrift"
                    font.pixelSize: 12
                    font.letterSpacing: 0.7
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: instructionColumn.implicitHeight + 42
            radius: 8
            color: "#101416ee"
            border.color: "#273034"

            ColumnLayout {
                id: instructionColumn
                anchors.fill: parent
                anchors.margins: 22
                spacing: 13

                Text {
                    Layout.fillWidth: true
                    text: root.wifiMode
                          ? "1  Keep this computer and iPad on the same Wi-Fi network"
                          : "1  Connect the iPad with a USB data cable"
                    color: "#dce3dc"
                    font.family: "Bahnschrift"
                    font.pixelSize: 14
                }
                Text {
                    Layout.fillWidth: true
                    text: root.wifiMode
                          ? "2  Open Control Center, tap Screen Mirroring"
                          : "2  Unlock the iPad and tap Trust if prompted"
                    color: "#dce3dc"
                    font.family: "Bahnschrift"
                    font.pixelSize: 14
                }
                Text {
                    Layout.fillWidth: true
                    text: root.wifiMode
                          ? "3  Select PadMirror"
                          : "3  Capture starts automatically"
                    color: "#dce3dc"
                    font.family: "Bahnschrift"
                    font.pixelSize: 14
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#273034" }

                Text {
                    Layout.fillWidth: true
                    text: root.wifiMode
                          ? (app.wifiAvailable
                             ? "Receiver ready at " + (app.wifiAddresses.length ? app.wifiAddresses.join(" / ") : "local network")
                             : "UxPlay is not installed or its path is not configured")
                          : (devices.hasDevice
                             ? devices.currentName + (devices.trustKnown && !devices.currentTrusted ? " - waiting for Trust" : " - detected")
                             : "No iPad detected")
                    color: root.wifiMode && !app.wifiAvailable ? "#ff776b" : root.accentColor
                    font.family: "Bahnschrift"
                    font.pixelSize: 12
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        Button {
            id: receiverButton
            Layout.alignment: Qt.AlignHCenter
            text: app.active ? "STOP RECEIVER" : "START RECEIVER"
            onClicked: app.active ? app.stop() : app.start()

            contentItem: Text {
                text: receiverButton.text
                color: "#071006"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: "Bahnschrift"
                font.pixelSize: 12
                font.bold: true
                font.letterSpacing: 1
            }
            background: Rectangle {
                implicitWidth: 170
                implicitHeight: 42
                radius: 5
                color: root.accentColor
                opacity: receiverButton.down ? 0.7 : 1
            }
        }
    }
}
