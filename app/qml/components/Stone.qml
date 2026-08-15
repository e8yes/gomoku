import QtQuick
import "../theme"

Item {
    id: root

    property int stoneColor: 0  // 0=Empty, 1=Black, 2=White
    property int moveNumber: 0
    property bool isLatest: false
    property bool isWinning: false
    property bool isGhost: false
    property int ghostColor: 0

    visible: stoneColor !== 0 || isGhost

    readonly property int effectiveColor: isGhost ? ghostColor : stoneColor
    readonly property bool isBlack: effectiveColor === 1

    // Stone body
    Rectangle {
        id: body
        anchors.fill: parent
        anchors.margins: Math.max(2, parent.width * 0.06)
        radius: width / 2
        opacity: root.isGhost ? 0.45 : 1.0

        color: root.isBlack ? GomokuTheme.blackStone : GomokuTheme.whiteStone
        border.color: root.isBlack ? "#111115" : GomokuTheme.whiteStoneShadow
        border.width: Math.max(1, width * 0.03)

        // Specular highlight highlight for 3D sphere look
        Rectangle {
            width: parent.width * 0.35
            height: width
            radius: width / 2
            x: parent.width * 0.2
            y: parent.height * 0.18
            color: "#FFFFFF"
            opacity: root.isBlack ? 0.18 : 0.45
        }

        // Winning stone highlight ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: width / 2
            color: "transparent"
            border.color: GomokuTheme.winningHighlight
            border.width: 3
            visible: root.isWinning
        }

        // Latest move indicator marker
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.28
            height: width
            radius: width / 2
            color: root.isBlack ? GomokuTheme.accentLatestMove : GomokuTheme.accentLatestMove
            visible: root.isLatest && root.moveNumber === 0 && !root.isGhost
        }

        // Move number text
        Text {
            anchors.centerIn: parent
            text: root.moveNumber > 0 ? root.moveNumber.toString() : ""
            visible: root.moveNumber > 0 && !root.isGhost
            font.pixelSize: Math.max(9, parent.width * 0.42)
            font.family: GomokuTheme.fontFamily
            font.bold: true
            color: root.isLatest
                   ? GomokuTheme.accentLatestMove
                   : (root.isBlack ? GomokuTheme.blackStoneText : GomokuTheme.whiteStoneText)
        }
    }
}
