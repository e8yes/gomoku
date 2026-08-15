#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/board_view_model.h"
#include "core/database_manager.h"
#include "core/plugin_registry.h"
#include "models/match_history_model.h"
#include "models/move_history_model.h"
#include "plugin/difficulty.h"
#include "plugin/gomoku_types.h"
#include "plugin/human_player.h"
#include "plugin/player_interface.h"

namespace gomoku::app {

class GameController : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString gamePhase READ gamePhase NOTIFY phaseChanged)
  Q_PROPERTY(QString currentSeat READ currentSeat NOTIFY turnChanged)
  Q_PROPERTY(int currentStoneToPlace READ currentStoneToPlace NOTIFY turnChanged)
  Q_PROPERTY(bool isHumanTurn READ isHumanTurn NOTIFY turnChanged)
  Q_PROPERTY(bool isAiThinking READ isAiThinking NOTIFY aiThinkingChanged)
  Q_PROPERTY(QString playerAName READ playerAName NOTIFY matchSetupChanged)
  Q_PROPERTY(QString playerBName READ playerBName NOTIFY matchSetupChanged)
  Q_PROPERTY(QString playerAType READ playerAType NOTIFY matchSetupChanged)
  Q_PROPERTY(QString playerBType READ playerBType NOTIFY matchSetupChanged)
  Q_PROPERTY(int playerAStone READ playerAStone NOTIFY stonesAssigned)
  Q_PROPERTY(int playerBStone READ playerBStone NOTIFY stonesAssigned)
  Q_PROPERTY(double playerAWinRate READ playerAWinRate NOTIFY winRateUpdated)
  Q_PROPERTY(double playerBWinRate READ playerBWinRate NOTIFY winRateUpdated)
  Q_PROPERTY(QString playerAWinRateText READ playerAWinRateText NOTIFY winRateUpdated)
  Q_PROPERTY(QString playerBWinRateText READ playerBWinRateText NOTIFY winRateUpdated)
  Q_PROPERTY(bool isGameOver READ isGameOver NOTIFY gameOverChanged)
  Q_PROPERTY(QString winnerSeat READ winnerSeat NOTIFY gameOverChanged)
  Q_PROPERTY(QString gameResultText READ gameResultText NOTIFY gameOverChanged)
  Q_PROPERTY(QString terminationReason READ terminationReason NOTIFY gameOverChanged)
  Q_PROPERTY(QString openingPromptText READ openingPromptText NOTIFY phaseChanged)
  Q_PROPERTY(int moveCount READ moveCount NOTIFY moveApplied)
  Q_PROPERTY(BoardViewModel* boardModel READ boardModel CONSTANT)
  Q_PROPERTY(MoveHistoryModel* moveHistoryModel READ moveHistoryModel CONSTANT)
  Q_PROPERTY(MatchHistoryModel* matchHistoryModel READ matchHistoryModel CONSTANT)

 public:
  explicit GameController(QObject* parent = nullptr);
  ~GameController() override;

  QString gamePhase() const;
  QString currentSeat() const;
  int currentStoneToPlace() const;
  bool isHumanTurn() const;
  bool isAiThinking() const { return is_ai_thinking_; }
  QString playerAName() const { return player_a_name_; }
  QString playerBName() const { return player_b_name_; }
  QString playerAType() const { return player_a_type_; }
  QString playerBType() const { return player_b_type_; }
  int playerAStone() const;
  int playerBStone() const;
  double playerAWinRate() const { return player_a_win_rate_; }
  double playerBWinRate() const { return player_b_win_rate_; }
  QString playerAWinRateText() const;
  QString playerBWinRateText() const;
  bool isGameOver() const { return is_game_over_; }
  QString winnerSeat() const { return winner_seat_; }
  QString gameResultText() const;
  QString terminationReason() const { return termination_reason_; }
  QString openingPromptText() const;
  int moveCount() const { return board_state_.move_count; }

  BoardViewModel* boardModel() { return &board_model_; }
  MoveHistoryModel* moveHistoryModel() { return &move_history_model_; }
  MatchHistoryModel* matchHistoryModel() { return &match_history_model_; }

  Q_INVOKABLE void startMatch(const QString& playerA, const QString& playerB,
                              int difficultyA = 3, int difficultyB = 3);
  Q_INVOKABLE bool submitBoardClick(int x, int y);
  Q_INVOKABLE bool submitSwap2Action(int actionId);
  Q_INVOKABLE void resignMatch(const QString& resigningSeat = "");
  Q_INVOKABLE void abortMatch();

 signals:
  void matchStarted();
  void phaseChanged();
  void turnChanged();
  void stonesAssigned();
  void winRateUpdated();
  void gameOverChanged();
  void aiThinkingChanged();
  void moveApplied(int actionId);
  void matchSetupChanged();
  void errorMessage(const QString& message);

 private slots:
  void OnWinRatePollTimeout();

 private:
  void TriggerAiTurnIfNeeded();
  void ProcessMove(int actionId, std::optional<float> win_rate_snapshot = std::nullopt);
  void ConcludeMatch(gomoku::plugin::GameResult result,
                     const std::string& winner, const std::string& reason);
  gomoku::plugin::IPlayer* GetCurrentPlayer() const;

  BoardViewModel board_model_;
  MoveHistoryModel move_history_model_;
  MatchHistoryModel match_history_model_;

  QTimer win_rate_timer_;

  QString player_a_name_{"Human A"};
  QString player_b_name_{"Human B"};
  QString player_a_type_{"Human"};
  QString player_b_type_{"Human"};
  int difficulty_a_{3};
  int difficulty_b_{3};

  std::unique_ptr<gomoku::plugin::IPlayer> player_a_;
  std::unique_ptr<gomoku::plugin::IPlayer> player_b_;

  gomoku::plugin::BoardState board_state_;
  std::vector<int> move_history_;
  int64_t db_match_id_{-1};
  int opening_path_{0};
  int current_ply_count_{0};

  bool is_game_over_{true};
  QString winner_seat_{""};
  QString termination_reason_{""};
  gomoku::plugin::GameResult game_result_{gomoku::plugin::GameResult::kUndetermined};

  double player_a_win_rate_{0.5};
  double player_b_win_rate_{0.5};

  std::thread ai_thread_;
  std::atomic<bool> is_ai_thinking_{false};
  std::atomic<uint64_t> match_epoch_{0};
  std::chrono::steady_clock::time_point last_move_time_;
};

}  // namespace gomoku::app
