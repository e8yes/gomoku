import QtQuick
import "../theme"

Rectangle {
    id: root

    property string phase: "STANDARD"
    property bool isHumanTurn: false

    signal actionTriggered(int actionId)

    visible: (phase === "SWAP2_DECISION" || phase === "CHOOSE_COLOR") && isHumanTurn

    implicitWidth: 320
    implicitHeight: contentColumn.height + 24
    radius: GomokuTheme.radiusMedium
    color: GomokuTheme.surfaceDark
    border.color: GomokuTheme.textWarning
    border.width: 1.5

    Column {
        id: contentColumn
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 8

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.phase === "SWAP2_DECISION" ? "Swap2 Decision Required" : "Choose Final Color"
            font.family: GomokuTheme.fontFamily
            font.pixelSize: GomokuTheme.fontSizeMedium
            font.bold: true
            color: GomokuTheme.textWarning
        }

        // Decision buttons for SWAP2_DECISION
        Column {
            width: parent.width
            spacing: 6
            visible: root.phase === "SWAP2_DECISION"

            Rectangle {
                width: parent.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: chooseWhiteMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Choose White (A keeps Black)"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: chooseWhiteMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionTriggered(225)
                }
            }

            Rectangle {
                width: parent.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: chooseBlackMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Choose Black / Swap (A takes White)"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: chooseBlackMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionTriggered(226)
                }
            }

            Rectangle {
                width: parent.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: placeTwoMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Place 2 More Stones (W-B)"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: placeTwoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionTriggered(227)
                }
            }
        }

        // Decision buttons for CHOOSE_COLOR
        Column {
            width: parent.width
            spacing: 6
            visible: root.phase === "CHOOSE_COLOR"

            Rectangle {
                width: parent.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: chooseWhiteAfterTwoMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Take White (Seat B takes Black)"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: chooseWhiteAfterTwoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionTriggered(228)
                }
            }

            Rectangle {
                width: parent.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: chooseBlackAfterTwoMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Take Black (Seat B takes White)"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    font.bold: true
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: chooseBlackAfterTwoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionTriggered(229)
                }
            }
        }
    }
}
