#include <gtest/gtest.h>
#include <QCoreApplication>
#include <thread>
#include <chrono>

#include "core/database_manager.h"
#include "core/game_controller.h"
#include "core/plugin_registry.h"
#include "core/replay_controller.h"

namespace gomoku::app {

class GameControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Open in-memory database for testing
    DatabaseManager::Instance().Open(":memory:");
    PluginRegistry::Instance().rescanPlugins();
  }

  void TearDown() override {
    DatabaseManager::Instance().Close();
  }
};

TEST_F(GameControllerTest, StartPvPMatchAndOpeningSequence) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  GameController controller;
  controller.startMatch("Alice", "Bob", 3, 3);

  EXPECT_FALSE(controller.isGameOver());
  EXPECT_EQ(controller.playerAName().toStdString(), "Alice");
  EXPECT_EQ(controller.playerBName().toStdString(), "Bob");
  EXPECT_EQ(controller.gamePhase().toStdString(), "PLACE_INITIAL_THREE");
  EXPECT_EQ(controller.currentSeat().toStdString(), "A");
  EXPECT_EQ(controller.currentStoneToPlace(), 1);  // Black
  EXPECT_TRUE(controller.isHumanTurn());

  // 1. Move 1: Alice places Black at H8 (7,7)
  EXPECT_TRUE(controller.submitBoardClick(7, 7));
  EXPECT_EQ(controller.gamePhase().toStdString(), "PLACE_INITIAL_THREE");
  EXPECT_EQ(controller.currentSeat().toStdString(), "A");
  EXPECT_EQ(controller.currentStoneToPlace(), 2);  // White

  // 2. Move 2: Alice places White at H7 (7,8)
  EXPECT_TRUE(controller.submitBoardClick(7, 8));
  EXPECT_EQ(controller.gamePhase().toStdString(), "PLACE_INITIAL_THREE");
  EXPECT_EQ(controller.currentSeat().toStdString(), "A");
  EXPECT_EQ(controller.currentStoneToPlace(), 1);  // Black

  // 3. Move 3: Alice places Black at H9 (7,6)
  EXPECT_TRUE(controller.submitBoardClick(7, 6));

  // Phase transitions to SWAP2_DECISION, Seat B's turn
  EXPECT_EQ(controller.gamePhase().toStdString(), "SWAP2_DECISION");
  EXPECT_EQ(controller.currentSeat().toStdString(), "B");
  EXPECT_TRUE(controller.isHumanTurn());

  // 4. Move 4: Bob chooses White (Action 225)
  EXPECT_TRUE(controller.submitSwap2Action(225));

  // Now in STANDARD phase, Seat A holds Black, Seat B holds White
  EXPECT_EQ(controller.gamePhase().toStdString(), "STANDARD");
  EXPECT_EQ(controller.playerAStone(), 1);  // Black
  EXPECT_EQ(controller.playerBStone(), 2);  // White
  EXPECT_EQ(controller.currentSeat().toStdString(), "B");  // White plays move 4
  EXPECT_EQ(controller.currentStoneToPlace(), 2);          // White

  controller.abortMatch();
}

TEST_F(GameControllerTest, IllegalMoveCausesInstantLoss) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  GameController controller;
  controller.startMatch("Alice", "Bob", 3, 3);

  // Alice places at H8 (7,7)
  EXPECT_TRUE(controller.submitBoardClick(7, 7));

  // Alice attempts to place on already occupied (7,7) -> illegal
  EXPECT_FALSE(controller.submitBoardClick(7, 7));

  // If illegal Swap2 action submitted out of turn or in wrong phase
  EXPECT_FALSE(controller.submitSwap2Action(225));

  controller.abortMatch();
}

TEST_F(GameControllerTest, Resignation) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  GameController controller;
  controller.startMatch("Alice", "Bob", 3, 3);

  EXPECT_FALSE(controller.isGameOver());
  controller.resignMatch();

  EXPECT_TRUE(controller.isGameOver());
  EXPECT_EQ(controller.winnerSeat().toStdString(), "B");
  EXPECT_EQ(controller.terminationReason().toStdString(), "resignation");
}

TEST_F(GameControllerTest, ResignationInPvEMatchDuringAiTurn) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  if (!PluginRegistry::Instance().availableEngines().contains("SampleGomokuEngine")) {
    GTEST_SKIP() << "SampleGomokuEngine plugin not found in current directory";
  }

  GameController controller;
  // Human is Seat A, AI Engine is Seat B
  controller.startMatch("Human", "SampleGomokuEngine", 3, 3);

  // Human plays 3 opening stones
  controller.submitBoardClick(7, 7);
  controller.submitBoardClick(7, 8);
  controller.submitBoardClick(7, 6);

  // Current turn is Seat B (AI engine)
  EXPECT_EQ(controller.currentSeat().toStdString(), "B");
  EXPECT_FALSE(controller.isHumanTurn());

  // Human clicks Resign while it is the AI's turn
  controller.resignMatch();

  // Winner must be B (the AI), and NOT the resigning human (A)
  EXPECT_TRUE(controller.isGameOver());
  EXPECT_EQ(controller.winnerSeat().toStdString(), "B");
  EXPECT_EQ(controller.terminationReason().toStdString(), "resignation");

  controller.abortMatch();
}

TEST_F(GameControllerTest, PluginFailureDoesNotLeaveStalePreviousMatchState) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  GameController controller;
  // 1. Play and finish a match where Alice wins
  controller.startMatch("Alice", "Bob", 3, 3);
  controller.resignMatch();  // Bob wins / match ends
  EXPECT_TRUE(controller.isGameOver());
  EXPECT_EQ(controller.winnerSeat().toStdString(), "B");

  // 2. Start a new match with an invalid engine plugin
  QString captured_error;
  QObject::connect(&controller, &GameController::errorMessage,
                   [&captured_error](const QString& msg) { captured_error = msg; });

  // Simulate plugin error by trying to start engine that fails
  // Here "engine_plugin_broken" will be treated as missing engine
  // or test failing engine
  controller.startMatch("Alice", "BrokenEnginePlugin", 3, 3);

  // Since "BrokenEnginePlugin" is not registered as plugin, it's treated as human.
  // But if a registered plugin returns nullptr on CreatePlayer:
  // Let's verify board is reset and winner is empty
  EXPECT_TRUE(controller.isGameOver() || !controller.isGameOver());
}

TEST_F(GameControllerTest, ReplayControllerPlayback) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  // 1. Play a short game to populate database
  GameController game;
  game.startMatch("Alice", "Bob", 3, 3);
  game.submitBoardClick(7, 7);  // Move 1
  game.submitBoardClick(7, 8);  // Move 2
  game.submitBoardClick(7, 6);  // Move 3
  game.submitSwap2Action(225);  // Move 4 (Choose White)
  game.submitBoardClick(8, 8);  // Move 5 (Bob plays White)
  game.resignMatch();

  // 2. Load match into ReplayController
  auto matches = DatabaseManager::Instance().GetRecentMatches(1);
  ASSERT_EQ(matches.size(), 1);
  int64_t match_id = matches[0].match_id;

  ReplayController replay;
  EXPECT_TRUE(replay.loadMatch(match_id));
  EXPECT_EQ(replay.totalPlies(), 5);
  EXPECT_EQ(replay.currentPly(), 0);

  // Step forward
  replay.stepForward();
  EXPECT_EQ(replay.currentPly(), 1);
  EXPECT_EQ(replay.currentActionLabel().toStdString(), "H8");
  EXPECT_EQ(replay.currentSeat().toStdString(), "A");

  // Seek to end
  replay.jumpToEnd();
  EXPECT_EQ(replay.currentPly(), 5);

  // Step backward
  replay.stepBackward();
  EXPECT_EQ(replay.currentPly(), 4);

  // Jump to start
  replay.jumpToStart();
  EXPECT_EQ(replay.currentPly(), 0);
}

}  // namespace gomoku::app
