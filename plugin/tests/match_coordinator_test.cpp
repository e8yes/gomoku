#include "plugin/match_coordinator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include "plugin/human_player.h"
#include "plugin/plugin_loader.h"

namespace gomoku::plugin {

class MatchCoordinatorTest : public ::testing::Test {
 protected:
  std::filesystem::path GetPluginPath() {
    std::filesystem::path candidates[] = {
        "libgomoku_sample_engine.so",
        "build_plugin/libgomoku_sample_engine.so",
        "../build_plugin/libgomoku_sample_engine.so",
        "build/libgomoku_sample_engine.so",
        "build/plugin/libgomoku_sample_engine.so",
        "../build/plugin/libgomoku_sample_engine.so",
        "./libgomoku_sample_engine.so",
        "plugin/libgomoku_sample_engine.so"};
    for (const auto& p : candidates) {
      if (std::filesystem::exists(p)) return p;
    }
    return "libgomoku_sample_engine.so";
  }
};

TEST_F(MatchCoordinatorTest, PlayerVsPlayerStepByStep) {
  HumanPlayer player_a("Alice");
  HumanPlayer player_b("Bob");

  MatchCoordinator coordinator;
  coordinator.StartMatch(&player_a, &player_b);
  EXPECT_FALSE(coordinator.IsFinished());

  // Step 1: Alice moves
  auto future_step1 =
      std::async(std::launch::async, [&]() { return coordinator.Step(); });
  while (!player_a.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  player_a.SubmitAction(BoardState::ActionFromXY(7, 7));
  EXPECT_TRUE(future_step1.get());
  EXPECT_EQ(coordinator.GetBoardState().cell(7, 7), Stone::kBlack);

  // Step 2: Alice moves second stone in Swap2
  auto future_step2 =
      std::async(std::launch::async, [&]() { return coordinator.Step(); });
  while (!player_a.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  player_a.SubmitAction(BoardState::ActionFromXY(7, 8));
  EXPECT_TRUE(future_step2.get());
  EXPECT_EQ(coordinator.GetBoardState().cell(7, 8), Stone::kWhite);
}

TEST_F(MatchCoordinatorTest, EngineVsEngineFullMatch) {
  auto plugin_path = GetPluginPath();
  ASSERT_TRUE(std::filesystem::exists(plugin_path));

  auto plugin = EnginePluginLoader::LoadPlugin(plugin_path);
  ASSERT_NE(plugin, nullptr);

  auto engine_a = plugin->CreatePlayer();
  auto engine_b = plugin->CreatePlayer();

  MatchCoordinator coordinator;
  int step_count = 0;
  auto result = coordinator.PlayMatch(
      engine_a.get(), engine_b.get(), Difficulty::kCasual, Difficulty::kClub,
      [&](const BoardState& /*state*/, int /*last_action*/,
          IPlayer* /*moving_player*/) { ++step_count; });

  EXPECT_GT(step_count, 0);
  EXPECT_TRUE(coordinator.IsFinished());
  EXPECT_NE(result.result, GameResult::kUndetermined);
}

TEST_F(MatchCoordinatorTest, PlayerVsEngineFullMatch) {
  auto plugin_path = GetPluginPath();
  ASSERT_TRUE(std::filesystem::exists(plugin_path));

  auto plugin = EnginePluginLoader::LoadPlugin(plugin_path);
  ASSERT_NE(plugin, nullptr);

  HumanPlayer human("Player1");
  auto engine = plugin->CreatePlayer();

  MatchCoordinator coordinator;
  coordinator.StartMatch(&human, engine.get(), Difficulty::kCasual,
                         Difficulty::kVeteran);

  // Move 1: Human places stone at (7, 7)
  auto f1 =
      std::async(std::launch::async, [&]() { return coordinator.Step(); });
  while (!human.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  human.SubmitAction(BoardState::ActionFromXY(7, 7));
  f1.get();

  // Move 2: Human places stone at (7, 8)
  auto f2 =
      std::async(std::launch::async, [&]() { return coordinator.Step(); });
  while (!human.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  human.SubmitAction(BoardState::ActionFromXY(7, 8));
  f2.get();

  // Move 3: Human places stone at (8, 8)
  auto f3 =
      std::async(std::launch::async, [&]() { return coordinator.Step(); });
  while (!human.IsWaitingForAction()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  human.SubmitAction(BoardState::ActionFromXY(8, 8));
  f3.get();

  // Phase is now Swap2 decision for Engine!
  EXPECT_EQ(coordinator.GetBoardState().phase, GamePhase::kSwap2Decision);
  EXPECT_EQ(coordinator.GetCurrentPlayer(), engine.get());

  // Engine executes Swap2 choice
  EXPECT_TRUE(coordinator.Step());
  EXPECT_EQ(coordinator.GetBoardState().phase, GamePhase::kStandard);
}

}  // namespace gomoku::plugin
