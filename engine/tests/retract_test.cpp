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
      0, 1, 2, Action::kSwap2ChooseWhite, 15*1+0, 15*14+1, 15*2+0, 15*14+2
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
      0, 1, 2, Action::kSwap2PlaceTwo, 15*1+0, 15*14+1, Action::kChooseBlack, 15*2+0, 15*14+2
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
