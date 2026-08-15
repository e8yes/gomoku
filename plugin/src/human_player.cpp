#include "plugin/human_player.h"

namespace gomoku::plugin {

HumanPlayer::HumanPlayer(std::string name) : name_(std::move(name)) {}

HumanPlayer::~HumanPlayer() {
  CancelInquiry();
}

void HumanPlayer::OnMatchStart(const MatchSettings& settings) {
  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = settings;
  waiting_for_action_ = false;
  cancelled_ = false;
  submitted_action_ = -1;
}

int HumanPlayer::InquireAction(const BoardState& board) {
  std::unique_lock<std::mutex> lock(mutex_);
  current_board_ = board;
  waiting_for_action_ = true;
  cancelled_ = false;
  submitted_action_ = -1;

  cv_.wait(lock, [this]() {
    return !waiting_for_action_ || cancelled_ || submitted_action_ >= 0;
  });

  waiting_for_action_ = false;

  if (cancelled_) {
    return -1;
  }

  const int action = submitted_action_;
  submitted_action_ = -1;
  return action;
}

void HumanPlayer::ApplyAction(int /*action_id*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  // In case an external action was applied while waiting
  if (waiting_for_action_ && submitted_action_ < 0) {
    waiting_for_action_ = false;
    cv_.notify_all();
  }
}

void HumanPlayer::OnMatchEnd(const MatchResultInfo& /*result_info*/) {
  CancelInquiry();
}

void HumanPlayer::CancelInquiry() {
  std::lock_guard<std::mutex> lock(mutex_);
  cancelled_ = true;
  waiting_for_action_ = false;
  cv_.notify_all();
}

bool HumanPlayer::SubmitAction(int action_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!waiting_for_action_ || cancelled_) {
    return false;
  }
  submitted_action_ = action_id;
  waiting_for_action_ = false;
  cv_.notify_all();
  return true;
}

bool HumanPlayer::IsWaitingForAction() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return waiting_for_action_;
}

MatchSettings HumanPlayer::GetMatchSettings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

}  // namespace gomoku::plugin
