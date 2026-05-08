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
  board.Apply(0);                          // B
  board.Apply(1);                          // W
  board.Apply(2);                          // B
  board.Apply(Action::kSwap2ChooseBlack);  // B chooses Black. A becomes White.
  // A becomes White. Next to move is White (A).
  EXPECT_EQ(board.current_player(), Seat::kA);
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);

  // Let's make it Black's turn (B). White plays somewhere useless.
  board.Apply(15 * 10 + 10);  // W
  EXPECT_EQ(board.current_player(), Seat::kB);
  EXPECT_EQ(board.stone_to_place(), Player::kBlack);

  // Black has 4 in a row: (0,0), (1,0), (2,0), (3,0)
  // Actually, we must place alternatingly... wait, that's tedious.
  // Let's do it properly.

  Board b;
  b.Apply(0);  // B
  b.Apply(1);  // W
  b.Apply(2);  // B
  b.Apply(
      Action::kSwap2ChooseBlack);  // B chooses Black. A is White. Next is W(A).

  b.Apply(15 * 1 + 0);  // W
  b.Apply(15 * 0 + 0);  // B
  b.Apply(15 * 1 + 1);  // W
  b.Apply(15 * 0 + 1);  // B
  b.Apply(15 * 1 + 2);  // W
  b.Apply(15 * 0 + 2);  // B
  b.Apply(15 * 1 + 3);  // W
  b.Apply(15 * 0 + 3);  // B

  // W's turn
  b.Apply(15 * 1 + 10);  // W useless move

  // Now it is B's turn (Black).
  // B has stones at (0,0), (0,1), (0,2), (0,3).
  // The winning move is (0,4) [which is index 4].
  EXPECT_EQ(b.current_player(), Seat::kB);
  EXPECT_EQ(b.stone_to_place(), Player::kBlack);

  RandomEvaluator evaluator;
  MCTS mcts(1000, 4, 1.0f);  // 1000 sims, 4 threads
  mcts.Search(b, &evaluator);

  int best_move = mcts.GetBestMove();
  EXPECT_EQ(best_move, 4);  // Index of (0, 4)
}
