#include <gtest/gtest.h>

#include <filesystem>

#include "core/plugin_registry.h"

namespace gomoku::app {

TEST(PluginRegistryTest, AvailableEnginesContainsHuman) {
  auto& registry = PluginRegistry::Instance();
  QStringList engines = registry.availableEngines();

  EXPECT_FALSE(engines.isEmpty());
  EXPECT_EQ(engines.first().toStdString(), "Human");
  EXPECT_FALSE(registry.isEnginePlugin("Human"));
}

TEST(PluginRegistryTest, CreateHumanPlayerReturnsNull) {
  auto& registry = PluginRegistry::Instance();
  auto player = registry.CreatePlayer("Human");
  EXPECT_EQ(player, nullptr);
}

TEST(PluginRegistryTest, DiscoverSampleEnginePlugin) {
  auto& registry = PluginRegistry::Instance();
  // Rescan current directory (which contains engine_plugin_sample.so in build output)
  registry.rescanPlugins();

  QStringList engines = registry.availableEngines();
  EXPECT_GE(engines.size(), 1);

  if (!engines.contains("SampleGomokuEngine")) {
    GTEST_SKIP() << "SampleGomokuEngine plugin not found in current directory";
  }

  EXPECT_TRUE(registry.isEnginePlugin("SampleGomokuEngine"));
  EXPECT_EQ(registry.getPluginVersion("SampleGomokuEngine").toStdString(), "1.0.0");
  EXPECT_EQ(registry.getPluginAuthor("SampleGomokuEngine").toStdString(), "Antigravity Team");

  auto player = registry.CreatePlayer("SampleGomokuEngine");
  ASSERT_NE(player, nullptr);
  EXPECT_EQ(player->GetName(), "SampleGomokuEngine");
  EXPECT_FALSE(player->IsHuman());

  // Test estimated win rate query
  auto wr = player->GetEstimatedWinRate();
  EXPECT_TRUE(wr.has_value());
}

}  // namespace gomoku::app
