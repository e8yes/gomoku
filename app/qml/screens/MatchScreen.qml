import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root

    property var gameController: null
    signal returnToHomeRequested()

    color: GomokuTheme.background

    Row {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingLarge
        spacing: GomokuTheme.spacingLarge

        // Left Pane: 15x15 Gomoku Board (takes remaining space)
        Item {
            width: Math.min(parent.height, parent.width - 360)
            height: parent.height

            GomokuBoard {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height)
                height: width
                boardModel: root.gameController ? root.gameController.boardModel : null
                interactive: root.gameController ? (root.gameController.isHumanTurn && !root.gameController.isGameOver) : false
                ghostColor: root.gameController ? root.gameController.currentStoneToPlace : 1

                onCellClicked: (x, y) => {
                    if (root.gameController) {
                        root.gameController.submitBoardClick(x, y)
                    }
                }
            }
        }

        // Right Pane: Telemetry, Swap2 Controls, History & Match Actions
        Column {
            width: 340
            height: parent.height
            spacing: 10

            // Player Cards
            PlayerCard {
                width: parent.width
                seat: "A"
                playerName: root.gameController ? root.gameController.playerAName : "Player A"
                playerType: root.gameController ? root.gameController.playerAType : "Human"
                stoneColor: root.gameController ? root.gameController.playerAStone : 0
                winRateText: root.gameController ? root.gameController.playerAWinRateText : "50.0%"
                isCurrentTurn: root.gameController ? (root.gameController.currentSeat === "A" && !root.gameController.isGameOver) : false
            }

            PlayerCard {
                width: parent.width
                seat: "B"
                playerName: root.gameController ? root.gameController.playerBName : "Player B"
                playerType: root.gameController ? root.gameController.playerBType : "Human"
                stoneColor: root.gameController ? root.gameController.playerBStone : 0
                winRateText: root.gameController ? root.gameController.playerBWinRateText : "50.0%"
                isCurrentTurn: root.gameController ? (root.gameController.currentSeat === "B" && !root.gameController.isGameOver) : false
            }

            // Win Rate Balance Gauge
            WinRateGauge {
                width: parent.width
                winRate: root.gameController ? root.gameController.playerAWinRate : 0.5
                textA: root.gameController ? ("A: " + root.gameController.playerAWinRateText) : "50.0%"
                textB: root.gameController ? ("B: " + root.gameController.playerBWinRateText) : "50.0%"
            }

            // Game Phase & Prompt Box
            Rectangle {
                width: parent.width
                height: 52
                radius: GomokuTheme.radiusSmall
                color: GomokuTheme.surfaceDark
                border.color: GomokuTheme.border

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    spacing: 2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Phase: " + (root.gameController ? root.gameController.gamePhase : "-")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        font.bold: true
                        color: GomokuTheme.textAccent
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.gameController ? root.gameController.openingPromptText : ""
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: 11
                        color: GomokuTheme.textSecondary
                        elide: Text.ElideRight
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Swap2 Interactive Controls
            Swap2Controls {
                width: parent.width
                phase: root.gameController ? root.gameController.gamePhase : "STANDARD"
                isHumanTurn: root.gameController ? root.gameController.isHumanTurn : false

                onActionTriggered: (actionId) => {
                    if (root.gameController) {
                        root.gameController.submitSwap2Action(actionId)
                    }
                }
            }

            // Action History List
            ActionHistoryView {
                width: parent.width
                height: Math.max(120, parent.height - 350)
                moveHistoryModel: root.gameController ? root.gameController.moveHistoryModel : null
            }

            // Bottom Action Controls
            Row {
                width: parent.width
                spacing: GomokuTheme.spacingSmall

                Rectangle {
                    width: parent.width / 2 - 4
                    height: 36
                    radius: GomokuTheme.radiusSmall
                    color: resignMouse.containsMouse ? "#C94A53" : GomokuTheme.textDanger
                    visible: root.gameController ? !root.gameController.isGameOver : false

                    Text {
                        anchors.centerIn: parent
                        text: "Resign 🏳️"
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        font.bold: true
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        id: resignMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.gameController) {
                                root.gameController.resignMatch()
                            }
                        }
                    }
                }

                Rectangle {
                    width: root.gameController && !root.gameController.isGameOver ? (parent.width / 2 - 4) : parent.width
                    height: 36
                    radius: GomokuTheme.radiusSmall
                    color: homeMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceDark
                    border.color: GomokuTheme.borderActive

                    Text {
                        anchors.centerIn: parent
                        text: "Return to Home 🏠"
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textPrimary
                    }

                    MouseArea {
                        id: homeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.gameController) {
                                root.gameController.abortMatch()
                            }
                            root.returnToHomeRequested()
                        }
                    }
                }
            }
        }
    }

    // Match Result Banner Modal
    Rectangle {
        anchors.centerIn: parent
        width: 380
        height: 180
        radius: GomokuTheme.radiusLarge
        color: GomokuTheme.surfaceLight
        border.color: GomokuTheme.winningHighlight
        border.width: 2
        visible: root.gameController ? root.gameController.isGameOver : false

        Column {
            anchors.centerIn: parent
            spacing: 12

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "🏆 Match Finished!"
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeTitle
                font.bold: true
                color: GomokuTheme.winningHighlight
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.gameController ? root.gameController.gameResultText : ""
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeMedium
                font.bold: true
                color: GomokuTheme.textPrimary
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Reason: " + (root.gameController ? root.gameController.terminationReason : "")
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                color: GomokuTheme.textSecondary
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 140
                height: 36
                radius: GomokuTheme.radiusSmall
                color: modalHomeMouse.containsMouse ? "#4D94DB" : GomokuTheme.textAccent

                Text {
                    anchors.centerIn: parent
                    text: "Main Menu"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: "#FFFFFF"
                }

                MouseArea {
                    id: modalHomeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.returnToHomeRequested()
                    }
                }
            }
        }
    }
}
