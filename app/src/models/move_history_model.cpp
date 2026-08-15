#include "models/move_history_model.h"

#include <QString>

namespace gomoku::app {

MoveHistoryModel::MoveHistoryModel(QObject* parent)
    : QAbstractListModel(parent) {}

int MoveHistoryModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(moves_.size());
}

QVariant MoveHistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(moves_.size())) {
    return QVariant();
  }

  const auto& move = moves_[index.row()];
  switch (role) {
    case PlyIndexRole:
      return move.ply_index;
    case ActionIdRole:
      return move.action_id;
    case ActionLabelRole:
      return QString::fromStdString(move.action_label);
    case SeatRole:
      return QString::fromStdString(move.seat);
    case StonePlacedRole:
      return QString::fromStdString(move.stone_placed);
    case PhaseRole:
      return QString::fromStdString(move.phase_at_move);
    case EstimatedWinRateRole:
      return move.estimated_win_rate.has_value()
                 ? QVariant(*move.estimated_win_rate)
                 : QVariant();
    case WinRateTextRole: {
      if (move.estimated_win_rate.has_value()) {
        float wr = *move.estimated_win_rate;
        // Normalize: if in [-1, 1], convert to [0, 100]%, or if in [0, 1] convert to [0, 100]%
        float pct = (wr >= -1.0f && wr <= 1.0f && wr < 0.0f)
                        ? (wr + 1.0f) * 50.0f
                        : (wr <= 1.0f ? wr * 100.0f : wr);
        return QString::asprintf("%.1f%%", pct);
      }
      return QString("-");
    }
    case TimeSpentRole:
      return move.time_spent_ms;
    case SummaryTextRole: {
      QString text = QString::asprintf("%d. Seat %s (%s): %s", move.ply_index,
                                       move.seat.c_str(),
                                       move.stone_placed.c_str(),
                                       move.action_label.c_str());
      return text;
    }
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> MoveHistoryModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[PlyIndexRole] = "plyIndex";
  roles[ActionIdRole] = "actionId";
  roles[ActionLabelRole] = "actionLabel";
  roles[SeatRole] = "seat";
  roles[StonePlacedRole] = "stonePlaced";
  roles[PhaseRole] = "phase";
  roles[EstimatedWinRateRole] = "estimatedWinRate";
  roles[WinRateTextRole] = "winRateText";
  roles[TimeSpentRole] = "timeSpent";
  roles[SummaryTextRole] = "summaryText";
  return roles;
}

void MoveHistoryModel::Clear() {
  beginResetModel();
  moves_.clear();
  endResetModel();
  emit moveCountChanged(0);
}

void MoveHistoryModel::AddMove(const MoveRecord& move) {
  beginInsertRows(QModelIndex(), static_cast<int>(moves_.size()),
                  static_cast<int>(moves_.size()));
  moves_.push_back(move);
  endInsertRows();
  emit moveCountChanged(static_cast<int>(moves_.size()));
}

void MoveHistoryModel::SetMoves(const std::vector<MoveRecord>& moves) {
  beginResetModel();
  moves_ = moves;
  endResetModel();
  emit moveCountChanged(static_cast<int>(moves_.size()));
}

}  // namespace gomoku::app
