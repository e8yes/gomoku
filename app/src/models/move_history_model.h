#pragma once

#include <QAbstractListModel>
#include <string>
#include <vector>

#include "core/database_manager.h"

namespace gomoku::app {

class MoveHistoryModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum MoveRoles {
    PlyIndexRole = Qt::UserRole + 1,
    ActionIdRole,
    ActionLabelRole,
    SeatRole,
    StonePlacedRole,
    PhaseRole,
    EstimatedWinRateRole,
    WinRateTextRole,
    TimeSpentRole,
    SummaryTextRole
  };

  explicit MoveHistoryModel(QObject* parent = nullptr);
  ~MoveHistoryModel() override = default;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void Clear();
  void AddMove(const MoveRecord& move);
  void SetMoves(const std::vector<MoveRecord>& moves);

  const std::vector<MoveRecord>& GetMoves() const { return moves_; }

 signals:
  void moveCountChanged(int count);

 private:
  std::vector<MoveRecord> moves_;
};

}  // namespace gomoku::app
