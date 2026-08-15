import QtQuick
import QtQuick.Controls
import "../theme"
import "../components"

Rectangle {
    id: root

    property var gameController: null
    property var pluginRegistry: null
    property var replayController: null

    signal startMatchRequested(string playerA, string playerB, int diffA, int diffB)
    signal replayMatchRequested(var matchId)

    color: GomokuTheme.background

    Column {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingLarge
        spacing: GomokuTheme.spacingMedium

        // Header Title
        Item {
            width: parent.width
            height: 40

            Text {
                text: "⚫⚪ Gomoku Swap2 Arena"
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeTitle
                font.bold: true
                color: GomokuTheme.textPrimary
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 130
                height: 32
                radius: GomokuTheme.radiusSmall
                color: rescanMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceDark
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Rescan Plugins 🔌"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: rescanMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (root.pluginRegistry) {
                            root.pluginRegistry.rescanPlugins()
                        }
                    }
                }
            }
        }

        // Match Setup Card
        Rectangle {
            width: parent.width
            height: 160
            radius: GomokuTheme.radiusMedium
            color: GomokuTheme.surfaceDark
            border.color: GomokuTheme.border
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: GomokuTheme.spacingMedium
                spacing: GomokuTheme.spacingMedium

                Text {
                    text: "Match Configuration"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeMedium
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                Row {
                    width: parent.width
                    spacing: GomokuTheme.spacingLarge

                    // Seat A Selector
                    Column {
                        spacing: 6
                        width: 220

                        Text {
                            text: "Seat A (Opener)"
                            font.family: GomokuTheme.fontFamily
                            font.pixelSize: GomokuTheme.fontSizeSmall
                            color: GomokuTheme.textSecondary
                        }

                        ComboBox {
                            id: comboSeatA
                            width: parent.width
                            height: 36
                            model: root.pluginRegistry ? root.pluginRegistry.availableEngines : ["Human"]
                            currentIndex: 0
                        }
                    }

                    // Seat B Selector
                    Column {
                        spacing: 6
                        width: 220

                        Text {
                            text: "Seat B (Responder)"
                            font.family: GomokuTheme.fontFamily
                            font.pixelSize: GomokuTheme.fontSizeSmall
                            color: GomokuTheme.textSecondary
                        }

                        ComboBox {
                            id: comboSeatB
                            width: parent.width
                            height: 36
                            model: root.pluginRegistry ? root.pluginRegistry.availableEngines : ["Human"]
                            currentIndex: 0
                        }
                    }

                    // Difficulty Selector
                    Column {
                        spacing: 6
                        width: 180

                        Text {
                            text: "AI Difficulty Level"
                            font.family: GomokuTheme.fontFamily
                            font.pixelSize: GomokuTheme.fontSizeSmall
                            color: GomokuTheme.textSecondary
                        }

                        ComboBox {
                            id: comboDifficulty
                            width: parent.width
                            height: 36
                            model: ["Apprentice (0)", "Casual (1)", "Club (2)", "Veteran (3)", "Champion (4)", "Truth (5)"]
                            currentIndex: 3
                        }
                    }

                    // Start Match Button
                    Rectangle {
                        width: 150
                        height: 44
                        radius: GomokuTheme.radiusMedium
                        color: startBtnMouse.containsMouse ? "#4D94DB" : GomokuTheme.textAccent
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            anchors.centerIn: parent
                            text: "Start Match ▶"
                            font.family: GomokuTheme.fontFamily
                            font.pixelSize: GomokuTheme.fontSizeMedium
                            font.bold: true
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            id: startBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                var pA = comboSeatA.currentText
                                var pB = comboSeatB.currentText
                                var diff = comboDifficulty.currentIndex
                                root.startMatchRequested(pA, pB, diff, diff)
                            }
                        }
                    }
                }
            }
        }

        // Past Matches Table
        MatchHistoryTable {
            width: parent.width
            height: parent.height - 240
            matchHistoryModel: root.gameController ? root.gameController.matchHistoryModel : null

            onReplayRequested: (matchId) => {
                root.replayMatchRequested(matchId)
            }
        }
    }
}
