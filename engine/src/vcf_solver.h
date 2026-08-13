#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "board.h"
#include "endgame_solver.h"

constexpr int kVcfBoardSize = 15;
constexpr int kVcfNumCells = kVcfBoardSize * kVcfBoardSize;

enum class Stone : std::uint8_t {
  kEmpty = 0,
  kBlack = 1,
  kWhite = 2,
};

inline int VcfActionFromXY(int x, int y) { return y * kVcfBoardSize + x; }

inline int VcfActionX(int action) { return action % kVcfBoardSize; }

inline int VcfActionY(int action) { return action / kVcfBoardSize; }

inline Stone OtherStone(Stone stone) {
  return stone == Stone::kBlack ? Stone::kWhite : Stone::kBlack;
}

// A small, Swap2-free position used to exercise the solver independently of
// the engine's game-phase machinery. Action IDs are encoded as y * 15 + x,
// matching Board.
struct Position {
  explicit Position(Stone current_player = Stone::kBlack)
      : current_player(current_player) {
    cells.fill(Stone::kEmpty);
  }

  bool Set(int x, int y, Stone stone);
  bool Set(int action, Stone stone);
  Stone At(int x, int y) const;
  Stone At(int action) const;
  bool IsEmpty(int action) const;

  std::array<Stone, kVcfNumCells> cells;
  Stone current_player;
};

// Returns a deterministic, first-found winning VCF line for
// position.current_player. The line contains alternating attacker and
// forced-defender placements. An empty line means that no VCF was found.
std::vector<int> SolveVCF(const Position& position);

// Adapter for the production board. VCF is defined for standard play only;
// Swap2 control phases and terminal boards return an empty line.
std::vector<int> SolveVCF(const ::Board& board);

// An opponent's forcing VCF can be used to derive a defensive move set. The
// analysis probes each legal move for the current player, then asks whether
// the opponent has a VCF from the resulting position. It is conservative: a
// safe action is only known to avoid a VCF, not to win the game.
EndgameDefenseAnalysis AnalyzeVCFDefense(const Position& position);
EndgameDefenseAnalysis AnalyzeVCFDefense(const ::Board& board);
