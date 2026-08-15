#include "core/game_controller.h"

#include <QMetaObject>
#include <iostream>

#include "core/app_constants.h"

namespace gomoku::app {

namespace {

int GetOpeningPathFromAction(int action_id) {
  if (action_id == gomoku::plugin::Swap2Action::kChooseWhite) return 1;
  if (action_id == gomoku::plugin::Swap2Action::kChooseBlack) return 2;
  if (action_id == gomoku::plugin::Swap2Action::kPlaceTwo) return 3;
  return 0;
}

}  // namespace

GameController::GameController(QObject* parent) : QObject(parent) {
  connect(&win_rate_timer_, &QTimer::timeout, this,
          &GameController::OnWinRatePollTimeout);
}

GameController::~GameController() { abortMatch(); }

QString GameController::gamePhase() const {
  if (is_game_over_) return "FINISHED";

  switch (board_state_.phase) {
    case gomoku::plugin::GamePhase::kPlaceInitialThree:
      return "PLACE_INITIAL_THREE";
    case gomoku::plugin::GamePhase::kSwap2Decision:
      return "SWAP2_DECISION";
    case gomoku::plugin::GamePhase::kSwap2PlaceTwo:
      return "SWAP2_PLACE_TWO";
    case gomoku::plugin::GamePhase::kChooseColor:
      return "CHOOSE_COLOR";
    case gomoku::plugin::GamePhase::kStandard:
      return "STANDARD";
    default:
      return "UNKNOWN";
  }
}

QString GameController::currentSeat() const {
  if (is_game_over_) return "-";
  return board_state_.current_seat == gomoku::plugin::Seat::kA ? "A" : "B";
}

int GameController::currentStoneToPlace() const {
  if (is_game_over_) return 0;
  return static_cast<int>(board_state_.stone_to_place);
}

bool GameController::isHumanTurn() const {
  if (is_game_over_) return false;
  auto* player = GetCurrentPlayer();
  if (!player) return true;
  return player->IsHuman();
}

int GameController::playerAStone() const {
  return static_cast<int>(board_state_.seat_a_stone);
}

int GameController::playerBStone() const {
  return static_cast<int>(board_state_.seat_b_stone);
}

QString GameController::playerAWinRateText() const {
  return QString::asprintf("%.1f%%", player_a_win_rate_ * 100.0);
}

QString GameController::playerBWinRateText() const {
  return QString::asprintf("%.1f%%", player_b_win_rate_ * 100.0);
}

QString GameController::gameResultText() const {
  if (!is_game_over_) return "Match in Progress";

  if (game_result_ == gomoku::plugin::GameResult::kPlayerAWin) {
    return QString("%1 (Seat A) Wins!").arg(player_a_name_);
  } else if (game_result_ == gomoku::plugin::GameResult::kPlayerBWin) {
    return QString("%1 (Seat B) Wins!").arg(player_b_name_);
  } else if (game_result_ == gomoku::plugin::GameResult::kDraw) {
    return "Match Ended in a Draw";
  }
  return "Match Concluded";
}

QString GameController::openingPromptText() const {
  if (is_game_over_) return "Match Finished";

  switch (board_state_.phase) {
    case gomoku::plugin::GamePhase::kPlaceInitialThree: {
      int stones_left = 3 - board_state_.move_count;
      QString stone_name =
          (board_state_.stone_to_place == gomoku::plugin::Stone::kBlack)
              ? "Black"
              : "White";
      return QString("Seat A (Opener): Place 3 opening stones (B-W-B). Next: %1 (%2 left)")
          .arg(stone_name)
          .arg(stones_left);
    }
    case gomoku::plugin::GamePhase::kSwap2Decision:
      return "Seat B (Responder): Choose White, Black (Swap), or Place 2 More Stones";
    case gomoku::plugin::GamePhase::kSwap2PlaceTwo: {
      int stones_left = 5 - board_state_.move_count;
      QString stone_name =
          (board_state_.stone_to_place == gomoku::plugin::Stone::kBlack)
              ? "Black"
              : "White";
      return QString("Seat B (Responder): Place 2 additional stones (W-B). Next: %1 (%2 left)")
          .arg(stone_name)
          .arg(stones_left);
    }
    case gomoku::plugin::GamePhase::kChooseColor:
      return "Seat A (Opener): Choose final color (White or Black)";
    case gomoku::plugin::GamePhase::kStandard: {
      QString current_player_name =
          (board_state_.current_seat == gomoku::plugin::Seat::kA)
              ? player_a_name_
              : player_b_name_;
      QString stone_name =
          (board_state_.stone_to_place == gomoku::plugin::Stone::kBlack)
              ? "Black"
              : "White";
      return QString("%1's Turn (%2)").arg(current_player_name).arg(stone_name);
    }
    default:
      return "";
  }
}

void GameController::startMatch(const QString& playerA, const QString& playerB,
                                int difficultyA, int difficultyB) {
  abortMatch();

  player_a_name_ = playerA.isEmpty() ? "Human A" : playerA;
  player_b_name_ = playerB.isEmpty() ? "Human B" : playerB;
  player_a_type_ = playerA;
  player_b_type_ = playerB;
  difficulty_a_ = difficultyA;
  difficulty_b_ = difficultyB;

  emit matchSetupChanged();

  // Instantiate Player A
  if (PluginRegistry::Instance().isEnginePlugin(playerA)) {
    player_a_ = PluginRegistry::Instance().CreatePlayer(playerA.toStdString());
  } else {
    player_a_ = std::make_unique<gomoku::plugin::HumanPlayer>(player_a_name_.toStdString());
  }

  // Instantiate Player B
  if (PluginRegistry::Instance().isEnginePlugin(playerB)) {
    player_b_ = PluginRegistry::Instance().CreatePlayer(playerB.toStdString());
  } else {
    player_b_ = std::make_unique<gomoku::plugin::HumanPlayer>(player_b_name_.toStdString());
  }

  // Reset board & state
  board_state_ = gomoku::plugin::BoardState();
  move_history_.clear();
  current_ply_count_ = 0;
  opening_path_ = 0;
  is_game_over_ = false;
  winner_seat_ = "";
  termination_reason_ = "";
  game_result_ = gomoku::plugin::GameResult::kUndetermined;
  player_a_win_rate_ = 0.5;
  player_b_win_rate_ = 0.5;

  board_model_.clearBoard();
  move_history_model_.Clear();

  // Record match start in SQLite database
  db_match_id_ = DatabaseManager::Instance().CreateMatch(
      player_a_name_.toStdString(), player_a_type_.toStdString(),
      player_b_name_.toStdString(), player_b_type_.toStdString(),
      difficulty_a_, difficulty_b_);

  // Notify players of match start
  gomoku::plugin::MatchSettings settings_a{
      gomoku::plugin::Seat::kA,
      static_cast<gomoku::plugin::Difficulty>(difficulty_a_),
      player_b_name_.toStdString(), "{}"};
  gomoku::plugin::MatchSettings settings_b{
      gomoku::plugin::Seat::kB,
      static_cast<gomoku::plugin::Difficulty>(difficulty_b_),
      player_a_name_.toStdString(), "{}"};

  if (player_a_) player_a_->OnMatchStart(settings_a);
  if (player_b_) player_b_->OnMatchStart(settings_b);

  last_move_time_ = std::chrono::steady_clock::now();

  // Start win rate polling timer
  win_rate_timer_.start(1000);

  emit matchStarted();
  emit phaseChanged();
  emit turnChanged();
  emit stonesAssigned();
  emit winRateUpdated();
  emit gameOverChanged();

  // If Seat A is AI, trigger AI search
  TriggerAiTurnIfNeeded();
}

void GameController::TriggerAiTurnIfNeeded() {
  if (is_game_over_) return;

  auto* player = GetCurrentPlayer();
  if (!player || player->IsHuman()) return;

  if (ai_thread_.joinable()) {
    ai_thread_.join();
  }

  is_ai_thinking_ = true;
  ai_thread_ = std::thread([this, player]() {
    // Perform AI search
    int action_id = player->InquireAction(board_state_);

    is_ai_thinking_ = false;

    // Dispatch action to Qt main thread
    QMetaObject::invokeMethod(this, [this, action_id]() {
      if (!is_game_over_) {
        ProcessMove(action_id);
      }
    }, Qt::QueuedConnection);
  });
}

void GameController::ProcessMove(int actionId,
                                 std::optional<float> win_rate_snapshot) {
  if (is_game_over_) return;

  auto now = std::chrono::steady_clock::now();
  int time_spent_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_move_time_)
          .count());
  last_move_time_ = now;

  // 1. Validate legality
  if (!board_state_.IsLegalAction(actionId)) {
    // Immediate forfeiture on illegal action
    std::string winner = (board_state_.current_seat == gomoku::plugin::Seat::kA)
                             ? "B"
                             : "A";
    auto result = (winner == "A") ? gomoku::plugin::GameResult::kPlayerAWin
                                  : gomoku::plugin::GameResult::kPlayerBWin;
    ConcludeMatch(result, winner, "illegal_action");
    return;
  }

  // 2. Query win rate snapshot before applying move
  if (!win_rate_snapshot.has_value()) {
    auto* player = GetCurrentPlayer();
    if (player) {
      win_rate_snapshot = player->GetEstimatedWinRate();
    }
  }

  // Record opening path if Swap2 control action
  if (opening_path_ == 0) {
    opening_path_ = GetOpeningPathFromAction(actionId);
  }

  // 3. Apply action to game rules state
  std::string seat_str =
      (board_state_.current_seat == gomoku::plugin::Seat::kA) ? "A" : "B";
  std::string stone_str =
      (board_state_.stone_to_place == gomoku::plugin::Stone::kBlack)
          ? "BLACK"
          : ((board_state_.stone_to_place == gomoku::plugin::Stone::kWhite)
                 ? "WHITE"
                 : "NONE");
  std::string phase_str = gamePhase().toStdString();
  std::string action_label = ActionToLabel(actionId);

  int x_coord = (actionId >= 0 && actionId < kNumBoardCells)
                    ? (actionId % kBoardSize)
                    : -1;
  int y_coord = (actionId >= 0 && actionId < kNumBoardCells)
                    ? (actionId / kBoardSize)
                    : -1;

  board_state_.ApplyAction(actionId);
  move_history_.push_back(actionId);
  current_ply_count_++;

  // 4. Notify both players of applied action
  if (player_a_) player_a_->ApplyAction(actionId);
  if (player_b_) player_b_->ApplyAction(actionId);

  // 5. Update BoardViewModel and MoveHistoryModel
  board_model_.SyncFromBoardState(board_state_, actionId, move_history_);

  MoveRecord move_rec;
  move_rec.match_id = db_match_id_;
  move_rec.ply_index = current_ply_count_;
  move_rec.action_id = actionId;
  move_rec.action_label = action_label;
  move_rec.x_coord = x_coord;
  move_rec.y_coord = y_coord;
  move_rec.seat = seat_str;
  move_rec.stone_placed = stone_str;
  move_rec.phase_at_move = phase_str;
  move_rec.estimated_win_rate = win_rate_snapshot;
  move_rec.time_spent_ms = time_spent_ms;

  move_history_model_.AddMove(move_rec);

  // 6. Record move in SQLite database
  if (db_match_id_ > 0) {
    DatabaseManager::Instance().RecordMove(
        db_match_id_, move_rec.ply_index, move_rec.action_id,
        move_rec.action_label, move_rec.x_coord, move_rec.y_coord,
        move_rec.seat, move_rec.stone_placed, move_rec.phase_at_move,
        move_rec.estimated_win_rate, move_rec.time_spent_ms);
  }

  // 7. Check if terminal state reached
  if (board_state_.IsTerminal()) {
    std::string winner = "";
    if (board_state_.result == gomoku::plugin::GameResult::kPlayerAWin) {
      winner = "A";
    } else if (board_state_.result == gomoku::plugin::GameResult::kPlayerBWin) {
      winner = "B";
    }
    std::string reason = (board_state_.result == gomoku::plugin::GameResult::kDraw)
                             ? "draw"
                             : "five_in_a_row";
    ConcludeMatch(board_state_.result, winner, reason);
    return;
  }

  emit moveApplied(actionId);
  emit phaseChanged();
  emit turnChanged();
  emit stonesAssigned();

  // If next player is AI, trigger AI search
  TriggerAiTurnIfNeeded();
}

void GameController::ConcludeMatch(gomoku::plugin::GameResult result,
                                   const std::string& winner,
                                   const std::string& reason) {
  is_game_over_ = true;
  game_result_ = result;
  winner_seat_ = QString::fromStdString(winner);
  termination_reason_ = QString::fromStdString(reason);

  win_rate_timer_.stop();

  // Notify players of match conclusion
  gomoku::plugin::MatchResultInfo result_info{
      result,
      (winner == "A"
           ? gomoku::plugin::Seat::kA
           : (winner == "B" ? gomoku::plugin::Seat::kB : gomoku::plugin::Seat::kA)),
      reason};

  if (player_a_) player_a_->OnMatchEnd(result_info);
  if (player_b_) player_b_->OnMatchEnd(result_info);

  // Update SQLite database match completion record
  std::string res_str = "DRAW";
  if (result == gomoku::plugin::GameResult::kPlayerAWin) res_str = "PLAYER_A_WIN";
  if (result == gomoku::plugin::GameResult::kPlayerBWin) res_str = "PLAYER_B_WIN";

  if (db_match_id_ > 0) {
    DatabaseManager::Instance().FinishMatch(
        db_match_id_, res_str, winner, reason, current_ply_count_, opening_path_);
  }

  // Refresh history model for main screen
  match_history_model_.refreshHistory();

  emit gameOverChanged();
  emit phaseChanged();
  emit turnChanged();
}

bool GameController::submitBoardClick(int x, int y) {
  if (is_game_over_ || !isHumanTurn()) return false;

  int action_id = gomoku::plugin::BoardState::ActionFromXY(x, y);
  if (!board_state_.IsLegalAction(action_id)) {
    return false;
  }

  ProcessMove(action_id);
  return true;
}

bool GameController::submitSwap2Action(int actionId) {
  if (is_game_over_ || !isHumanTurn()) return false;

  if (!board_state_.IsLegalAction(actionId)) {
    return false;
  }

  ProcessMove(actionId);
  return true;
}

void GameController::resignMatch() {
  if (is_game_over_) return;

  std::string winner =
      (board_state_.current_seat == gomoku::plugin::Seat::kA) ? "B" : "A";
  auto result = (winner == "A") ? gomoku::plugin::GameResult::kPlayerAWin
                                : gomoku::plugin::GameResult::kPlayerBWin;

  abortMatch();
  ConcludeMatch(result, winner, "resignation");
}

void GameController::abortMatch() {
  is_game_over_ = true;

  if (player_a_) player_a_->CancelInquiry();
  if (player_b_) player_b_->CancelInquiry();

  if (ai_thread_.joinable()) {
    ai_thread_.join();
  }

  win_rate_timer_.stop();
}

void GameController::OnWinRatePollTimeout() {
  if (is_game_over_) return;

  auto* player = GetCurrentPlayer();
  if (player) {
    auto wr_opt = player->GetEstimatedWinRate();
    if (wr_opt.has_value()) {
      float wr = *wr_opt;
      float norm_wr = (wr >= -1.0f && wr <= 1.0f && wr < 0.0f)
                          ? (wr + 1.0f) * 0.5f
                          : (wr <= 1.0f ? wr : 0.5f);

      if (board_state_.current_seat == gomoku::plugin::Seat::kA) {
        player_a_win_rate_ = norm_wr;
        player_b_win_rate_ = 1.0 - norm_wr;
      } else {
        player_b_win_rate_ = norm_wr;
        player_a_win_rate_ = 1.0 - norm_wr;
      }
      emit winRateUpdated();
    }
  }
}

gomoku::plugin::IPlayer* GameController::GetCurrentPlayer() const {
  if (board_state_.current_seat == gomoku::plugin::Seat::kA) {
    return player_a_.get();
  } else {
    return player_b_.get();
  }
}

}  // namespace gomoku::app
