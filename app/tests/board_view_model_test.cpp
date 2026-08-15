#include <gtest/gtest.h>

#include "core/board_view_model.h"

namespace gomoku::app {

TEST(BoardViewModelTest, InitialState) {
  BoardViewModel model;
  EXPECT_EQ(model.rowCount(), 225);

  for (int i = 0; i < 225; ++i) {
    QModelIndex idx = model.index(i);
    EXPECT_EQ(model.data(idx, BoardViewModel::StoneColorRole).toInt(), 0);
    EXPECT_EQ(model.data(idx, BoardViewModel::MoveNumberRole).toInt(), 0);
    EXPECT_FALSE(model.data(idx, BoardViewModel::IsLatestMoveRole).toBool());
    EXPECT_FALSE(model.data(idx, BoardViewModel::IsWinningFiveRole).toBool());
    EXPECT_FALSE(model.data(idx, BoardViewModel::IsGhostRole).toBool());
  }
}

TEST(BoardViewModelTest, SetCellAndCoordinateLabels) {
  BoardViewModel model;

  // Set H8 (x=7, y=7) to Black, Move 1
  model.SetCell(7, 7, 1, 1, true);

  QModelIndex idx_h8 = model.index(7 * 15 + 7);
  EXPECT_EQ(model.data(idx_h8, BoardViewModel::StoneColorRole).toInt(), 1);
  EXPECT_EQ(model.data(idx_h8, BoardViewModel::MoveNumberRole).toInt(), 1);
  EXPECT_TRUE(model.data(idx_h8, BoardViewModel::IsLatestMoveRole).toBool());
  EXPECT_EQ(model.data(idx_h8, BoardViewModel::CoordinateLabelRole).toString().toStdString(), "H8");
  EXPECT_TRUE(model.data(idx_h8, BoardViewModel::IsStarPointRole).toBool());

  // Set H4 (x=7, y=11) to White, Move 2
  model.SetCell(7, 11, 2, 2, true);

  QModelIndex idx_h4 = model.index(11 * 15 + 7);
  EXPECT_EQ(model.data(idx_h4, BoardViewModel::StoneColorRole).toInt(), 2);
  EXPECT_EQ(model.data(idx_h4, BoardViewModel::MoveNumberRole).toInt(), 2);
  EXPECT_TRUE(model.data(idx_h4, BoardViewModel::IsLatestMoveRole).toBool());
  EXPECT_EQ(model.data(idx_h4, BoardViewModel::CoordinateLabelRole).toString().toStdString(), "H4");

  // Previous H8 should no longer be latest move
  EXPECT_FALSE(model.data(idx_h8, BoardViewModel::IsLatestMoveRole).toBool());
}

TEST(BoardViewModelTest, WinningFiveMarkers) {
  BoardViewModel model;
  std::vector<int> winning_cells = {
      gomoku::plugin::BoardState::ActionFromXY(7, 7),
      gomoku::plugin::BoardState::ActionFromXY(8, 7),
      gomoku::plugin::BoardState::ActionFromXY(9, 7),
      gomoku::plugin::BoardState::ActionFromXY(10, 7),
      gomoku::plugin::BoardState::ActionFromXY(11, 7),
  };

  model.SetWinningFive(winning_cells);

  for (int cell : winning_cells) {
    QModelIndex idx = model.index(cell);
    EXPECT_TRUE(model.data(idx, BoardViewModel::IsWinningFiveRole).toBool());
  }
}

TEST(BoardViewModelTest, GhostHover) {
  BoardViewModel model;

  model.setGhostCell(7, 7, 1);
  QModelIndex idx = model.index(7 * 15 + 7);
  EXPECT_TRUE(model.data(idx, BoardViewModel::IsGhostRole).toBool());
  EXPECT_EQ(model.data(idx, BoardViewModel::GhostColorRole).toInt(), 1);

  model.clearGhost();
  EXPECT_FALSE(model.data(idx, BoardViewModel::IsGhostRole).toBool());
}

TEST(BoardViewModelTest, SyncFromBoardState) {
  BoardViewModel model;
  gomoku::plugin::BoardState state;
  state.SetCell(7, 7, gomoku::plugin::Stone::kBlack);
  state.SetCell(7, 8, gomoku::plugin::Stone::kWhite);
  state.move_count = 2;

  std::vector<int> history = {
      gomoku::plugin::BoardState::ActionFromXY(7, 7),
      gomoku::plugin::BoardState::ActionFromXY(7, 8)};

  model.SyncFromBoardState(state, history.back(), history);

  QModelIndex idx1 = model.index(7 * 15 + 7);
  EXPECT_EQ(model.data(idx1, BoardViewModel::StoneColorRole).toInt(), 1);
  EXPECT_EQ(model.data(idx1, BoardViewModel::MoveNumberRole).toInt(), 1);

  QModelIndex idx2 = model.index(8 * 15 + 7);
  EXPECT_EQ(model.data(idx2, BoardViewModel::StoneColorRole).toInt(), 2);
  EXPECT_EQ(model.data(idx2, BoardViewModel::MoveNumberRole).toInt(), 2);
  EXPECT_TRUE(model.data(idx2, BoardViewModel::IsLatestMoveRole).toBool());
}

}  // namespace gomoku::app
