#include "plugin/difficulty.h"

#include <gtest/gtest.h>

namespace gomoku::plugin {

TEST(DifficultyTest, StringConversions) {
  EXPECT_STREQ(DifficultyToString(Difficulty::kApprentice), "Apprentice");
  EXPECT_STREQ(DifficultyToString(Difficulty::kCasual), "Casual");
  EXPECT_STREQ(DifficultyToString(Difficulty::kClub), "Club");
  EXPECT_STREQ(DifficultyToString(Difficulty::kVeteran), "Veteran");
  EXPECT_STREQ(DifficultyToString(Difficulty::kChampion), "Champion");
  EXPECT_STREQ(DifficultyToString(Difficulty::kTruth), "Truth");

  EXPECT_EQ(DifficultyFromString("Apprentice"), Difficulty::kApprentice);
  EXPECT_EQ(DifficultyFromString("casual"), Difficulty::kCasual);
  EXPECT_EQ(DifficultyFromString("CLUB"), Difficulty::kClub);
  EXPECT_EQ(DifficultyFromString("Veteran"), Difficulty::kVeteran);
  EXPECT_EQ(DifficultyFromString("champion"), Difficulty::kChampion);
  EXPECT_EQ(DifficultyFromString("truth"), Difficulty::kTruth);

  EXPECT_FALSE(DifficultyFromString("InvalidLevel").has_value());
}

}  // namespace gomoku::plugin
