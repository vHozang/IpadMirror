pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    property color accentColor: "#b7f34a"

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#20282b99"
        border.width: 1
    }

    Repeater {
        model: 4
        Item {
            id: corner
            required property int index
            width: 38
            height: 38
            x: corner.index % 2 === 0 ? 16 : root.width - width - 16
            y: corner.index < 2 ? 16 : root.height - height - 16

            Rectangle {
                width: 28
                height: 2
                color: root.accentColor
                anchors.left: parent.left
                anchors.leftMargin: corner.index % 2 === 0 ? 0 : 10
                anchors.top: parent.top
                anchors.topMargin: corner.index < 2 ? 0 : 36
            }
            Rectangle {
                width: 2
                height: 28
                color: root.accentColor
                anchors.left: parent.left
                anchors.leftMargin: corner.index % 2 === 0 ? 0 : 36
                anchors.top: parent.top
                anchors.topMargin: corner.index < 2 ? 0 : 10
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 220
        height: 2
        color: root.accentColor
        opacity: 0.7
    }
}
