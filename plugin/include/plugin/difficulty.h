#pragma once

#include <optional>
#include <string>

namespace gomoku::plugin {

enum class Difficulty {
  kApprentice = 0,  // Beginner: Fast, high exploration, blunders possible
  kCasual = 1,      // Casual: Light tactical search, no deep threat solver
  kClub = 2,        // Club: Intermediate search, defensive tactical awareness
  kVeteran = 3,     // Veteran: Strong search, full attack and defense solving
  kChampion = 4,    // Champion: Master-level search, deterministic optimal moves
  kTruth = 5        // Truth: Maximum depth & simulation budget, proof-level search
};

const char* DifficultyToString(Difficulty diff);
std::optional<Difficulty> DifficultyFromString(const std::string& str);

}  // namespace gomoku::plugin
