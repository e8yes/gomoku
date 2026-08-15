#include "plugin/gomoku_types.h"

#include <algorithm>
#include <sstream>

namespace gomoku::plugin {

BoardState::BoardState() {
  cells.fill(Stone::kEmpty);
  phase = GamePhase::kPlaceInitialThree;
  current_seat = Seat::kA;
  stone_to_place = Stone::kBlack;
  move_count = 0;
  result = GameResult::kUndetermined;
  seat_a_stone = Stone::kEmpty;
  seat_b_stone = Stone::kEmpty;
}

bool BoardState::IsLegalAction(int action_id) const {
  if (result != GameResult::kUndetermined) return false;

  switch (phase) {
    case GamePhase::kPlaceInitialThree:
    case GamePhase::kSwap2PlaceTwo:
    case GamePhase::kStandard:
      return IsPlacement(action_id) && cells[action_id] == Stone::kEmpty;

    case GamePhase::kSwap2Decision:
      return action_id == Swap2Action::kChooseWhite ||
             action_id == Swap2Action::kChooseBlack ||
             action_id == Swap2Action::kPlaceTwo;

    case GamePhase::kChooseColor:
      return action_id == Swap2Action::kChooseWhiteAfterTwo ||
             action_id == Swap2Action::kChooseBlackAfterTwo;
  }
  return false;
}

std::vector<int> BoardState::GetLegalActions() const {
  std::vector<int> legal;
  if (result != GameResult::kUndetermined) return legal;

  switch (phase) {
    case GamePhase::kPlaceInitialThree:
    case GamePhase::kSwap2PlaceTwo:
    case GamePhase::kStandard: {
      legal.reserve(kNumBoardCells - move_count);
      for (int i = 0; i < kNumBoardCells; ++i) {
        if (cells[i] == Stone::kEmpty) {
          legal.push_back(i);
        }
      }
      break;
    }
    case GamePhase::kSwap2Decision:
      legal = {Swap2Action::kChooseWhite, Swap2Action::kChooseBlack,
               Swap2Action::kPlaceTwo};
      break;

    case GamePhase::kChooseColor:
      legal = {Swap2Action::kChooseWhiteAfterTwo,
               Swap2Action::kChooseBlackAfterTwo};
      break;
  }
  return legal;
}

bool BoardState::CheckExactFive(int x, int y, Stone s) const {
  if (s == Stone::kEmpty) return false;

  constexpr int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

  for (const auto& d : dirs) {
    int dx = d[0];
    int dy = d[1];

    int count = 1;

    // Positive direction
    int nx = x + dx;
    int ny = y + dy;
    while (nx >= 0 && nx < kBoardSize && ny >= 0 && ny < kBoardSize &&
           cell(nx, ny) == s) {
      ++count;
      nx += dx;
      ny += dy;
    }

    // Negative direction
    nx = x - dx;
    ny = y - dy;
    while (nx >= 0 && nx < kBoardSize && ny >= 0 && ny < kBoardSize &&
           cell(nx, ny) == s) {
      ++count;
      nx -= dx;
      ny -= dy;
    }

    // Exact five rule: exactly 5 consecutive stones wins (overline does not win)
    if (count == 5) return true;
  }
  return false;
}

void BoardState::TransitionPhase() {
  switch (phase) {
    case GamePhase::kPlaceInitialThree:
      if (move_count == 1) {
        stone_to_place = Stone::kWhite;
      } else if (move_count == 2) {
        stone_to_place = Stone::kBlack;
      } else if (move_count == 3) {
        phase = GamePhase::kSwap2Decision;
        current_seat = Seat::kB;
        stone_to_place = Stone::kEmpty;
      }
      break;

    case GamePhase::kSwap2PlaceTwo:
      if (move_count == 4) {
        stone_to_place = Stone::kWhite;
      } else if (move_count == 5) {
        phase = GamePhase::kChooseColor;
        current_seat = Seat::kA;
        stone_to_place = Stone::kEmpty;
      }
      break;

    case GamePhase::kStandard:
      current_seat = OtherSeat(current_seat);
      stone_to_place = OtherStone(stone_to_place);
      break;

    default:
      break;
  }
}

void BoardState::ApplyAction(int action_id) {
  if (!IsLegalAction(action_id)) return;

  if (IsPlacement(action_id)) {
    const int x = ActionX(action_id);
    const int y = ActionY(action_id);
    cells[action_id] = stone_to_place;
    ++move_count;

    if (CheckExactFive(x, y, stone_to_place)) {
      if (phase == GamePhase::kStandard) {
        result = (current_seat == Seat::kA) ? GameResult::kPlayerAWin
                                            : GameResult::kPlayerBWin;
      } else {
        // Winning during opening placement
        result = (stone_to_place == Stone::kBlack) ? GameResult::kPlayerAWin
                                                   : GameResult::kPlayerBWin;
      }
      return;
    }

    if (move_count == kNumBoardCells) {
      result = GameResult::kDraw;
      return;
    }

    TransitionPhase();
  } else {
    // Swap2 decisions
    switch (action_id) {
      case Swap2Action::kChooseWhite:
        seat_b_stone = Stone::kWhite;
        seat_a_stone = Stone::kBlack;
        phase = GamePhase::kStandard;
        current_seat = Seat::kB;  // Move 4 is White's turn -> Seat B
        stone_to_place = Stone::kWhite;
        break;

      case Swap2Action::kChooseBlack:
        seat_b_stone = Stone::kBlack;
        seat_a_stone = Stone::kWhite;
        phase = GamePhase::kStandard;
        current_seat = Seat::kA;  // Move 4 is White's turn -> Seat A
        stone_to_place = Stone::kWhite;
        break;

      case Swap2Action::kPlaceTwo:
        phase = GamePhase::kSwap2PlaceTwo;
        current_seat = Seat::kB;
        stone_to_place = Stone::kBlack;  // Move 4 stone is Black
        break;

      case Swap2Action::kChooseWhiteAfterTwo:
        seat_a_stone = Stone::kWhite;
        seat_b_stone = Stone::kBlack;
        phase = GamePhase::kStandard;
        current_seat = Seat::kA;  // Move 6 is White's turn -> Seat A
        stone_to_place = Stone::kWhite;
        break;

      case Swap2Action::kChooseBlackAfterTwo:
        seat_a_stone = Stone::kBlack;
        seat_b_stone = Stone::kWhite;
        phase = GamePhase::kStandard;
        current_seat = Seat::kB;  // Move 6 is White's turn -> Seat B
        stone_to_place = Stone::kWhite;
        break;

      default:
        break;
    }
  }
}

std::string BoardState::ToString() const {
  std::ostringstream oss;
  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      Stone s = cell(x, y);
      if (s == Stone::kBlack) {
        oss << "X ";
      } else if (s == Stone::kWhite) {
        oss << "O ";
      } else {
        oss << ". ";
      }
    }
    oss << "\n";
  }
  return oss.str();
}

}  // namespace gomoku::plugin
