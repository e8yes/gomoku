#include "core/board_view_model.h"

namespace gomoku::app {

BoardViewModel::BoardViewModel(QObject* parent) : QAbstractListModel(parent) {
  clearBoard();
}

int BoardViewModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(cells_.size());
}

QVariant BoardViewModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(cells_.size())) {
    return QVariant();
  }

  const auto& cell = cells_[index.row()];
  switch (role) {
    case CellXRole:
      return cell.x;
    case CellYRole:
      return cell.y;
    case StoneColorRole:
      return cell.stone_color;
    case MoveNumberRole:
      return cell.move_number;
    case IsLatestMoveRole:
      return cell.is_latest;
    case IsWinningFiveRole:
      return cell.is_winning_five;
    case IsGhostRole:
      return cell.is_ghost;
    case GhostColorRole:
      return cell.ghost_color;
    case CoordinateLabelRole:
      return QString::fromStdString(FormatCoordinate(cell.x, cell.y));
    case IsStarPointRole:
      return IsStarPoint(cell.x, cell.y);
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> BoardViewModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[CellXRole] = "cellX";
  roles[CellYRole] = "cellY";
  roles[StoneColorRole] = "stoneColor";
  roles[MoveNumberRole] = "moveNumber";
  roles[IsLatestMoveRole] = "isLatestMove";
  roles[IsWinningFiveRole] = "isWinningFive";
  roles[IsGhostRole] = "isGhost";
  roles[GhostColorRole] = "ghostColor";
  roles[CoordinateLabelRole] = "coordinateLabel";
  roles[IsStarPointRole] = "isStarPoint";
  return roles;
}

void BoardViewModel::clearBoard() {
  beginResetModel();
  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      int idx = y * kBoardSize + x;
      cells_[idx].x = x;
      cells_[idx].y = y;
      cells_[idx].stone_color = 0;
      cells_[idx].move_number = 0;
      cells_[idx].is_latest = false;
      cells_[idx].is_winning_five = false;
      cells_[idx].is_ghost = false;
      cells_[idx].ghost_color = 0;
    }
  }
  latest_action_ = -1;
  ghost_index_ = -1;
  endResetModel();
  emit boardReset();
}

void BoardViewModel::setGhostCell(int x, int y, int ghostColor) {
  if (x < 0 || x >= kBoardSize || y < 0 || y >= kBoardSize) {
    clearGhost();
    return;
  }

  int target_idx = y * kBoardSize + x;
  if (cells_[target_idx].stone_color != 0) {
    clearGhost();
    return;
  }

  if (ghost_index_ == target_idx && cells_[target_idx].ghost_color == ghostColor) {
    return;
  }

  // Clear previous ghost
  if (ghost_index_ >= 0 && ghost_index_ < static_cast<int>(cells_.size())) {
    cells_[ghost_index_].is_ghost = false;
    cells_[ghost_index_].ghost_color = 0;
    emit dataChanged(index(ghost_index_), index(ghost_index_),
                     {IsGhostRole, GhostColorRole});
  }

  ghost_index_ = target_idx;
  cells_[target_idx].is_ghost = true;
  cells_[target_idx].ghost_color = ghostColor;
  emit dataChanged(index(target_idx), index(target_idx),
                   {IsGhostRole, GhostColorRole});
}

void BoardViewModel::clearGhost() {
  if (ghost_index_ >= 0 && ghost_index_ < static_cast<int>(cells_.size())) {
    cells_[ghost_index_].is_ghost = false;
    cells_[ghost_index_].ghost_color = 0;
    emit dataChanged(index(ghost_index_), index(ghost_index_),
                     {IsGhostRole, GhostColorRole});
    ghost_index_ = -1;
  }
}

void BoardViewModel::SetCell(int x, int y, int stone_color, int move_number,
                             bool is_latest) {
  if (x < 0 || x >= kBoardSize || y < 0 || y >= kBoardSize) return;

  int idx = y * kBoardSize + x;
  if (latest_action_ >= 0 && latest_action_ < static_cast<int>(cells_.size()) &&
      is_latest) {
    cells_[latest_action_].is_latest = false;
    emit dataChanged(index(latest_action_), index(latest_action_),
                     {IsLatestMoveRole});
  }

  cells_[idx].stone_color = stone_color;
  cells_[idx].move_number = move_number;
  cells_[idx].is_latest = is_latest;
  cells_[idx].is_ghost = false;

  if (is_latest) {
    latest_action_ = idx;
  }

  emit dataChanged(index(idx), index(idx),
                   {StoneColorRole, MoveNumberRole, IsLatestMoveRole,
                    IsGhostRole});
  emit cellChanged(x, y);
}

void BoardViewModel::SetCellByAction(int action_id, int stone_color,
                                     int move_number, bool is_latest) {
  if (action_id < 0 || action_id >= kNumBoardCells) return;
  int x = action_id % kBoardSize;
  int y = action_id / kBoardSize;
  SetCell(x, y, stone_color, move_number, is_latest);
}

void BoardViewModel::SetWinningFive(const std::vector<int>& winning_actions) {
  for (int action_id : winning_actions) {
    if (action_id >= 0 && action_id < kNumBoardCells) {
      cells_[action_id].is_winning_five = true;
      emit dataChanged(index(action_id), index(action_id),
                       {IsWinningFiveRole});
    }
  }
}

void BoardViewModel::SyncFromBoardState(const gomoku::plugin::BoardState& state,
                                        int latest_action,
                                        const std::vector<int>& move_history) {
  beginResetModel();
  latest_action_ = latest_action;
  ghost_index_ = -1;

  // Build move order mapping from placement history if supplied
  std::array<int, kNumBoardCells> move_numbers{};
  int move_num = 1;
  for (int act : move_history) {
    if (act >= 0 && act < kNumBoardCells) {
      move_numbers[act] = move_num++;
    }
  }

  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      int idx = y * kBoardSize + x;
      cells_[idx].x = x;
      cells_[idx].y = y;
      cells_[idx].stone_color = static_cast<int>(state.cells[idx]);
      cells_[idx].move_number = move_numbers[idx];
      cells_[idx].is_latest = (idx == latest_action);
      cells_[idx].is_winning_five = false;
      cells_[idx].is_ghost = false;
      cells_[idx].ghost_color = 0;
    }
  }
  endResetModel();
  emit boardReset();
}

}  // namespace gomoku::app
