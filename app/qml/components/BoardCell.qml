import QtQuick
import "../theme"

Item {
    id: root

    property int cellX: 0
    property int cellY: 0
    property int stoneColor: 0
    property int moveNumber: 0
    property bool isLatestMove: false
    property bool isWinningFive: false
    property bool isGhost: false
    property int ghostColor: 0
    property bool isStarPoint: false
    property bool interactive: true

    signal clicked(int x, int y)
    signal hovered(int x, int y)
    signal unhovered()

    // Horizontal grid line
    Rectangle {
        y: Math.floor(parent.height / 2)
        height: 1
        color: GomokuTheme.boardGridLine
        x: root.cellX === 0 ? parent.width / 2 : 0
        width: root.cellX === 0 ? parent.width / 2 : (root.cellX === 14 ? parent.width / 2 : parent.width)
    }

    // Vertical grid line
    Rectangle {
        x: Math.floor(parent.width / 2)
        width: 1
        color: GomokuTheme.boardGridLine
        y: root.cellY === 0 ? parent.height / 2 : 0
        height: root.cellY === 0 ? parent.height / 2 : (root.cellY === 14 ? parent.height / 2 : parent.height)
    }

    // Star point dot
    Rectangle {
        anchors.centerIn: parent
        width: Math.max(5, parent.width * 0.16)
        height: width
        radius: width / 2
        color: GomokuTheme.starPoint
        visible: root.isStarPoint && root.stoneColor === 0 && !root.isGhost
    }

    // Stone component
    Stone {
        anchors.fill: parent
        stoneColor: root.stoneColor
        moveNumber: root.moveNumber
        isLatest: root.isLatestMove
        isWinning: root.isWinningFive
        isGhost: root.isGhost
        ghostColor: root.ghostColor
    }

    // Interactive mouse area
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.interactive

        onClicked: {
            if (root.stoneColor === 0) {
                root.clicked(root.cellX, root.cellY)
            }
        }

        onEntered: {
            if (root.stoneColor === 0) {
                root.hovered(root.cellX, root.cellY)
            }
        }

        onExited: {
            root.unhovered()
        }
    }
}
