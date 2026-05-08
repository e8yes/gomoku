#pragma once

#include <array>
#include <string>
#include <vector>

enum class Player { kNone = 0, kBlack = 1, kWhite = 2 };

enum class Seat { kA = 0, kB = 1 };

enum class Phase {
  kPlaceInitialThree = 0,
  kSwap2Decision = 1,
  kSwap2PlaceTwo = 2,
  kChooseColor = 3,
  kStandard = 4
};

enum class Result {
  kUndetermined = 0,
  kPlayerAWin = 1,
  kPlayerBWin = 2,
  kDraw = 3
};

struct Action {
  static constexpr int kSwap2ChooseWhite = 225;
  static constexpr int kSwap2ChooseBlack = 226;
  static constexpr int kSwap2PlaceTwo = 227;
  static constexpr int kChooseWhite = 228;
  static constexpr int kChooseBlack = 229;

  int id;  // 0-224 are placements (y*15 + x), 225-229 are swap2 actions.

  Action(int i) : id(i) {}
  bool IsPlacement() const { return id >= 0 && id < 225; }
};

class Board {
 public:
  static constexpr int kSize = 15;
  static constexpr int kNumCells = kSize * kSize;
  static constexpr int kNumActions = kNumCells + 5;

  Board();

  // Returns a list of legal action IDs.
  std::vector<int> GetLegalActions() const;

  // Apply action and transition state. Assumes the action is legal.
  void Apply(int action_id);

  // Retract action. This is fully copyless and purely deterministic, perfectly 
  // suited for minimax perturbation and highly-efficient tree unmaking.
  void Retract(int action_id);

  // Get current seat to move.
  Seat current_player() const { return current_player_; }
  Player stone_to_place() const { return stone_to_place_; }
  Phase phase() const { return phase_; }
  Result result() const { return result_; }

  // Returns stone at (x, y)
  Player cell(int x, int y) const { return cells_[y * kSize + x]; }

  // Check if the board is in a terminal state
  bool IsTerminal() const { return result_ != Result::kUndetermined; }

  // Evaluates value from a specific seat's perspective
  // Returns 1.0 for win, -1.0 for loss, 0.0 for draw or undetermined.
  float GetValueForSeat(Seat seat) const;

 private:
  std::array<Player, kNumCells> cells_;
  Phase phase_;
  Seat current_player_;
  Player stone_to_place_;
  int move_count_;  // Total placements

  Player seat_a_stone_;
  Player seat_b_stone_;

  Result result_;

  bool CheckWin(int x, int y, Player p) const;
  void TransitionPhase();
};
