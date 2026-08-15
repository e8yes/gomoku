#include <gtest/gtest.h>

#include "core/database_manager.h"

namespace gomoku::app {

TEST(DatabaseManagerTest, InProcessMemoryDatabase) {
  DatabaseManager db(":memory:");
  EXPECT_TRUE(db.IsOpen());

  // 1. Create a match
  int64_t match_id = db.CreateMatch("Alice", "Human", "AlphaZero_Bot",
                                     "engine_plugin_alphazero.so", 3, 4);
  EXPECT_GT(match_id, 0);

  // 2. Record several moves with win rate snapshots
  bool m1 = db.RecordMove(match_id, 1, 112, "H8", 7, 7, "A", "BLACK",
                          "PLACE_INITIAL_THREE", 0.52f, 150);
  EXPECT_TRUE(m1);

  bool m2 = db.RecordMove(match_id, 2, 113, "J8", 8, 7, "A", "WHITE",
                          "PLACE_INITIAL_THREE", 0.50f, 120);
  EXPECT_TRUE(m2);

  bool m3 = db.RecordMove(match_id, 3, 114, "K8", 9, 7, "A", "BLACK",
                          "PLACE_INITIAL_THREE", 0.55f, 90);
  EXPECT_TRUE(m3);

  // Swap2 control action
  bool m4 = db.RecordMove(match_id, 4, 225, "swap2_choose_white", -1, -1, "B",
                          "NONE", "SWAP2_DECISION", 0.48f, 300);
  EXPECT_TRUE(m4);

  // 3. Conclude match
  bool finished = db.FinishMatch(match_id, "PLAYER_A_WIN", "A",
                                 "five_in_a_row", 4, 1);
  EXPECT_TRUE(finished);

  // 4. Query match by ID
  auto match_opt = db.GetMatchById(match_id);
  ASSERT_TRUE(match_opt.has_value());
  EXPECT_EQ(match_opt->player_a_name, "Alice");
  EXPECT_EQ(match_opt->player_b_name, "AlphaZero_Bot");
  EXPECT_EQ(match_opt->difficulty_a, 3);
  EXPECT_EQ(match_opt->difficulty_b, 4);
  EXPECT_EQ(match_opt->result, "PLAYER_A_WIN");
  EXPECT_EQ(match_opt->winner_seat, "A");
  EXPECT_EQ(match_opt->termination_reason, "five_in_a_row");
  EXPECT_EQ(match_opt->total_plies, 4);
  EXPECT_EQ(match_opt->opening_path, 1);

  // 5. Query moves for match
  auto moves = db.GetMovesForMatch(match_id);
  ASSERT_EQ(moves.size(), 4);
  EXPECT_EQ(moves[0].ply_index, 1);
  EXPECT_EQ(moves[0].action_id, 112);
  EXPECT_EQ(moves[0].action_label, "H8");
  EXPECT_EQ(moves[0].x_coord, 7);
  EXPECT_EQ(moves[0].y_coord, 7);
  EXPECT_EQ(moves[0].stone_placed, "BLACK");
  ASSERT_TRUE(moves[0].estimated_win_rate.has_value());
  EXPECT_FLOAT_EQ(*moves[0].estimated_win_rate, 0.52f);

  EXPECT_EQ(moves[3].action_id, 225);
  EXPECT_EQ(moves[3].action_label, "swap2_choose_white");
  EXPECT_EQ(moves[3].x_coord, -1);
  EXPECT_EQ(moves[3].stone_placed, "NONE");

  // 6. Query recent matches list
  auto list = db.GetRecentMatches(10);
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].match_id, match_id);

  // 7. Delete match
  EXPECT_TRUE(db.DeleteMatch(match_id));
  EXPECT_EQ(db.GetRecentMatches(10).size(), 0);
  EXPECT_EQ(db.GetMovesForMatch(match_id).size(), 0);
}

}  // namespace gomoku::app
