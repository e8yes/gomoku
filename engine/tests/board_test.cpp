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
  board.Apply(Action::FromXY(0, 0).id);    // B (0, 0)
  board.Apply(Action::FromXY(1, 0).id);    // W (1, 0)
  board.Apply(Action::FromXY(2, 0).id);    // B (2, 0)
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.
  // White to move. Current player is B.
  EXPECT_EQ(board.current_player(), Seat::kB);
  EXPECT_EQ(board.stone_to_place(), Player::kWhite);

  // B places W at (0, 1)
  board.Apply(Action::FromXY(0, 1).id);
  // A places B at (1, 14)
  board.Apply(Action::FromXY(1, 14).id);

  // Now let's form exactly five for White in a column
  board.Apply(Action::FromXY(0, 2).id);   // W
  board.Apply(Action::FromXY(2, 14).id);  // B
  board.Apply(Action::FromXY(0, 3).id);   // W
  board.Apply(Action::FromXY(3, 14).id);  // B
  board.Apply(Action::FromXY(0, 4).id);   // W
  board.Apply(Action::FromXY(4, 14).id);  // B

  // Next is W. Will place at (0, 5) making it 5 in a row
  EXPECT_EQ(board.result(), Result::kUndetermined);
  board.Apply(Action::FromXY(0, 5).id);  // W

  EXPECT_EQ(board.result(), Result::kPlayerBWin);  // White is B
}

TEST(BoardTest, OverlineDoesNotWin) {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);    // B
  board.Apply(Action::FromXY(1, 0).id);    // W
  board.Apply(Action::FromXY(2, 0).id);    // B
  board.Apply(Action::kSwap2ChooseWhite);  // B chooses White. A is Black.

  // Setup a situation where a move creates 6 in a row.
  // W at (0, 1), W at (0, 2), W at (0, 3), W at (0, 5), W at (0, 6)
  // W places at (0, 4) to make 6.
  board.Apply(Action::FromXY(0, 1).id);   // W
  board.Apply(Action::FromXY(1, 14).id);  // B
  board.Apply(Action::FromXY(0, 2).id);   // W
  board.Apply(Action::FromXY(2, 13).id);  // B
  board.Apply(Action::FromXY(0, 3).id);   // W
  board.Apply(Action::FromXY(3, 14).id);  // B
  board.Apply(Action::FromXY(0, 5).id);   // W
  board.Apply(Action::FromXY(4, 13).id);  // B
  board.Apply(Action::FromXY(0, 6).id);   // W
  board.Apply(Action::FromXY(5, 14).id);  // B

  // W places at (0, 4)
  board.Apply(Action::FromXY(0, 4).id);  // W

  // Overline! Should not win.
  EXPECT_EQ(board.result(), Result::kUndetermined);
}

TEST(BoardTest, ZobristInitialState) {
  Board board1;
  Board board2;

  // Both empty boards should have identical signatures.
  EXPECT_EQ(board1.signature(), board2.signature());

  // Signature should not be all-zeros (trivial default initialization).
  EXPECT_NE(board1.signature()[0], 0);
  EXPECT_NE(board1.signature()[1], 0);
}

TEST(BoardTest, ZobristApplyAndRetract) {
  Board board;
  BoardSignature initial_sig = board.signature();

  // Make some moves (placements)
  int m1 = Action::FromXY(7, 7).id;
  int m2 = Action::FromXY(8, 8).id;
  int m3 = Action::FromXY(6, 6).id;

  board.Apply(m1);
  EXPECT_NE(board.signature(), initial_sig);

  board.Apply(m2);
  board.Apply(m3);

  // Apply a control action (Swap2 decision)
  int control = Action::kSwap2ChooseWhite;
  board.Apply(control);

  // Apply standard game moves
  int m4 = Action::FromXY(5, 5).id;
  int m5 = Action::FromXY(4, 4).id;
  board.Apply(m4);
  board.Apply(m5);

  // Now retract everything in reverse order
  board.Retract(m5);
  board.Retract(m4);
  board.Retract(control);
  board.Retract(m3);
  board.Retract(m2);
  board.Retract(m1);

  // The board signature should be exactly restored to the initial signature.
  EXPECT_EQ(board.signature(), initial_sig);
}

TEST(BoardTest, ZobristTransposition) {
  Board board1;
  Board board2;

  // Transition both boards to standard phase
  // Play initial three
  board1.Apply(Action::FromXY(0, 0).id);    // B
  board1.Apply(Action::FromXY(1, 0).id);    // W
  board1.Apply(Action::FromXY(2, 0).id);    // B
  board1.Apply(Action::kSwap2ChooseWhite);  // A is Black, B is White

  board2.Apply(Action::FromXY(0, 0).id);
  board2.Apply(Action::FromXY(1, 0).id);
  board2.Apply(Action::FromXY(2, 0).id);
  board2.Apply(Action::kSwap2ChooseWhite);

  // Now we are in Phase::kStandard.
  // Next player to move is B (White), then A (Black), then B (White), then A
  // (Black).

  // Path 1:
  // B plays (4, 4), A plays (5, 5), B plays (6, 6), A plays (7, 7)
  board1.Apply(Action::FromXY(4, 4).id);  // W
  board1.Apply(Action::FromXY(5, 5).id);  // B
  board1.Apply(Action::FromXY(6, 6).id);  // W
  board1.Apply(Action::FromXY(7, 7).id);  // B

  // Path 2:
  // Transpose the W moves: B plays (6, 6) first, then (4, 4)
  // Transpose the B moves: A plays (7, 7) first, then (5, 5)
  // So:
  // B plays (6, 6)
  // A plays (7, 7)
  // B plays (4, 4)
  // A plays (5, 5)
  board2.Apply(Action::FromXY(6, 6).id);  // W
  board2.Apply(Action::FromXY(7, 7).id);  // B
  board2.Apply(Action::FromXY(4, 4).id);  // W
  board2.Apply(Action::FromXY(5, 5).id);  // B

  // The final board cells and state are completely identical.
  EXPECT_EQ(board1.signature(), board2.signature());
}

TEST(BoardTest, ZobristCollisions) {
  Board board1;
  Board board2;

  // Signatures should change when different moves are applied
  board1.Apply(Action::FromXY(7, 7).id);
  board2.Apply(Action::FromXY(0, 0).id);

  EXPECT_NE(board1.signature(), board2.signature());
}
