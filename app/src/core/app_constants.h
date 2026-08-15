#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace gomoku::app {

constexpr int kBoardSize = 15;
constexpr int kNumBoardCells = kBoardSize * kBoardSize;  // 225
constexpr int kNumTotalActions = kNumBoardCells + 5;     // 230

// Coordinate system: X in [0..14] -> 'A'..'O'
//                    Y in [0..14] -> '1'..'15' (1 at bottom / row 14, 15 at top / row 0 or 1-15 standard)
inline char ColumnToLetter(int x) {
  if (x < 0 || x >= kBoardSize) return '?';
  return static_cast<char>('A' + x);
}

inline int LetterToColumn(char c) {
  if (c >= 'a' && c <= 'o') c = static_cast<char>(c - 'a' + 'A');
  if (c >= 'A' && c <= 'O') return c - 'A';
  return -1;
}

inline std::string FormatCoordinate(int x, int y) {
  if (x < 0 || x >= kBoardSize || y < 0 || y >= kBoardSize) {
    return "Invalid";
  }
  // Standard Gomoku notation: Letter (A-O) + 1-based Row Number (1-15 from top or bottom)
  // Let's use A-O and 1-15 where Row 0 is 15 (top) or 1 (bottom).
  // Standard grid: y=0 is row 15, y=14 is row 1
  int row_num = 15 - y;
  return std::string(1, ColumnToLetter(x)) + std::to_string(row_num);
}

inline std::string ActionToLabel(int action_id) {
  if (action_id >= 0 && action_id < kNumBoardCells) {
    int x = action_id % kBoardSize;
    int y = action_id / kBoardSize;
    return FormatCoordinate(x, y);
  }
  switch (action_id) {
    case 225:
      return "swap2_choose_white";
    case 226:
      return "swap2_choose_black";
    case 227:
      return "swap2_place_two";
    case 228:
      return "choose_white";
    case 229:
      return "choose_black";
    default:
      return "unknown_action";
  }
}

// Star points (Hoshiboshi) at (3,3), (3,11), (7,7), (11,3), (11,11)
struct StarPoint {
  int x;
  int y;
};

constexpr std::array<StarPoint, 5> kStarPoints = {
    StarPoint{3, 3},    // D12
    StarPoint{11, 3},   // L12
    StarPoint{7, 7},    // H8 (Tengen)
    StarPoint{3, 11},   // D4
    StarPoint{11, 11},  // L4
};

inline bool IsStarPoint(int x, int y) {
  for (const auto& sp : kStarPoints) {
    if (sp.x == x && sp.y == y) return true;
  }
  return false;
}

}  // namespace gomoku::app
