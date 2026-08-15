#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gomoku::plugin {

constexpr int kBoardSize = 15;
constexpr int kNumBoardCells = kBoardSize * kBoardSize;  // 225
constexpr int kNumTotalActions = kNumBoardCells + 5;     // 230

enum class Stone : uint8_t {
  kEmpty = 0,
  kBlack = 1,
  kWhite = 2,
};

inline Stone OtherStone(Stone s) {
  if (s == Stone::kBlack) return Stone::kWhite;
  if (s == Stone::kWhite) return Stone::kBlack;
  return Stone::kEmpty;
}

enum class Seat : uint8_t {
  kA = 0,  // First player (places initial stones)
  kB = 1,  // Second player
};

inline Seat OtherSeat(Seat seat) {
  return seat == Seat::kA ? Seat::kB : Seat::kA;
}

enum class GamePhase : uint8_t {
  kPlaceInitialThree = 0,  // Seat A places 3 stones (2 Black, 1 White)
  kSwap2Decision = 1,      // Seat B chooses: White, Black (swap), or Place 2
  kSwap2PlaceTwo = 2,      // Seat B places 2 stones (1 Black, 1 White)
  kChooseColor = 3,        // Seat A chooses Black or White
  kStandard = 4            // Standard Gomoku alternating play
};

enum class GameResult : uint8_t {
  kUndetermined = 0,
  kPlayerAWin = 1,
  kPlayerBWin = 2,
  kDraw = 3,
};

namespace Swap2Action {
constexpr int kChooseWhite = 225;          // Seat B chooses White
constexpr int kChooseBlack = 226;          // Seat B chooses Black (swap)
constexpr int kPlaceTwo = 227;             // Seat B chooses to place two stones
constexpr int kChooseWhiteAfterTwo = 228;  // Seat A chooses White
constexpr int kChooseBlackAfterTwo = 229;  // Seat A chooses Black
}  // namespace Swap2Action

struct BoardState {
  std::array<Stone, kNumBoardCells> cells{};
  GamePhase phase = GamePhase::kPlaceInitialThree;
  Seat current_seat = Seat::kA;
  Stone stone_to_place = Stone::kBlack;
  int move_count = 0;
  GameResult result = GameResult::kUndetermined;

  Stone seat_a_stone = Stone::kEmpty;
  Stone seat_b_stone = Stone::kEmpty;

  BoardState();

  Stone cell(int x, int y) const { return cells[y * kBoardSize + x]; }
  Stone cell(int action_id) const { return cells[action_id]; }
  void SetCell(int x, int y, Stone s) { cells[y * kBoardSize + x] = s; }

  static bool IsPlacement(int action_id) {
    return action_id >= 0 && action_id < kNumBoardCells;
  }
  static int ActionFromXY(int x, int y) { return y * kBoardSize + x; }
  static int ActionX(int action_id) { return action_id % kBoardSize; }
  static int ActionY(int action_id) { return action_id / kBoardSize; }

  bool IsLegalAction(int action_id) const;
  std::vector<int> GetLegalActions() const;
  void ApplyAction(int action_id);
  bool IsTerminal() const { return result != GameResult::kUndetermined; }

  std::string ToString() const;

 private:
  bool CheckExactFive(int x, int y, Stone s) const;
  void TransitionPhase();
};

}  // namespace gomoku::plugin
