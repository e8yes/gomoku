import QtQuick
import "../theme"
import "../components"

Rectangle {
    id: root

    property var replayController: null
    signal returnToHomeRequested()

    color: GomokuTheme.background

    Row {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingLarge
        spacing: GomokuTheme.spacingLarge

        // Left Pane: Board + Replay Controls directly below
        Column {
            width: Math.min(parent.height, parent.width - 360)
            height: parent.height
            spacing: GomokuTheme.spacingSmall

            GomokuBoard {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, parent.height - 110)
                height: width
                boardModel: root.replayController ? root.replayController.boardModel : null
                interactive: false
            }

            // Replay Timeline Navigation Bar (Directly below board)
            ReplayControls {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, parent.height - 110)
                totalPlies: root.replayController ? root.replayController.totalPlies : 0
                currentPly: root.replayController ? root.replayController.currentPly : 0
                isPlaying: root.replayController ? root.replayController.isPlaying : false
                currentActionLabel: root.replayController ? root.replayController.currentActionLabel : ""
                currentWinRateText: root.replayController ? root.replayController.currentWinRateText : "-"

                onJumpToStart: if (root.replayController) root.replayController.jumpToStart()
                onStepBackward: if (root.replayController) root.replayController.stepBackward()
                onTogglePlay: if (root.replayController) root.replayController.togglePlay()
                onStepForward: if (root.replayController) root.replayController.stepForward()
                onJumpToEnd: if (root.replayController) root.replayController.jumpToEnd()
                onSeekToPly: (ply) => if (root.replayController) root.replayController.seekToPly(ply)
                onSpeedSelected: (ms) => if (root.replayController) root.replayController.setPlaybackSpeed(ms)
            }
        }

        // Right Pane: Match Details & Move History Log
        Column {
            width: 340
            height: parent.height
            spacing: 12

            // Match Info Card
            Rectangle {
                width: parent.width
                height: 140
                radius: GomokuTheme.radiusMedium
                color: GomokuTheme.surfaceDark
                border.color: GomokuTheme.border

                Column {
                    anchors.fill: parent
                    anchors.margins: GomokuTheme.spacingMedium
                    spacing: 6

                    Text {
                        text: "Replay Details"
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeMedium
                        font.bold: true
                        color: GomokuTheme.textPrimary
                    }

                    Text {
                        text: "Seat A (Opener): " + (root.replayController ? root.replayController.playerAName : "-")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Text {
                        text: "Seat B (Responder): " + (root.replayController ? root.replayController.playerBName : "-")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Text {
                        text: "Outcome: " + (root.replayController ? root.replayController.resultText : "-")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        font.bold: true
                        color: GomokuTheme.textSuccess
                    }

                    Text {
                        text: "Reason: " + (root.replayController ? root.replayController.terminationReason : "-")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textMuted
                    }
                }
            }

            // Action History
            ActionHistoryView {
                width: parent.width
                height: parent.height - 200
                moveHistoryModel: root.replayController ? root.replayController.moveHistoryModel : null
            }

            // Return to Home
            Rectangle {
                width: parent.width
                height: 40
                radius: GomokuTheme.radiusSmall
                color: homeBtnMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceDark
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Return to Home 🏠"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: homeBtnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (root.replayController) {
                            root.replayController.pause()
                        }
                        root.returnToHomeRequested()
                    }
                }
            }
        }
    }
}
