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
  board.Apply(Action::FromXY(7, 7).id);  // Center black
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);
  EXPECT_EQ(board.current_player(), Seat::kA);

  board.Apply(Action::FromXY(8, 8).id);  // White
  EXPECT_EQ(board.stone_to_place(), Player::kBlack);
  EXPECT_EQ(board.current_player(), Seat::kA);

  board.Apply(Action::FromXY(6, 6).id);  // Black

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
  board.Apply(Action::FromXY(0, 0).id);                          // B (0, 0)
  board.Apply(Action::FromXY(1, 0).id);                          // W (1, 0)
  board.Apply(Action::FromXY(2, 0).id);                          // B (2, 0)
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.
  // White to move. Current player is B.
  EXPECT_EQ(board.current_player(), Seat::kB);
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);

  // B places W at (0, 1)
  board.Apply(Action::FromXY(0, 1).id);
  // A places B at (1, 14)
  board.Apply(Action::FromXY(1, 14).id);

  // Now let's form exactly five for White in a column
  board.Apply(Action::FromXY(0, 2).id);  // W
  board.Apply(Action::FromXY(2, 14).id); // B
  board.Apply(Action::FromXY(0, 3).id);  // W
  board.Apply(Action::FromXY(3, 14).id); // B
  board.Apply(Action::FromXY(0, 4).id);  // W
  board.Apply(Action::FromXY(4, 14).id); // B

  // Next is W. Will place at (0, 5) making it 5 in a row
  EXPECT_EQ(board.result(), Result::kUndetermined);
  board.Apply(Action::FromXY(0, 5).id);  // W

  EXPECT_EQ(board.result(), Result::kPlayerBWin);  // White is B
}

TEST(BoardTest, OverlineDoesNotWin) {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);                          // B
  board.Apply(Action::FromXY(1, 0).id);                          // W
  board.Apply(Action::FromXY(2, 0).id);                          // B
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.

  // Setup a situation where a move creates 6 in a row.
  // W at (0, 1), W at (0, 2), W at (0, 3), W at (0, 5), W at (0, 6)
  // W places at (0, 4) to make 6.
  board.Apply(Action::FromXY(0, 1).id);  // W
  board.Apply(Action::FromXY(1, 14).id); // B
  board.Apply(Action::FromXY(0, 2).id);  // W
  board.Apply(Action::FromXY(2, 13).id); // B
  board.Apply(Action::FromXY(0, 3).id);  // W
  board.Apply(Action::FromXY(3, 14).id); // B
  board.Apply(Action::FromXY(0, 5).id);  // W
  board.Apply(Action::FromXY(4, 13).id); // B
  board.Apply(Action::FromXY(0, 6).id);  // W
  board.Apply(Action::FromXY(5, 14).id); // B

  // W places at (0, 4)
  board.Apply(Action::FromXY(0, 4).id);  // W

  // Overline! Should not win.
  EXPECT_EQ(board.result(), Result::kUndetermined);
}
