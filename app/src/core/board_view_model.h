#pragma once

#include <QAbstractListModel>
#include <array>
#include <string>
#include <vector>

#include "core/app_constants.h"
#include "plugin/gomoku_types.h"

namespace gomoku::app {

struct CellData {
  int x = 0;
  int y = 0;
  int stone_color = 0;  // 0 = Empty, 1 = Black, 2 = White
  int move_number = 0;  // 0 if empty
  bool is_latest = false;
  bool is_winning_five = false;
  bool is_ghost = false;
  int ghost_color = 0;
};

class BoardViewModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum CellRoles {
    CellXRole = Qt::UserRole + 1,
    CellYRole,
    StoneColorRole,
    MoveNumberRole,
    IsLatestMoveRole,
    IsWinningFiveRole,
    IsGhostRole,
    GhostColorRole,
    CoordinateLabelRole,
    IsStarPointRole
  };

  explicit BoardViewModel(QObject* parent = nullptr);
  ~BoardViewModel() override = default;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void clearBoard();
  Q_INVOKABLE void setGhostCell(int x, int y, int ghostColor);
  Q_INVOKABLE void clearGhost();

  void SetCell(int x, int y, int stone_color, int move_number, bool is_latest = false);
  void SetCellByAction(int action_id, int stone_color, int move_number, bool is_latest = false);
  void SetWinningFive(const std::vector<int>& winning_actions);
  void SyncFromBoardState(const gomoku::plugin::BoardState& state, int latest_action = -1,
                          const std::vector<int>& move_history = {});

  const CellData& GetCell(int x, int y) const { return cells_[y * kBoardSize + x]; }
  const CellData& GetCell(int index) const { return cells_[index]; }

 signals:
  void boardReset();
  void cellChanged(int x, int y);

 private:
  std::array<CellData, kNumBoardCells> cells_;
  int latest_action_{-1};
  int ghost_index_{-1};
};

}  // namespace gomoku::app
