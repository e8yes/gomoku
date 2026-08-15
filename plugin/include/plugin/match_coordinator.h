#pragma once

#include <functional>
#include <memory>
#include <string>

#include "plugin/difficulty.h"
#include "plugin/gomoku_types.h"
#include "plugin/player_interface.h"

namespace gomoku::plugin {

// Callback invoked after each move is applied
using StepCallback = std::function<void(
    const BoardState& state, int last_action, IPlayer* moving_player)>;

class MatchCoordinator {
 public:
  MatchCoordinator();
  ~MatchCoordinator() = default;

  // Plays an entire match from start to finish synchronously.
  MatchResultInfo PlayMatch(IPlayer* player_a, IPlayer* player_b,
                            Difficulty diff_a = Difficulty::kVeteran,
                            Difficulty diff_b = Difficulty::kVeteran,
                            const StepCallback& step_callback = nullptr);

  // Step-by-step match execution (suitable for interactive GUI event loops)
  void StartMatch(IPlayer* player_a, IPlayer* player_b,
                  Difficulty diff_a = Difficulty::kVeteran,
                  Difficulty diff_b = Difficulty::kVeteran);

  // Executes one turn (inquires move from current player, applies it to board
  // and both players) Returns true if a valid move was executed, false if match
  // already ended.
  bool Step(const StepCallback& step_callback = nullptr);

  // Cancels / aborts the active match (resets players and terminates)
  void AbortMatch(const std::string& reason = "aborted");

  bool IsFinished() const { return finished_; }
  const BoardState& GetBoardState() const { return board_; }
  const MatchResultInfo& GetResult() const { return result_info_; }
  IPlayer* GetCurrentPlayer() const;

 private:
  IPlayer* player_a_{nullptr};
  IPlayer* player_b_{nullptr};
  BoardState board_;
  bool finished_{true};
  MatchResultInfo result_info_;
};

}  // namespace gomoku::plugin
