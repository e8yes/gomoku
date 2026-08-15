import QtQuick
import "../theme"

Rectangle {
    id: root

    property var boardModel: null
    property bool interactive: true
    property int ghostColor: 1

    signal cellClicked(int x, int y)

    color: GomokuTheme.boardBackground
    radius: GomokuTheme.radiusMedium
    border.color: GomokuTheme.boardBorder
    border.width: 2

    // Maintain aspect ratio
    implicitWidth: 600
    implicitHeight: 600

    readonly property var colLabels: ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O"]
    readonly property var rowLabels: ["15", "14", "13", "12", "11", "10", "9", "8", "7", "6", "5", "4", "3", "2", "1"]

    readonly property real marginSize: Math.max(20, Math.min(width, height) * 0.05)
    readonly property real gridAreaSize: Math.min(width, height) - (2 * marginSize)
    readonly property real cellSize: gridAreaSize / 15

    // Coordinate labels: Top
    Row {
        anchors.top: parent.top
        anchors.topMargin: (root.marginSize - 16) / 2
        anchors.left: gridContainer.left
        anchors.right: gridContainer.right
        height: root.marginSize

        Repeater {
            model: root.colLabels
            Item {
                width: root.cellSize
                height: parent.height
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: Math.max(9, root.cellSize * 0.35)
                    font.bold: true
                    color: GomokuTheme.boardGridLine
                }
            }
        }
    }

    // Coordinate labels: Bottom
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: (root.marginSize - 16) / 2
        anchors.left: gridContainer.left
        anchors.right: gridContainer.right
        height: root.marginSize

        Repeater {
            model: root.colLabels
            Item {
                width: root.cellSize
                height: parent.height
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: Math.max(9, root.cellSize * 0.35)
                    font.bold: true
                    color: GomokuTheme.boardGridLine
                }
            }
        }
    }

    // Coordinate labels: Left
    Column {
        anchors.left: parent.left
        anchors.leftMargin: (root.marginSize - 16) / 2
        anchors.top: gridContainer.top
        anchors.bottom: gridContainer.bottom
        width: root.marginSize

        Repeater {
            model: root.rowLabels
            Item {
                width: parent.width
                height: root.cellSize
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: Math.max(9, root.cellSize * 0.35)
                    font.bold: true
                    color: GomokuTheme.boardGridLine
                }
            }
        }
    }

    // Coordinate labels: Right
    Column {
        anchors.right: parent.right
        anchors.rightMargin: (root.marginSize - 16) / 2
        anchors.top: gridContainer.top
        anchors.bottom: gridContainer.bottom
        width: root.marginSize

        Repeater {
            model: root.rowLabels
            Item {
                width: parent.width
                height: root.cellSize
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: Math.max(9, root.cellSize * 0.35)
                    font.bold: true
                    color: GomokuTheme.boardGridLine
                }
            }
        }
    }

    // Grid Container
    Item {
        id: gridContainer
        anchors.centerIn: parent
        width: root.gridAreaSize
        height: root.gridAreaSize

        // Outer border line enclosing the 15x15 board
        Rectangle {
            anchors.fill: parent
            anchors.margins: root.cellSize / 2
            color: "transparent"
            border.color: GomokuTheme.boardGridLine
            border.width: 1.5
        }

        // 15x15 Grid of intersections
        Grid {
            anchors.fill: parent
            columns: 15
            rows: 15

            Repeater {
                model: root.boardModel

                BoardCell {
                    width: root.cellSize
                    height: root.cellSize
                    cellX: model.cellX
                    cellY: model.cellY
                    stoneColor: model.stoneColor
                    moveNumber: model.moveNumber
                    isLatestMove: model.isLatestMove
                    isWinningFive: model.isWinningFive
                    isGhost: model.isGhost
                    ghostColor: model.ghostColor
                    isStarPoint: model.isStarPoint
                    interactive: root.interactive

                    onClicked: (x, y) => {
                        root.cellClicked(x, y)
                    }

                    onHovered: (x, y) => {
                        if (root.interactive && root.boardModel) {
                            root.boardModel.setGhostCell(x, y, root.ghostColor)
                        }
                    }

                    onUnhovered: {
                        if (root.boardModel) {
                            root.boardModel.clearGhost()
                        }
                    }
                }
            }
        }
    }
}
