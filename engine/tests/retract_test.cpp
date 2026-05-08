#include "board.h"
#include <gtest/gtest.h>
#include <vector>

void CompareBoards(const Board& a, const Board& b) {
  EXPECT_EQ(a.phase(), b.phase());
  EXPECT_EQ(a.current_player(), b.current_player());
  EXPECT_EQ(a.stone_to_place(), b.stone_to_place());
  EXPECT_EQ(a.result(), b.result());
  for (int i = 0; i < Board::kNumCells; ++i) {
    EXPECT_EQ(a.cell(i % 15, i / 15), b.cell(i % 15, i / 15));
  }
}

TEST(BoardTest, RetractStandard) {
  Board board;
  std::vector<int> moves = {
      Action::FromXY(0, 0).id, Action::FromXY(1, 0).id, Action::FromXY(2, 0).id, Action::kSwap2ChooseWhite, Action::FromXY(0, 1).id, Action::FromXY(1, 14).id, Action::FromXY(0, 2).id, Action::FromXY(2, 14).id
  };
  
  std::vector<Board> history;
  history.push_back(board);
  
  for (int m : moves) {
    board.Apply(m);
    history.push_back(board);
  }
  
  for (int i = moves.size() - 1; i >= 0; --i) {
    board.Retract(moves[i]);
    CompareBoards(board, history[i]);
  }
}

TEST(BoardTest, RetractSwap2PlaceTwo) {
  Board board;
  std::vector<int> moves = {
      Action::FromXY(0, 0).id, Action::FromXY(1, 0).id, Action::FromXY(2, 0).id, Action::kSwap2PlaceTwo, Action::FromXY(0, 1).id, Action::FromXY(1, 14).id, Action::kChooseBlack, Action::FromXY(0, 2).id, Action::FromXY(2, 14).id
  };
  
  std::vector<Board> history;
  history.push_back(board);
  
  for (int m : moves) {
    board.Apply(m);
    history.push_back(board);
  }
  
  for (int i = moves.size() - 1; i >= 0; --i) {
    board.Retract(moves[i]);
    CompareBoards(board, history[i]);
  }
}

TEST(BoardTest, RetractWinningMove) {
  Board board;
  std::vector<int> moves = {
      Action::FromXY(0, 0).id, Action::FromXY(0, 1).id, Action::FromXY(1, 0).id, Action::kSwap2ChooseWhite,
      // now W is current player. B is Seat A. W is Seat B.
      Action::FromXY(1, 1).id, Action::FromXY(2, 0).id, Action::FromXY(2, 1).id, Action::FromXY(3, 0).id, Action::FromXY(3, 1).id, Action::FromXY(4, 0).id
  };
  
  std::vector<Board> history;
  history.push_back(board);
  for (int m : moves) {
    board.Apply(m);
    history.push_back(board);
  }
  
  for (int i = moves.size() - 1; i >= 0; --i) {
    board.Retract(moves[i]);
    CompareBoards(board, history[i]);
  }
}
