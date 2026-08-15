#include "models/match_history_model.h"

#include <QString>

namespace gomoku::app {

MatchHistoryModel::MatchHistoryModel(QObject* parent)
    : QAbstractListModel(parent) {
  refreshHistory();
}

int MatchHistoryModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(matches_.size());
}

QVariant MatchHistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(matches_.size())) {
    return QVariant();
  }

  const auto& match = matches_[index.row()];
  switch (role) {
    case MatchIdRole:
      return static_cast<qint64>(match.match_id);
    case CreatedAtRole:
      return QString::fromStdString(match.created_at);
    case PlayerANameRole:
      return QString::fromStdString(match.player_a_name);
    case PlayerBNameRole:
      return QString::fromStdString(match.player_b_name);
    case PlayerATypeRole:
      return QString::fromStdString(match.player_a_type);
    case PlayerBTypeRole:
      return QString::fromStdString(match.player_b_type);
    case DifficultyARole:
      return match.difficulty_a;
    case DifficultyBRole:
      return match.difficulty_b;
    case ResultRole:
      return QString::fromStdString(match.result);
    case WinnerSeatRole:
      return QString::fromStdString(match.winner_seat);
    case TerminationReasonRole:
      return QString::fromStdString(match.termination_reason);
    case TotalPliesRole:
      return match.total_plies;
    case OpeningPathRole:
      return match.opening_path;
    case WinnerTextRole: {
      if (match.result == "PLAYER_A_WIN") {
        return QString::fromStdString(match.player_a_name + " (Seat A)");
      } else if (match.result == "PLAYER_B_WIN") {
        return QString::fromStdString(match.player_b_name + " (Seat B)");
      } else if (match.result == "DRAW") {
        return QString("Draw");
      }
      return QString("In Progress");
    }
    case SummaryTextRole: {
      return QString::asprintf("#%lld: %s vs %s (%d plies, %s)",
                               static_cast<long long>(match.match_id),
                               match.player_a_name.c_str(),
                               match.player_b_name.c_str(),
                               match.total_plies,
                               match.result.c_str());
    }
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> MatchHistoryModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[MatchIdRole] = "matchId";
  roles[CreatedAtRole] = "createdAt";
  roles[PlayerANameRole] = "playerAName";
  roles[PlayerBNameRole] = "playerBName";
  roles[PlayerATypeRole] = "playerAType";
  roles[PlayerBTypeRole] = "playerBType";
  roles[DifficultyARole] = "difficultyA";
  roles[DifficultyBRole] = "difficultyB";
  roles[ResultRole] = "result";
  roles[WinnerSeatRole] = "winnerSeat";
  roles[TerminationReasonRole] = "terminationReason";
  roles[TotalPliesRole] = "totalPlies";
  roles[OpeningPathRole] = "openingPath";
  roles[WinnerTextRole] = "winnerText";
  roles[SummaryTextRole] = "summaryText";
  return roles;
}

void MatchHistoryModel::refreshHistory(int limit) {
  beginResetModel();
  matches_ = DatabaseManager::Instance().GetRecentMatches(limit);
  endResetModel();
  emit matchCountChanged(static_cast<int>(matches_.size()));
}

bool MatchHistoryModel::deleteMatch(qint64 matchId) {
  bool ok = DatabaseManager::Instance().DeleteMatch(matchId);
  if (ok) {
    refreshHistory();
  }
  return ok;
}

}  // namespace gomoku::app
