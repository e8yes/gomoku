#include "mcts.h"

#include <gtest/gtest.h>

#include "random_evaluator.h"

TEST(MCTSTest, SimpleEndgame) {
  // We set up a board where Black has 4 stones in a row and it's Black's turn.
  // MCTS with a random evaluator should easily find the winning move within a
  // few simulations because expanding that node will immediately result in a
  // terminal win.

  Board board;
  // Fast-forward to standard phase, A is Black, B is White
  board.Apply(Action::FromXY(0, 0).id);    // B
  board.Apply(Action::FromXY(1, 0).id);    // W
  board.Apply(Action::FromXY(2, 0).id);    // B
  board.Apply(Action::kSwap2ChooseBlack);  // B chooses Black. A becomes White.
  // A becomes White. Next to move is White (A).
  EXPECT_EQ(board.current_player(), Seat::kA);
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);

  // Let's make it Black's turn (B). White plays somewhere useless.
  board.Apply(Action::FromXY(10, 10).id);  // W
  EXPECT_EQ(board.current_player(), Seat::kB);
  EXPECT_EQ(board.stone_to_place(), Player::kBlack);

  // Black has 4 in a row: (0,0), (1,0), (2,0), (3,0)
  // Actually, we must place alternatingly... wait, that's tedious.
  // Let's do it properly.

  Board b;
  b.Apply(Action::FromXY(0, 0).id);  // B
  b.Apply(Action::FromXY(1, 0).id);  // W
  b.Apply(Action::FromXY(2, 0).id);  // B
  b.Apply(
      Action::kSwap2ChooseBlack);  // B chooses Black. A is White. Next is W(A).

  b.Apply(Action::FromXY(0, 1).id);  // W
  b.Apply(Action::FromXY(0, 0).id);  // B
  b.Apply(Action::FromXY(1, 1).id);  // W
  b.Apply(Action::FromXY(1, 0).id);  // B
  b.Apply(Action::FromXY(2, 1).id);  // W
  b.Apply(Action::FromXY(2, 0).id);  // B
  b.Apply(Action::FromXY(3, 1).id);  // W
  b.Apply(Action::FromXY(3, 0).id);  // B

  // W's turn
  b.Apply(Action::FromXY(10, 1).id);  // W useless move

  // Now it is B's turn (Black).
  // B has stones at (0,0), (0,1), (0,2), (0,3).
  // The winning move is (0,4) [which is index 4].
  EXPECT_EQ(b.current_player(), Seat::kB);
  EXPECT_EQ(b.stone_to_place(), Player::kBlack);

  RandomEvaluator evaluator;
  MCTS mcts(1000, 32, 1.0f);  // 1000 sims, batch size 32
  std::vector<float> policy = mcts.Search(b, &evaluator);

  int best_move = GetBestAction(policy);

  EXPECT_EQ(best_move, Action::FromXY(4, 0).id);  // Index of (4, 0)
}
