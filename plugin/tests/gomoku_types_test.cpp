#include "plugin/gomoku_types.h"

#include <gtest/gtest.h>

namespace gomoku::plugin {

TEST(GomokuTypesTest, InitialState) {
  BoardState board;
  EXPECT_EQ(board.phase, GamePhase::kPlaceInitialThree);
  EXPECT_EQ(board.current_seat, Seat::kA);
  EXPECT_EQ(board.stone_to_place, Stone::kBlack);
  EXPECT_EQ(board.move_count, 0);
  EXPECT_EQ(board.result, GameResult::kUndetermined);
  EXPECT_FALSE(board.IsTerminal());

  auto legal = board.GetLegalActions();
  EXPECT_EQ(legal.size(), 225);
}

TEST(GomokuTypesTest, Swap2Transitions) {
  BoardState board;

  // Move 1: Black
  board.ApplyAction(BoardState::ActionFromXY(7, 7));
  EXPECT_EQ(board.move_count, 1);
  EXPECT_EQ(board.stone_to_place, Stone::kWhite);
  EXPECT_EQ(board.phase, GamePhase::kPlaceInitialThree);

  // Move 2: White
  board.ApplyAction(BoardState::ActionFromXY(7, 8));
  EXPECT_EQ(board.move_count, 2);
  EXPECT_EQ(board.stone_to_place, Stone::kBlack);
  EXPECT_EQ(board.phase, GamePhase::kPlaceInitialThree);

  // Move 3: Black
  board.ApplyAction(BoardState::ActionFromXY(8, 8));
  EXPECT_EQ(board.move_count, 3);
  EXPECT_EQ(board.phase, GamePhase::kSwap2Decision);
  EXPECT_EQ(board.current_seat, Seat::kB);

  auto legal = board.GetLegalActions();
  EXPECT_EQ(legal.size(), 3);
  EXPECT_TRUE(board.IsLegalAction(Swap2Action::kChooseWhite));
  EXPECT_TRUE(board.IsLegalAction(Swap2Action::kChooseBlack));
  EXPECT_TRUE(board.IsLegalAction(Swap2Action::kPlaceTwo));

  // Seat B chooses White
  board.ApplyAction(Swap2Action::kChooseWhite);
  EXPECT_EQ(board.phase, GamePhase::kStandard);
  EXPECT_EQ(board.current_seat, Seat::kB);
  EXPECT_EQ(board.stone_to_place, Stone::kWhite);
}

TEST(GomokuTypesTest, ExactFiveWin) {
  BoardState board;
  // Fast-forward to standard phase
  board.phase = GamePhase::kStandard;
  board.current_seat = Seat::kA;
  board.stone_to_place = Stone::kBlack;

  for (int x = 0; x < 4; ++x) {
    board.ApplyAction(BoardState::ActionFromXY(x, 0));  // Black
    board.ApplyAction(BoardState::ActionFromXY(x, 1));  // White
  }

  // Black 5th stone
  board.ApplyAction(BoardState::ActionFromXY(4, 0));
  EXPECT_TRUE(board.IsTerminal());
  EXPECT_EQ(board.result, GameResult::kPlayerAWin);
}

TEST(GomokuTypesTest, OverlineDoesNotWin) {
  BoardState board;
  board.phase = GamePhase::kStandard;
  board.current_seat = Seat::kA;

  // Place 5 stones manually
  for (int x = 0; x < 5; ++x) {
    board.SetCell(x, 0, Stone::kBlack);
  }
  board.move_count = 5;

  // Place 6th stone (forming line of 6)
  board.stone_to_place = Stone::kBlack;
  board.ApplyAction(BoardState::ActionFromXY(5, 0));

  // In exact five Gomoku rules, 6 in a row is an overline and not a win
  EXPECT_EQ(board.result, GameResult::kUndetermined);
}

}  // namespace gomoku::plugin
