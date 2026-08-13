#pragma once

#include <functional>
#include <vector>

#include "board.h"

// The callback must return a legal, forcing winning line for board's current
// player, or an empty vector when it cannot prove a VCF. MCTS uses the first
// action as the solved policy move and assigns the leaf value +1.0.
using EndgameSolver = std::function<std::vector<int>(const Board&)>;
