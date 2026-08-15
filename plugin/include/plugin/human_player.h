#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

#include "plugin/player_interface.h"

namespace gomoku::plugin {

class HumanPlayer : public IPlayer {
 public:
  explicit HumanPlayer(std::string name = "Human");
  ~HumanPlayer() override;

  // IPlayer implementation
  void OnMatchStart(const MatchSettings& settings) override;
  int InquireAction(const BoardState& board) override;
  void ApplyAction(int action_id) override;
  void OnMatchEnd(const MatchResultInfo& result_info) override;

  void CancelInquiry() override;
  std::string GetName() const override { return name_; }
  bool IsHuman() const override { return true; }

  // GUI Bridge Functions:
  // Called by QtQuick / GUI event handlers when human clicks a cell or selects
  // a Swap2 option.
  bool SubmitAction(int action_id);

  // Returns true if the human player is currently being inquired for a move.
  bool IsWaitingForAction() const;

  // Returns the latest match settings.
  MatchSettings GetMatchSettings() const;

 private:
  std::string name_;
  MatchSettings settings_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool waiting_for_action_{false};
  bool cancelled_{false};
  int submitted_action_{-1};
  BoardState current_board_;
};

}  // namespace gomoku::plugin
