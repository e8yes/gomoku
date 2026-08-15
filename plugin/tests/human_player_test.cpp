#include "plugin/human_player.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

namespace gomoku::plugin {

TEST(HumanPlayerTest, BasicProperties) {
  HumanPlayer player("Alice");
  EXPECT_EQ(player.GetName(), "Alice");
  EXPECT_TRUE(player.IsHuman());
  EXPECT_FALSE(player.IsWaitingForAction());
}

TEST(HumanPlayerTest, ThreadedMoveSubmission) {
  HumanPlayer player("Bob");
  MatchSettings settings{};
  settings.seat = Seat::kA;
  settings.difficulty = Difficulty::kCasual;
  player.OnMatchStart(settings);

  BoardState board;

  // Inquire action in background thread
  auto future_action = std::async(
      std::launch::async, [&]() { return player.InquireAction(board); });

  // Wait for player to enter waiting state
  while (!player.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  EXPECT_TRUE(player.IsWaitingForAction());

  // Simulate human clicking (7, 7) on GUI
  const int action = BoardState::ActionFromXY(7, 7);
  bool submitted = player.SubmitAction(action);
  EXPECT_TRUE(submitted);

  int result_action = future_action.get();
  EXPECT_EQ(result_action, action);
  EXPECT_FALSE(player.IsWaitingForAction());
}

TEST(HumanPlayerTest, Cancellation) {
  HumanPlayer player("Charlie");
  BoardState board;

  auto future_action = std::async(
      std::launch::async, [&]() { return player.InquireAction(board); });

  while (!player.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  player.CancelInquiry();

  int result_action = future_action.get();
  EXPECT_EQ(result_action, -1);
}

}  // namespace gomoku::plugin
