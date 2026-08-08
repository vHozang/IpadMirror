pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    focus: true

    readonly property color ink: "#07090a"
    readonly property color panel: "#101416"
    readonly property color line: "#273034"
    readonly property color lime: "#b7f34a"
    readonly property color cyan: "#45d7e8"
    readonly property color textMain: "#eef3eb"
    readonly property color textMuted: "#899399"
    property bool settingsOpen: false

    Rectangle {
        anchors.fill: parent
        color: root.ink
    }

    PlayerView {
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: app.streaming
        accentColor: root.lime
    }

    WaitingView {
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !app.streaming
        wifiMode: settings.connectionMode === 1
        accentColor: settings.connectionMode === 1 ? root.cyan : root.lime
    }

    Rectangle {
        id: header
        z: 20
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: app.fullscreen ? 0 : 52
        color: "#0c0f10f4"
        border.color: root.line
        clip: true

        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: app.beginWindowMove()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 9
                Layout.preferredHeight: 24
                radius: 2
                color: settings.connectionMode === 1 ? root.cyan : root.lime
            }

            Text {
                text: "PADMIRROR"
                color: root.textMain
                font.family: "Bahnschrift"
                font.pixelSize: 18
                font.bold: true
                font.letterSpacing: 1.8
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: root.line }

            Text {
                text: settings.gamingMode ? "GAMING / LOW BUFFER" : "MEDIA MODE"
                color: root.textMuted
                font.family: "Bahnschrift"
                font.pixelSize: 11
                font.letterSpacing: 1.2
            }

            Item { Layout.fillWidth: true }

            HeaderButton {
                label: "SETTINGS"
                onClicked: root.settingsOpen = !root.settingsOpen
            }
            HeaderButton { label: "_"; compact: true; onClicked: app.minimizeWindow() }
            HeaderButton { label: "[]"; compact: true; onClicked: app.toggleMaximize() }
            HeaderButton { label: "X"; compact: true; danger: true; onClicked: app.closeWindow() }
        }
    }

    Rectangle {
        id: footer
        z: 20
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: app.fullscreen ? 44 : 58
        color: "#0c0f10f4"
        border.color: root.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            spacing: 10

            StatusPill {
                label: settings.gamingMode ? "GAME" : "MEDIA"
                active: settings.gamingMode
                accent: root.lime
            }
            StatusPill {
                label: app.connectionLabel
                active: app.active
                accent: settings.connectionMode === 1 ? root.cyan : root.lime
            }
            StatusPill {
                label: metrics.sourceFps > 1
                       ? Math.round(metrics.sourceFps) + " FPS"
                       : (app.streaming ? "IDLE" : "-- FPS")
                active: metrics.sourceFps > 50
                accent: root.lime
            }

            Text {
                Layout.fillWidth: true
                text: app.errorText.length > 0 ? app.errorText : app.statusText
                color: app.errorText.length > 0 ? "#ff776b" : root.textMuted
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: "Bahnschrift"
                font.pixelSize: 12
            }

            HeaderButton {
                label: settings.diagnosticsVisible ? "DIAG ON" : "DIAG"
                onClicked: settings.diagnosticsVisible = !settings.diagnosticsVisible
            }
            HeaderButton {
                label: app.fullscreen ? "WINDOW" : "FULLSCREEN"
                onClicked: app.toggleFullscreen()
            }
        }
    }

    DiagnosticsOverlay {
        z: 30
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.margins: 16
        visible: settings.diagnosticsVisible
        metricsObject: metrics
        accentColor: settings.connectionMode === 1 ? root.cyan : root.lime
    }

    Rectangle {
        z: 39
        anchors.fill: parent
        color: "#00000088"
        visible: root.settingsOpen
        MouseArea { anchors.fill: parent; onClicked: root.settingsOpen = false }
    }

    SettingsView {
        z: 40
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(460, parent.width * 0.48)
        visible: root.settingsOpen
        appController: app
        settingsObject: settings
        accentColor: settings.connectionMode === 1 ? root.cyan : root.lime
        onCloseRequested: root.settingsOpen = false
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_F11) {
            app.toggleFullscreen()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && root.settingsOpen) {
            root.settingsOpen = false
            event.accepted = true
        }
    }

    component HeaderButton: Button {
        id: button
        property string label
        property bool compact: false
        property bool danger: false
        implicitWidth: compact ? 38 : Math.max(76, contentItem.implicitWidth + 24)
        implicitHeight: 34
        hoverEnabled: true

        contentItem: Text {
            text: button.label
            color: button.danger && button.hovered ? "#ffffff" : root.textMain
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: "Bahnschrift"
            font.pixelSize: 11
            font.bold: true
            font.letterSpacing: 0.8
        }
        background: Rectangle {
            radius: 4
            color: button.danger && button.hovered
                   ? "#c9473d"
                   : button.hovered ? "#22292c" : "transparent"
            border.color: button.hovered ? root.line : "transparent"
        }
    }

    component StatusPill: Rectangle {
        property string label
        property bool active
        property color accent
        implicitWidth: pillText.implicitWidth + 24
        implicitHeight: 28
        radius: 3
        color: active ? Qt.rgba(accent.r, accent.g, accent.b, 0.12) : "#151a1c"
        border.color: active ? accent : root.line

        Text {
            id: pillText
            anchors.centerIn: parent
            text: parent.label
            color: parent.active ? parent.accent : root.textMuted
            font.family: "Bahnschrift"
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1
        }
    }
}
