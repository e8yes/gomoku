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

    Component {
        id: mainScreenComponent
        MainScreen {
            gameController: gameControllerInstance
            pluginRegistry: pluginRegistryInstance
            replayController: replayControllerInstance

            onStartMatchRequested: (pA, pB, diffA, diffB) => {
                gameControllerInstance.startMatch(pA, pB, diffA, diffB)
                stackView.push(matchScreenComponent)
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
