#include "plugin/match_coordinator.h"

#include <stdexcept>

namespace gomoku::plugin {

MatchCoordinator::MatchCoordinator() {
  finished_ = true;
}

IPlayer* MatchCoordinator::GetCurrentPlayer() const {
  if (finished_ || !player_a_ || !player_b_) return nullptr;
  return (board_.current_seat == Seat::kA) ? player_a_ : player_b_;
}

void MatchCoordinator::StartMatch(IPlayer* player_a, IPlayer* player_b,
                                  Difficulty diff_a, Difficulty diff_b) {
  if (!player_a || !player_b) {
    throw std::invalid_argument("Player instances must not be null");
  }

  player_a_ = player_a;
  player_b_ = player_b;
  board_ = BoardState();
  finished_ = false;
  result_info_ = MatchResultInfo();

  MatchSettings settings_a{};
  settings_a.seat = Seat::kA;
  settings_a.difficulty = diff_a;
  settings_a.opponent_name = player_b_->GetName();

  MatchSettings settings_b{};
  settings_b.seat = Seat::kB;
  settings_b.difficulty = diff_b;
  settings_b.opponent_name = player_a_->GetName();

  player_a_->OnMatchStart(settings_a);
  player_b_->OnMatchStart(settings_b);
}

bool MatchCoordinator::Step(const StepCallback& step_callback) {
  if (finished_ || !player_a_ || !player_b_) return false;

  IPlayer* current_player = GetCurrentPlayer();
  if (!current_player) return false;

  const int action = current_player->InquireAction(board_);

  if (!board_.IsLegalAction(action)) {
    // Forfeit on illegal action
    finished_ = true;
    result_info_.result = (board_.current_seat == Seat::kA)
                              ? GameResult::kPlayerBWin
                              : GameResult::kPlayerAWin;
    result_info_.winner_seat =
        (board_.current_seat == Seat::kA) ? Seat::kB : Seat::kA;
    result_info_.termination_reason = "illegal_action";

    player_a_->OnMatchEnd(result_info_);
    player_b_->OnMatchEnd(result_info_);
    return false;
  }

  board_.ApplyAction(action);

  // Informs BOTH players the action actually applied
  player_a_->ApplyAction(action);
  player_b_->ApplyAction(action);

  if (step_callback) {
    step_callback(board_, action, current_player);
  }

  if (board_.IsTerminal()) {
    finished_ = true;
    result_info_.result = board_.result;
    if (board_.result == GameResult::kPlayerAWin) {
      result_info_.winner_seat = Seat::kA;
      result_info_.termination_reason = "five_in_a_row";
    } else if (board_.result == GameResult::kPlayerBWin) {
      result_info_.winner_seat = Seat::kB;
      result_info_.termination_reason = "five_in_a_row";
    } else {
      result_info_.termination_reason = "draw";
    }

    player_a_->OnMatchEnd(result_info_);
    player_b_->OnMatchEnd(result_info_);
  }

  return true;
}

MatchResultInfo MatchCoordinator::PlayMatch(IPlayer* player_a, IPlayer* player_b,
                                            Difficulty diff_a, Difficulty diff_b,
                                            const StepCallback& step_callback) {
  StartMatch(player_a, player_b, diff_a, diff_b);

  while (!IsFinished()) {
    if (!Step(step_callback)) {
      break;
    }
  }

  return result_info_;
}

void MatchCoordinator::AbortMatch(const std::string& reason) {
  if (finished_) return;

  finished_ = true;
  result_info_.result = GameResult::kUndetermined;
  result_info_.termination_reason = reason;

  if (player_a_) {
    player_a_->CancelInquiry();
    player_a_->OnMatchEnd(result_info_);
  }
  if (player_b_) {
    player_b_->CancelInquiry();
    player_b_->OnMatchEnd(result_info_);
  }
}

}  // namespace gomoku::plugin
