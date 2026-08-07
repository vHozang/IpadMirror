pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var appController
    property var settingsObject
    property color accentColor: "#b7f34a"
    signal closeRequested()

    color: "#0d1112"
    border.color: "#334044"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 62
            color: "#121718"
            border.color: "#273034"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                Text {
                    text: "SETTINGS"
                    color: "#eef3eb"
                    font.family: "Bahnschrift"
                    font.pixelSize: 18
                    font.bold: true
                    font.letterSpacing: 1.5
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: closeButton
                    text: "CLOSE"
                    onClicked: root.closeRequested()
                    contentItem: Text {
                        text: closeButton.text
                        color: "#b9c2bd"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "Bahnschrift"
                        font.pixelSize: 10
                        font.bold: true
                    }
                    background: Rectangle { color: closeButton.hovered ? "#252c2f" : "transparent"; radius: 4 }
                }
            }
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            contentHeight: settingsColumn.implicitHeight + 42

            ColumnLayout {
                id: settingsColumn
                width: Math.max(0, root.width - 40)
                x: 20
                y: 18
                spacing: 11

                SectionTitle { text: "CONNECTION" }
                FieldLabel { text: "Mode" }
                ComboBox {
                    id: audioBufferCombo
                    Layout.fillWidth: true
                    model: ["USB Gaming", "AirPlay Wi-Fi"]
                    currentIndex: root.settingsObject.connectionMode
                    onActivated: root.settingsObject.connectionMode = currentIndex
                }

                FieldLabel { text: "UxPlay executable"; visible: root.settingsObject.connectionMode === 1 }
                TextField {
                    Layout.fillWidth: true
                    visible: root.settingsObject.connectionMode === 1
                    text: root.settingsObject.uxplayPath
                    placeholderText: "Auto-detect or C:/msys64/ucrt64/bin/uxplay.exe"
                    onEditingFinished: root.settingsObject.uxplayPath = text
                }

                SectionTitle { text: "VIDEO" }
                FieldLabel { text: "Hardware decoder" }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["Auto", "D3D11", "VideoToolbox"]
                    currentIndex: Math.max(0, model.indexOf(root.settingsObject.hardwareDecoder))
                    onActivated: root.settingsObject.hardwareDecoder = currentText
                }
                CheckBox { text: "Drop stale frames"; checked: root.settingsObject.dropStaleFrames; onToggled: root.settingsObject.dropStaleFrames = checked }
                CheckBox { text: "Gaming Mode"; checked: root.settingsObject.gamingMode; onToggled: root.settingsObject.gamingMode = checked }

                SectionTitle { text: "AUDIO" }
                FieldLabel { text: "Output" }
                ComboBox {
                    id: audioOutput
                    Layout.fillWidth: true
                    model: root.appController.audioDevices
                    textRole: "name"
                    Component.onCompleted: {
                        for (let i = 0; i < count; ++i) {
                            if (textAt(i) === root.settingsObject.audioDevice) currentIndex = i
                        }
                    }
                    onActivated: root.settingsObject.audioDevice = currentIndex === 0 ? "" : currentText
                }
                FieldLabel { text: "Buffer" }
                ComboBox {
                    Layout.fillWidth: true
                    model: [5, 10, 15, 20]
                    currentIndex: model.indexOf(root.settingsObject.audioBufferMs)
                    displayText: currentText + " ms"
                    delegate: ItemDelegate {
                        required property var modelData
                        width: audioBufferCombo.width
                        text: modelData + " ms"
                    }
                    onActivated: root.settingsObject.audioBufferMs = Number(currentText)
                }
                FieldLabel { text: "Windows audio mode" }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["Low latency", "Exclusive", "Safe / compatible"]
                    currentIndex: root.settingsObject.audioMode
                    onActivated: root.settingsObject.audioMode = currentIndex
                }
                CheckBox { text: "Strict sync to video"; checked: root.settingsObject.strictSync; onToggled: root.settingsObject.strictSync = checked }

                SectionTitle { text: "DISPLAY" }
                CheckBox { text: "Maintain aspect ratio"; checked: root.settingsObject.maintainAspectRatio; onToggled: root.settingsObject.maintainAspectRatio = checked }
                CheckBox { text: "Fullscreen on launch"; checked: root.settingsObject.fullscreenOnLaunch; onToggled: root.settingsObject.fullscreenOnLaunch = checked }
                CheckBox { text: "Always on top"; checked: root.settingsObject.alwaysOnTop; onToggled: root.settingsObject.alwaysOnTop = checked }

                SectionTitle { text: "DEBUG" }
                CheckBox { text: "Diagnostics overlay"; checked: root.settingsObject.diagnosticsVisible; onToggled: root.settingsObject.diagnosticsVisible = checked }

                Button {
                    id: applyButton
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    text: "APPLY AND RESTART RECEIVER"
                    onClicked: {
                        root.appController.restart()
                        root.closeRequested()
                    }
                    contentItem: Text {
                        text: applyButton.text
                        color: "#071006"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "Bahnschrift"
                        font.pixelSize: 11
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    background: Rectangle { implicitHeight: 44; color: root.accentColor; radius: 5 }
                }

            }
        }
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: 9
        text: ""
        color: root.accentColor
        font.family: "Bahnschrift"
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 1.5
    }

    component FieldLabel: Text {
        Layout.fillWidth: true
        color: "#7d888d"
        font.family: "Bahnschrift"
        font.pixelSize: 10
        font.letterSpacing: 0.7
    }
}
