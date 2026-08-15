#include "plugin/plugin_loader.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

namespace gomoku::plugin {

class PluginLoaderTest : public ::testing::Test {
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

TEST_F(PluginLoaderTest, LoadAndQueryPluginMetadata) {
  auto plugin_path = GetPluginPath();
  ASSERT_TRUE(std::filesystem::exists(plugin_path))
      << "Plugin library not found at: " << plugin_path;

  auto plugin = EnginePluginLoader::LoadPlugin(plugin_path);
  ASSERT_NE(plugin, nullptr);

  auto info = plugin->GetInfo();
  EXPECT_EQ(info.api_version_major, GOMOKU_PLUGIN_API_VERSION_MAJOR);
  EXPECT_STREQ(info.plugin_name, "SampleGomokuEngine");
  EXPECT_STREQ(info.plugin_version, "1.0.0");
  EXPECT_STREQ(info.author, "Antigravity Team");
}

TEST_F(PluginLoaderTest, PlayerLifecycleAndInquiry) {
  auto plugin_path = GetPluginPath();
  ASSERT_TRUE(std::filesystem::exists(plugin_path));

  auto plugin = EnginePluginLoader::LoadPlugin(plugin_path);
  ASSERT_NE(plugin, nullptr);

  auto player = plugin->CreatePlayer();
  ASSERT_NE(player, nullptr);

  MatchSettings settings{};
  settings.seat = Seat::kA;
  settings.difficulty = Difficulty::kChampion;
  settings.opponent_name = "Opponent";
  player->OnMatchStart(settings);

  BoardState board;
  // Move 1
  int action = player->InquireAction(board);
  EXPECT_GE(action, 0);
  EXPECT_LT(action, 225);
  EXPECT_TRUE(board.IsLegalAction(action));

  // Inform player action was applied
  player->ApplyAction(action);
  board.ApplyAction(action);

  // Match end
  MatchResultInfo result_info{};
  result_info.result = GameResult::kPlayerAWin;
  result_info.winner_seat = Seat::kA;
  result_info.termination_reason = "five_in_a_row";
  player->OnMatchEnd(result_info);
}

TEST_F(PluginLoaderTest, ConcurrentTelemetryWhileInquiryInFlight) {
  auto plugin_path = GetPluginPath();
  ASSERT_TRUE(std::filesystem::exists(plugin_path));

  auto plugin = EnginePluginLoader::LoadPlugin(plugin_path);
  ASSERT_NE(plugin, nullptr);

  auto player = plugin->CreatePlayer();
  ASSERT_NE(player, nullptr);

  MatchSettings settings{};
  settings.seat = Seat::kA;
  settings.difficulty = Difficulty::kTruth;
  player->OnMatchStart(settings);

  BoardState board;
  // Setup a mid-game board
  board.phase = GamePhase::kStandard;
  board.ApplyAction(BoardState::ActionFromXY(7, 7));
  board.ApplyAction(BoardState::ActionFromXY(7, 8));
  board.ApplyAction(BoardState::ActionFromXY(8, 7));
  board.ApplyAction(BoardState::ActionFromXY(8, 8));

  // Launch inquiry asynchronously in worker thread
  auto inquiry_future =
      std::async(std::launch::async, [&]() { return player->InquireAction(board); });

  // Concurrently poll telemetry from main/GUI thread
  for (int i = 0; i < 5; ++i) {
    auto win_rate = player->GetEstimatedWinRate();
    if (win_rate.has_value()) {
      EXPECT_GE(*win_rate, 0.0f);
      EXPECT_LE(*win_rate, 1.0f);
    }

    auto policy = player->GetPolicy();
    if (policy.has_value()) {
      EXPECT_EQ(policy->size(), kNumTotalActions);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  int chosen_action = inquiry_future.get();
  EXPECT_TRUE(board.IsLegalAction(chosen_action));
}

}  // namespace gomoku::plugin
