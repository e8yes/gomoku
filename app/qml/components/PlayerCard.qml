import QtQuick
import "../theme"

Rectangle {
    id: root

    property string seat: "A"
    property string playerName: "Player"
    property string playerType: "Human"
    property int stoneColor: 0  // 0=Pending, 1=Black, 2=White
    property string winRateText: "50.0%"
    property bool isCurrentTurn: false
    property bool isThinking: false

    implicitWidth: 320
    implicitHeight: 90

    radius: GomokuTheme.radiusMedium
    color: isCurrentTurn ? GomokuTheme.surfaceLight : GomokuTheme.surfaceDark
    border.color: isThinking ? GomokuTheme.textWarning : (isCurrentTurn ? GomokuTheme.textAccent : GomokuTheme.border)
    border.width: isCurrentTurn ? 2 : 1

    Row {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingMedium
        spacing: GomokuTheme.spacingMedium

        // Stone / Seat Avatar Icon
        Rectangle {
            width: 50
            height: 50
            radius: 25
            anchors.verticalCenter: parent.verticalCenter
            color: root.stoneColor === 1
                   ? GomokuTheme.blackStone
                   : (root.stoneColor === 2 ? GomokuTheme.whiteStone : GomokuTheme.surfaceHover)
            border.color: GomokuTheme.borderActive
            border.width: 1.5

            Text {
                anchors.centerIn: parent
                text: root.stoneColor === 0 ? root.seat : (root.stoneColor === 1 ? "⚫" : "⚪")
                font.family: GomokuTheme.fontFamily
                font.pixelSize: root.stoneColor === 0 ? 18 : 22
                font.bold: true
                color: root.stoneColor === 2 ? GomokuTheme.whiteStoneText : GomokuTheme.textPrimary
            }
        }

        // Details Column
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 70
            spacing: 4

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: "Seat " + root.seat + ":"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textMuted
                }

                Text {
                    text: root.playerName
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeMedium
                    font.bold: true
                    color: GomokuTheme.textPrimary
                    elide: Text.ElideRight
                    width: parent.width - 150
                }

                Rectangle {
                    visible: root.isThinking
                    width: 76
                    height: 20
                    radius: 4
                    color: "#4A3B18"
                    border.color: GomokuTheme.textWarning

                    Text {
                        anchors.centerIn: parent
                        text: "Thinking..."
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: 10
                        font.bold: true
                        color: GomokuTheme.textWarning
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Text {
                    text: "Win Rate: " + root.winRateText
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    color: GomokuTheme.textSuccess
                }

                Text {
                    text: "(" + root.playerType + ")"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    color: GomokuTheme.textSecondary
                }
            }
        }
    }
}
