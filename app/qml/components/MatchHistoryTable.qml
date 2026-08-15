import QtQuick
import "../theme"

Rectangle {
    id: root

    property var matchHistoryModel: null
    signal replayRequested(var matchId)

    radius: GomokuTheme.radiusMedium
    color: GomokuTheme.surfaceDark
    border.color: GomokuTheme.border
    border.width: 1

    Column {
        anchors.fill: parent
        anchors.margins: GomokuTheme.spacingMedium
        spacing: GomokuTheme.spacingSmall

        // Header Row
        Row {
            width: parent.width
            height: 30

            Text {
                text: "Past Matches (SQLite Database)"
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeMedium
                font.bold: true
                color: GomokuTheme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }

            Item { width: 1; height: 1; Layout.fillWidth: true }

            Rectangle {
                anchors.right: parent.right
                width: 80
                height: 28
                radius: GomokuTheme.radiusSmall
                color: refreshMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceLight
                border.color: GomokuTheme.borderActive

                Text {
                    anchors.centerIn: parent
                    text: "Refresh 🔄"
                    font.family: GomokuTheme.fontFamily
                    font.pixelSize: GomokuTheme.fontSizeSmall
                    color: GomokuTheme.textPrimary
                }

                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (root.matchHistoryModel) {
                            root.matchHistoryModel.refreshHistory()
                        }
                    }
                }
            }
        }

        // Table Header
        Rectangle {
            width: parent.width
            height: 28
            color: GomokuTheme.surfaceLight
            radius: GomokuTheme.radiusSmall

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Text { width: 60; anchors.verticalCenter: parent.verticalCenter; text: "ID"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 140; anchors.verticalCenter: parent.verticalCenter; text: "Date"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 160; anchors.verticalCenter: parent.verticalCenter; text: "Seat A (Opener)"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 160; anchors.verticalCenter: parent.verticalCenter; text: "Seat B (Responder)"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 120; anchors.verticalCenter: parent.verticalCenter; text: "Result"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 60; anchors.verticalCenter: parent.verticalCenter; text: "Plies"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
                Text { width: 80; anchors.verticalCenter: parent.verticalCenter; text: "Action"; font.bold: true; color: GomokuTheme.textMuted; font.pixelSize: GomokuTheme.fontSizeSmall }
            }
        }

        // List of Matches
        ListView {
            id: listView
            width: parent.width
            height: parent.height - 75
            clip: true
            model: root.matchHistoryModel

            delegate: Rectangle {
                width: listView.width
                height: 36
                radius: GomokuTheme.radiusSmall
                color: index % 2 === 0 ? "transparent" : GomokuTheme.surfaceLight

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Text {
                        width: 60
                        anchors.verticalCenter: parent.verticalCenter
                        text: "#" + model.matchId
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Text {
                        width: 140
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.createdAt.substring(0, 16)
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Text {
                        width: 160
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.playerAName + " (" + model.playerAType + ")"
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textPrimary
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 160
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.playerBName + " (" + model.playerBType + ")"
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textPrimary
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 120
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.winnerText
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        font.bold: true
                        color: model.result === "PLAYER_A_WIN" || model.result === "PLAYER_B_WIN"
                               ? GomokuTheme.textSuccess
                               : GomokuTheme.textWarning
                    }

                    Text {
                        width: 60
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.totalPlies.toString()
                        font.family: GomokuTheme.fontFamily
                        font.pixelSize: GomokuTheme.fontSizeSmall
                        color: GomokuTheme.textSecondary
                    }

                    Rectangle {
                        width: 70
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        radius: GomokuTheme.radiusSmall
                        color: replayMouse.containsMouse ? GomokuTheme.surfaceHover : GomokuTheme.surfaceDark
                        border.color: GomokuTheme.textAccent

                        Text {
                            anchors.centerIn: parent
                            text: "Replay ▶"
                            font.family: GomokuTheme.fontFamily
                            font.pixelSize: GomokuTheme.fontSizeSmall
                            font.bold: true
                            color: GomokuTheme.textAccent
                        }

                        MouseArea {
                            id: replayMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.replayRequested(model.matchId)
                            }
                        }
                    }
                }
            }
        }
    }
}
