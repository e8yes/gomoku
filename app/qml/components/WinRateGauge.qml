import QtQuick
import "../theme"

Item {
    id: root

    property real winRate: 0.5  // 0.0 to 1.0 (Player A win rate)
    property string textA: "50.0%"
    property string textB: "50.0%"

    implicitWidth: 260
    implicitHeight: 28

    Rectangle {
        anchors.fill: parent
        radius: GomokuTheme.radiusSmall
        color: GomokuTheme.surfaceDark
        border.color: GomokuTheme.border
        border.width: 1
        clip: true

        // Player A (Black side) bar
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * Math.max(0.0, Math.min(1.0, root.winRate))
            color: "#3B4252"

            Behavior on width {
                NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
            }
        }

        // Center divider line
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: parent.height
            color: GomokuTheme.borderActive
        }

        // Percentage texts
        Row {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Text {
                width: parent.width / 2 - 8
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignLeft
                text: root.textA
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                font.bold: true
                color: GomokuTheme.textPrimary
            }

            Text {
                width: parent.width / 2 - 8
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                text: root.textB
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                font.bold: true
                color: GomokuTheme.textPrimary
            }
        }
    }
}
