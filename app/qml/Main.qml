import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "theme"
import "screens"

ApplicationWindow {
    id: window
    width: 1100
    height: 780
    minimumWidth: 900
    minimumHeight: 650
    visible: true
    title: qsTr("Gomoku Swap2 Desktop Arena")
    color: GomokuTheme.background

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: mainScreenComponent
    }

    // Global Error Toast Banner
    Rectangle {
        id: errorBanner
        y: opacity > 0 ? 16 : -80
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 40, 650)
        height: 48
        radius: GomokuTheme.radiusSmall
        color: "#C94A53"
        border.color: "#A8323A"
        z: 999
        visible: opacity > 0
        opacity: 0.0

        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Row {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Text {
                text: "⚠️"
                font.pixelSize: 16
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: errorText
                text: ""
                font.family: GomokuTheme.fontFamily
                font.pixelSize: GomokuTheme.fontSizeSmall
                font.bold: true
                color: "#FFFFFF"
                elide: Text.ElideRight
                width: parent.width - 60
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: "✕"
                font.family: GomokuTheme.fontFamily
                font.pixelSize: 14
                font.bold: true
                color: "#FFFFFF"
                anchors.verticalCenter: parent.verticalCenter

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: errorBanner.hide()
                }
            }
        }

        Timer {
            id: errorTimer
            interval: 5000
            onTriggered: errorBanner.hide()
        }

        function show(msg) {
            errorText.text = msg
            opacity = 1.0
            errorTimer.restart()
        }

        function hide() {
            opacity = 0.0
            errorTimer.stop()
        }
    }

    Connections {
        target: gameControllerInstance
        function onErrorMessage(message) {
            errorBanner.show(message)
        }
    }

    Connections {
        target: pluginRegistryInstance
        function onPluginError(path, errorMessage) {
            errorBanner.show("Plugin Error (" + path + "): " + errorMessage)
        }
    }

    Component {
        id: mainScreenComponent
        MainScreen {
            gameController: gameControllerInstance
            pluginRegistry: pluginRegistryInstance
            replayController: replayControllerInstance

            onStartMatchRequested: (pA, pB, diffA, diffB) => {
                gameControllerInstance.startMatch(pA, pB, diffA, diffB)
                if (!gameControllerInstance.isGameOver) {
                    stackView.push(matchScreenComponent)
                }
            }

            onReplayMatchRequested: (matchId) => {
                var ok = replayControllerInstance.loadMatch(matchId)
                if (ok) {
                    stackView.push(replayScreenComponent)
                }
            }
        }
    }

    Component {
        id: matchScreenComponent
        MatchScreen {
            gameController: gameControllerInstance

            onReturnToHomeRequested: {
                stackView.pop()
            }
        }
    }

    Component {
        id: replayScreenComponent
        ReplayScreen {
            replayController: replayControllerInstance

            onReturnToHomeRequested: {
                stackView.pop()
            }
        }
    }
}
