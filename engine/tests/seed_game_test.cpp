#include "seed_game.h"

#include <gtest/gtest.h>

TEST(SeedGameTest, KeepsAndLabelsTheFinalThreePositions) {
  Config config;
  config.simulations = 1;
  config.batch_size = 1;
  config.seed = 1234;
  config.keep_last_moves = 3;

  const std::vector<TrainingExample> examples = GenerateGame(config);

  ASSERT_EQ(examples.size(), 3u);
  for (const auto& example : examples) {
    EXPECT_FALSE(example.board.IsTerminal());
    ASSERT_EQ(example.policy.size(),
              static_cast<std::size_t>(Board::kNumActions));
    EXPECT_GE(example.value, -1.0f);
    EXPECT_LE(example.value, 1.0f);
  }
}
