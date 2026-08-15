#include "plugin/difficulty.h"

#include <algorithm>
#include <cctype>

namespace gomoku::plugin {

const char* DifficultyToString(Difficulty diff) {
  switch (diff) {
    case Difficulty::kApprentice:
      return "Apprentice";
    case Difficulty::kCasual:
      return "Casual";
    case Difficulty::kClub:
      return "Club";
    case Difficulty::kVeteran:
      return "Veteran";
    case Difficulty::kChampion:
      return "Champion";
    case Difficulty::kTruth:
      return "Truth";
  }
  return "Unknown";
}

std::optional<Difficulty> DifficultyFromString(const std::string& str) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "apprentice") return Difficulty::kApprentice;
  if (lower == "casual") return Difficulty::kCasual;
  if (lower == "club") return Difficulty::kClub;
  if (lower == "veteran") return Difficulty::kVeteran;
  if (lower == "champion") return Difficulty::kChampion;
  if (lower == "truth") return Difficulty::kTruth;

  return std::nullopt;
}

}  // namespace gomoku::plugin
