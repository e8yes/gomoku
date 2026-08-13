#pragma once

#include <functional>
#include <vector>

#include "board.h"

// The callback must return a legal, forcing winning line for board's current
// player, or an empty vector when it cannot prove a VCF. MCTS uses the first
// action as the solved policy move and assigns the leaf value +1.0.
using EndgameSolver = std::function<std::vector<int>(const Board&)>;

// A defensive analysis is intentionally separate from EndgameSolver. An
// empty safe_actions vector is ambiguous without threat_detected: it can mean
// either that no opponent VCF was found or that every candidate move loses.
struct EndgameDefenseAnalysis {
  bool threat_detected = false;
  std::vector<int> safe_actions;
};

// The callback identifies moves that do not expose the opponent to a proven
// VCF. MCTS uses it only to mask losing root priors when at least one safe move
// exists; it does not treat a safe move as a proven win.
using EndgameDefenseSolver =
    std::function<EndgameDefenseAnalysis(const Board&)>;
