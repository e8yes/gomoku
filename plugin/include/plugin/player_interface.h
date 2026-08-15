#pragma once

#include <optional>
#include <string>
#include <vector>

#include "plugin/difficulty.h"
#include "plugin/gomoku_types.h"

namespace gomoku::plugin {

struct MatchSettings {
  Seat seat = Seat::kA;
  Difficulty difficulty = Difficulty::kVeteran;
  std::string opponent_name = "Opponent";
  std::string custom_config_json = "{}";
};

struct MatchResultInfo {
  GameResult result = GameResult::kUndetermined;
  Seat winner_seat = Seat::kA;
  std::string
      termination_reason;  // "five_in_a_row", "resignation", "draw", etc.
};

class IPlayer {
 public:
  virtual ~IPlayer() = default;

  // Informs the engine or human player of the beginning of a match.
  // The difficulty level is supplied via settings.difficulty (one of 6 levels).
  virtual void OnMatchStart(const MatchSettings& settings) = 0;

  // Inquires the next optimal action given the current board state.
  // Returns the chosen action ID (0-224 for placement, 225-229 for Swap2
  // choices).
  virtual int InquireAction(const BoardState& board) = 0;

  // Informs the engine or player of the action actually applied to the board
  // state after an inquiry or the opponent's move.
  virtual void ApplyAction(int action_id) = 0;

  // Informs the engine or human player of the end of a match.
  virtual void OnMatchEnd(const MatchResultInfo& result_info) = 0;

  // Optional: returns current estimated win rate (e.g. in [-1.0, 1.0] or
  // [0.0, 1.0]). Can be called concurrently while an action inquiry is still
  // in-flight.
  virtual std::optional<float> GetEstimatedWinRate() const {
    return std::nullopt;
  }

  // Optional: returns current policy (action probability distribution over all
  // 230 actions). Can be called concurrently while an action inquiry is still
  // in-flight.
  virtual std::optional<std::vector<float>> GetPolicy() const {
    return std::nullopt;
  }

  // Cancels / aborts an active in-flight action inquiry.
  virtual void CancelInquiry() {}

  // Player name/identifier.
  virtual std::string GetName() const = 0;

  // Returns true if this player represents a human user.
  virtual bool IsHuman() const { return false; }
};

}  // namespace gomoku::plugin
