import QtQuick
import "../theme"

Rectangle {
    id: root

    property int totalPlies: 0
    property int currentPly: 0
    property bool isPlaying: false
    property string currentWinRateText: "-"
    property string currentActionLabel: ""

    signal jumpToStart()
    signal stepBackward()
    signal togglePlay()
    signal stepForward()
    signal jumpToEnd()
    signal seekToPly(int ply)
    signal speedSelected(int ms)

    implicitWidth: 600
    implicitHeight: 100
    radius: GomokuTheme.radiusMedium
    color: GomokuTheme.surfaceDark
    border.color: GomokuTheme.border
    border.width: 1

    Column {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingSmall
        spacing: 8

        // Top info row: current move & recorded win rate
        Item {
            width: parent.width
            height: 20

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Ply: " + root.currentPly + " / " + root.totalPlies +
                      (root.currentActionLabel.length > 0 ? (" (" + root.currentActionLabel + ")") : "")
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                font.bold: true
                color: GomokuTheme.textPrimary
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.currentWinRateText !== "-" ? ("Recorded Eval: " + root.currentWinRateText) : ""
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                font.bold: true
                color: GomokuTheme.textSuccess
            }
        }

        // Timeline Slider
        Rectangle {
            id: sliderTrack
            width: parent.width
            height: 8
            radius: 4
            color: GomokuTheme.surfaceLight

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: root.totalPlies > 0
                       ? (parent.width * (root.currentPly / root.totalPlies))
                       : 0
                radius: 4
                color: GomokuTheme.textAccent
            }

            Rectangle {
                width: 16
                height: 16
                radius: 8
                color: GomokuTheme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
                x: root.totalPlies > 0
                   ? Math.max(0, Math.min(sliderTrack.width - 16, (sliderTrack.width * (root.currentPly / root.totalPlies)) - 8))
                   : 0
            }

            MouseArea {
                anchors.fill: parent
                onClicked: (mouse) => {
                    if (root.totalPlies > 0) {
                        var target = Math.round((mouse.x / width) * root.totalPlies)
                        root.seekToPly(target)
                    }
                }
            }
        }

        // Control Buttons Row
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: GomokuTheme.spacingSmall

            // Jump to Start
            Rectangle {
                width: 44
                height: 32
                radius: GomokuTheme.radiusSmall
                color: startMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive
                Text { anchors.centerIn: parent; text: "|<"; color: GomokuTheme.textPrimary; font.bold: true }
                MouseArea { id: startMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.jumpToStart() }
            }

            // Step Backward
            Rectangle {
                width: 44
                height: 32
                radius: GomokuTheme.radiusSmall
                color: prevMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive
                Text { anchors.centerIn: parent; text: "<"; color: GomokuTheme.textPrimary; font.bold: true }
                MouseArea { id: prevMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.stepBackward() }
            }

            // Play / Pause
            Rectangle {
                width: 90
                height: 32
                radius: GomokuTheme.radiusSmall
                color: playMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.textAccent
                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "Pause ⏸" : "Play ▶"
                    color: GomokuTheme.textAccent
                    font.bold: true
                }
                MouseArea { id: playMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.togglePlay() }
            }

            // Step Forward
            Rectangle {
                width: 44
                height: 32
                radius: GomokuTheme.radiusSmall
                color: nextMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive
                Text { anchors.centerIn: parent; text: ">"; color: GomokuTheme.textPrimary; font.bold: true }
                MouseArea { id: nextMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.stepForward() }
            }

            // Jump to End
            Rectangle {
                width: 44
                height: 32
                radius: GomokuTheme.radiusSmall
                color: endMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive
                Text { anchors.centerIn: parent; text: ">|"; color: GomokuTheme.textPrimary; font.bold: true }
                MouseArea { id: endMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.jumpToEnd() }
            }
        }
    }
}
