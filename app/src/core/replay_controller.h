#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>

#include "core/board_view_model.h"
#include "core/database_manager.h"
#include "models/move_history_model.h"
#include "plugin/gomoku_types.h"

namespace gomoku::app {

class ReplayController : public QObject {
  Q_OBJECT

  Q_PROPERTY(qint64 matchId READ matchId NOTIFY matchLoaded)
  Q_PROPERTY(int totalPlies READ totalPlies NOTIFY stateChanged)
  Q_PROPERTY(int currentPly READ currentPly WRITE seekToPly NOTIFY stateChanged)
  Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playStateChanged)
  Q_PROPERTY(int playbackSpeedMs READ playbackSpeedMs WRITE setPlaybackSpeed NOTIFY speedChanged)
  Q_PROPERTY(QString playerAName READ playerAName NOTIFY matchLoaded)
  Q_PROPERTY(QString playerBName READ playerBName NOTIFY matchLoaded)
  Q_PROPERTY(QString resultText READ resultText NOTIFY matchLoaded)
  Q_PROPERTY(QString terminationReason READ terminationReason NOTIFY matchLoaded)
  Q_PROPERTY(QString currentActionLabel READ currentActionLabel NOTIFY stateChanged)
  Q_PROPERTY(QString currentSeat READ currentSeat NOTIFY stateChanged)
  Q_PROPERTY(QString currentWinRateText READ currentWinRateText NOTIFY stateChanged)
  Q_PROPERTY(BoardViewModel* boardModel READ boardModel CONSTANT)
  Q_PROPERTY(MoveHistoryModel* moveHistoryModel READ moveHistoryModel CONSTANT)

 public:
  explicit ReplayController(QObject* parent = nullptr);
  ~ReplayController() override = default;

  qint64 matchId() const { return match_id_; }
  int totalPlies() const { return static_cast<int>(moves_.size()); }
  int currentPly() const { return current_ply_; }
  bool isPlaying() const { return play_timer_.isActive(); }
  int playbackSpeedMs() const { return play_speed_ms_; }
  QString playerAName() const { return QString::fromStdString(match_info_.player_a_name); }
  QString playerBName() const { return QString::fromStdString(match_info_.player_b_name); }
  QString resultText() const;
  QString terminationReason() const { return QString::fromStdString(match_info_.termination_reason); }
  QString currentActionLabel() const;
  QString currentSeat() const;
  QString currentWinRateText() const;

  BoardViewModel* boardModel() { return &board_model_; }
  MoveHistoryModel* moveHistoryModel() { return &move_history_model_; }

  Q_INVOKABLE bool loadMatch(qint64 matchId);
  Q_INVOKABLE void jumpToStart();
  Q_INVOKABLE void stepBackward();
  Q_INVOKABLE void stepForward();
  Q_INVOKABLE void jumpToEnd();
  Q_INVOKABLE void seekToPly(int ply);
  Q_INVOKABLE void togglePlay();
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void setPlaybackSpeed(int ms);

 signals:
  void matchLoaded();
  void stateChanged();
  void playStateChanged();
  void speedChanged();

 private slots:
  void OnPlayTimerTimeout();

 private:
  void ReconstructStateAtCurrentPly();

  qint64 match_id_{0};
  MatchRecord match_info_;
  std::vector<MoveRecord> moves_;
  int current_ply_{0};
  int play_speed_ms_{1000};

  QTimer play_timer_;
  BoardViewModel board_model_;
  MoveHistoryModel move_history_model_;
};

}  // namespace gomoku::app
