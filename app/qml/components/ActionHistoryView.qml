import QtQuick
import "../theme"

Rectangle {
    id: root

    property var moveHistoryModel: null

    radius: GomokuTheme.radiusMedium
    color: GomokuTheme.surfaceDark
    border.color: GomokuTheme.border
    border.width: 1

    Column {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingSmall
        spacing: 6

        Text {
            text: "Action History"
            font.family: GomokuTheme.fontFamily
            font.pixelSize: GomokuTheme.fontSizeSmall
            font.bold: true
            color: GomokuTheme.textMuted
            leftPadding: 4
        }

        ListView {
            id: listView
            width: parent.width
            height: parent.height - 24
            clip: true
            model: root.moveHistoryModel

            delegate: Rectangle {
                width: listView.width
                height: 28
                radius: GomokuTheme.radiusSmall
                color: index % 2 === 0 ? "transparent" : GomokuTheme.surfaceLight

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        width: 28
                        anchors.verticalCenter: parent.verticalCenter
                        text: "#" + model.plyIndex
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Text {
                        width: 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.stonePlaced === "BLACK" ? "⚫" : (model.stonePlaced === "WHITE" ? "⚪" : "⚙️")
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                    }

                    Text {
                        width: 100
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.actionLabel
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        font.bold: true
                        color: GomokuTheme.textPrimary
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.winRateText !== "-" ? ("Eval: " + model.winRateText) : ""
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSuccess
                    }
                }
            }

            onCountChanged: {
                listView.positionViewAtEnd()
            }
        }
    }
}
