#include "board.h"

#include <gtest/gtest.h>

TEST(BoardTest, InitialState) {
  Board board;
  EXPECT_EQ(board.phase(), Phase::kPlaceInitialThree);
  EXPECT_EQ(board.current_player(), Seat::kA);
  EXPECT_EQ(board.stone_to_place(), Player::kBlack);
}

TEST(BoardTest, Swap2InitialThree) {
  Board board;
  board.Apply(7 * 15 + 7);  // Center black
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);
  EXPECT_EQ(board.current_player(), Seat::kA);

  board.Apply(8 * 15 + 8);  // White
  EXPECT_EQ(board.stone_to_place(), Player::kBlack);
  EXPECT_EQ(board.current_player(), Seat::kA);

  board.Apply(6 * 15 + 6);  // Black

  EXPECT_EQ(board.phase(), Phase::kSwap2Decision);
  EXPECT_EQ(board.stone_to_place(), Player::kNone);
  EXPECT_EQ(board.current_player(), Seat::kB);

  auto legal = board.GetLegalActions();
  EXPECT_EQ(legal.size(), 3);
  EXPECT_EQ(legal[0], Action::kSwap2ChooseWhite);
  EXPECT_EQ(legal[1], Action::kSwap2ChooseBlack);
  EXPECT_EQ(legal[2], Action::kSwap2PlaceTwo);
}

TEST(BoardTest, ExactFiveRule) {
  Board board;
  // Fast-forward to standard play by A choosing Black and B choosing White
  // Let's use Swap2 Choose White
  board.Apply(0);                          // B (0, 0)
  board.Apply(1);                          // W (1, 0)
  board.Apply(2);                          // B (2, 0)
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.
  // White to move. Current player is B.
  EXPECT_EQ(board.current_player(), Seat::kB);
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);

  // B places W at (0, 1)
  board.Apply(15 * 1 + 0);
  // A places B at (1, 14)
  board.Apply(15 * 14 + 1);

  // Now let's form exactly five for White in a column
  board.Apply(15 * 2 + 0);  // W
  board.Apply(15 * 14 + 2); // B
  board.Apply(15 * 3 + 0);  // W
  board.Apply(15 * 14 + 3); // B
  board.Apply(15 * 4 + 0);  // W
  board.Apply(15 * 14 + 4); // B

  // Next is W. Will place at (0, 5) making it 5 in a row
  EXPECT_EQ(board.result(), Result::kUndetermined);
  board.Apply(15 * 5 + 0);  // W

  EXPECT_EQ(board.result(), Result::kPlayerBWin);  // White is B
}

TEST(BoardTest, OverlineDoesNotWin) {
  Board board;
  board.Apply(0);                          // B
  board.Apply(1);                          // W
  board.Apply(2);                          // B
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.

  // Setup a situation where a move creates 6 in a row.
  // W at (0, 1), W at (0, 2), W at (0, 3), W at (0, 5), W at (0, 6)
  // W places at (0, 4) to make 6.
  board.Apply(15 * 1 + 0);  // W
  board.Apply(15 * 14 + 1); // B
  board.Apply(15 * 2 + 0);  // W
  board.Apply(15 * 13 + 2); // B
  board.Apply(15 * 3 + 0);  // W
  board.Apply(15 * 14 + 3); // B
  board.Apply(15 * 5 + 0);  // W
  board.Apply(15 * 13 + 4); // B
  board.Apply(15 * 6 + 0);  // W
  board.Apply(15 * 14 + 5); // B

  // W places at (0, 4)
  board.Apply(15 * 4 + 0);  // W

  // Overline! Should not win.
  EXPECT_EQ(board.result(), Result::kUndetermined);
}
