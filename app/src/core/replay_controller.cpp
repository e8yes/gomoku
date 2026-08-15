#include "core/replay_controller.h"

#include <algorithm>

namespace gomoku::app {

ReplayController::ReplayController(QObject* parent) : QObject(parent) {
  connect(&play_timer_, &QTimer::timeout, this,
          &ReplayController::OnPlayTimerTimeout);
}

bool ReplayController::loadMatch(qint64 matchId) {
  pause();

  auto match_opt = DatabaseManager::Instance().GetMatchById(matchId);
  if (!match_opt.has_value()) {
    return false;
  }

  match_id_ = matchId;
  match_info_ = *match_opt;
  moves_ = DatabaseManager::Instance().GetMovesForMatch(matchId);

  move_history_model_.SetMoves(moves_);

  current_ply_ = 0;
  ReconstructStateAtCurrentPly();

  emit matchLoaded();
  emit stateChanged();
  return true;
}

QString ReplayController::resultText() const {
  if (match_info_.result == "PLAYER_A_WIN") {
    return QString::fromStdString(match_info_.player_a_name + " Won (Seat A)");
  } else if (match_info_.result == "PLAYER_B_WIN") {
    return QString::fromStdString(match_info_.player_b_name + " Won (Seat B)");
  } else if (match_info_.result == "DRAW") {
    return "Draw";
  }
  return "Undetermined";
}

QString ReplayController::currentActionLabel() const {
  if (current_ply_ <= 0 || current_ply_ > static_cast<int>(moves_.size())) {
    return "Initial Board";
  }
  return QString::fromStdString(moves_[current_ply_ - 1].action_label);
}

QString ReplayController::currentSeat() const {
  if (current_ply_ <= 0 || current_ply_ > static_cast<int>(moves_.size())) {
    return "-";
  }
  return QString::fromStdString(moves_[current_ply_ - 1].seat);
}

QString ReplayController::currentWinRateText() const {
  if (current_ply_ <= 0 || current_ply_ > static_cast<int>(moves_.size())) {
    return "-";
  }
  const auto& move = moves_[current_ply_ - 1];
  if (move.estimated_win_rate.has_value()) {
    float wr = *move.estimated_win_rate;
    float pct = (wr >= -1.0f && wr <= 1.0f && wr < 0.0f)
                    ? (wr + 1.0f) * 50.0f
                    : (wr <= 1.0f ? wr * 100.0f : wr);
    return QString::asprintf("%.1f%%", pct);
  }
  return "-";
}

void ReplayController::jumpToStart() {
  pause();
  seekToPly(0);
}

void ReplayController::stepBackward() {
  pause();
  if (current_ply_ > 0) {
    seekToPly(current_ply_ - 1);
  }
}

void ReplayController::stepForward() {
  if (current_ply_ < static_cast<int>(moves_.size())) {
    seekToPly(current_ply_ + 1);
  } else {
    pause();
  }
}

void ReplayController::jumpToEnd() {
  pause();
  seekToPly(static_cast<int>(moves_.size()));
}

void ReplayController::seekToPly(int ply) {
  int target = std::clamp(ply, 0, static_cast<int>(moves_.size()));
  if (current_ply_ == target) return;

  current_ply_ = target;
  ReconstructStateAtCurrentPly();
  emit stateChanged();
}

void ReplayController::togglePlay() {
  if (isPlaying()) {
    pause();
  } else {
    play();
  }
}

void ReplayController::play() {
  if (current_ply_ >= static_cast<int>(moves_.size())) {
    seekToPly(0);
  }
  play_timer_.start(play_speed_ms_);
  emit playStateChanged();
}

void ReplayController::pause() {
  if (play_timer_.isActive()) {
    play_timer_.stop();
    emit playStateChanged();
  }
}

void ReplayController::setPlaybackSpeed(int ms) {
  play_speed_ms_ = std::max(100, ms);
  if (play_timer_.isActive()) {
    play_timer_.start(play_speed_ms_);
  }
  emit speedChanged();
}

void ReplayController::OnPlayTimerTimeout() {
  if (current_ply_ < static_cast<int>(moves_.size())) {
    seekToPly(current_ply_ + 1);
  } else {
    pause();
  }
}

void ReplayController::ReconstructStateAtCurrentPly() {
  board_model_.clearBoard();

  int move_num = 1;

  for (int i = 0; i < current_ply_; ++i) {
    const auto& move = moves_[i];
    if (move.action_id >= 0 && move.action_id < kNumBoardCells) {
      int color = (move.stone_placed == "BLACK" || move.stone_placed == "1")
                      ? 1
                      : ((move.stone_placed == "WHITE" || move.stone_placed == "2") ? 2 : 0);
      bool is_latest = (i == current_ply_ - 1);
      board_model_.SetCellByAction(move.action_id, color, move_num++, is_latest);
    }
  }
}

}  // namespace gomoku::app
