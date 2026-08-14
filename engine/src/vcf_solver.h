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

constexpr int kDefaultMaxVcfNodes = 1000;

// Returns a deterministic, first-found winning VCF line for
// position.current_player within max_nodes search budget. The line contains
// alternating attacker and forced-defender placements. An empty line means
// that no VCF was found within budget.
std::vector<int> SolveVCF(const Position& position,
                          int max_nodes = kDefaultMaxVcfNodes);

// Adapter for the production board. VCF is defined for standard play only;
// Swap2 control phases and terminal boards return an empty line.
std::vector<int> SolveVCF(const ::Board& board,
                          int max_nodes = kDefaultMaxVcfNodes);

// An opponent's forcing VCF can be used to derive a defensive move set. The
// analysis probes candidate moves that interact with the discovered threat
// line (the line itself, its collinear neighborhoods, winning squares,
// immediate wins, and counter-fours), then asks whether the opponent has a
// VCF from the resulting position. It is conservative in both directions: a
// safe action is only known to avoid a VCF, not to win the game, and
// safe_actions may omit safe moves outside the candidate set (e.g. moves
// that only become a counter-four after a forced blocking reply).
EndgameDefenseAnalysis AnalyzeVCFDefense(const Position& position,
                                         int max_nodes = kDefaultMaxVcfNodes);
EndgameDefenseAnalysis AnalyzeVCFDefense(const ::Board& board,
                                         int max_nodes = kDefaultMaxVcfNodes);
