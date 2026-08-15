#pragma once

#include <QAbstractListModel>
#include <string>
#include <vector>

#include "core/database_manager.h"

namespace gomoku::app {

class MatchHistoryModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum MatchRoles {
    MatchIdRole = Qt::UserRole + 1,
    CreatedAtRole,
    PlayerANameRole,
    PlayerBNameRole,
    PlayerATypeRole,
    PlayerBTypeRole,
    DifficultyARole,
    DifficultyBRole,
    ResultRole,
    WinnerSeatRole,
    TerminationReasonRole,
    TotalPliesRole,
    OpeningPathRole,
    SummaryTextRole,
    WinnerTextRole
  };

  explicit MatchHistoryModel(QObject* parent = nullptr);
  ~MatchHistoryModel() override = default;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void refreshHistory(int limit = 100);
  Q_INVOKABLE bool deleteMatch(qint64 matchId);

  const std::vector<MatchRecord>& GetMatches() const { return matches_; }

 signals:
  void matchCountChanged(int count);

 private:
  std::vector<MatchRecord> matches_;
};

}  // namespace gomoku::app
