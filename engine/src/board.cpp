#include "board.h"

#include <stdexcept>
#include <sstream>

std::string Action::ToString() const {
  if (id == kSwap2ChooseWhite) return "swap2_choose_white";
  if (id == kSwap2ChooseBlack) return "swap2_choose_black";
  if (id == kSwap2PlaceTwo) return "swap2_place_two";
  if (id == kChooseWhite) return "choose_white";
  if (id == kChooseBlack) return "choose_black";
  
  if (IsPlacement()) {
    std::ostringstream oss;
    oss << "(" << x() << "," << y() << ")";
    return oss.str();
  }
  
  return "invalid_action";
}

Action Action::FromString(const std::string& str) {
  if (str == "swap2_choose_white") return Action(kSwap2ChooseWhite);
  if (str == "swap2_choose_black") return Action(kSwap2ChooseBlack);
  if (str == "swap2_place_two") return Action(kSwap2PlaceTwo);
  if (str == "choose_white") return Action(kChooseWhite);
  if (str == "choose_black") return Action(kChooseBlack);
  
  if (str.length() >= 5 && str.front() == '(' && str.back() == ')') {
    size_t comma = str.find(',');
    if (comma != std::string::npos) {
      int x = std::stoi(str.substr(1, comma - 1));
      int y = std::stoi(str.substr(comma + 1, str.length() - comma - 2));
      return FromXY(x, y);
    }
  }
  
  throw std::invalid_argument("Invalid action string: " + str);
}

Board::Board() {
  cells_.fill(Player::kNone);
  phase_ = Phase::kPlaceInitialThree;
  current_player_ = Seat::kA;
  stone_to_place_ = Player::kBlack;
  move_count_ = 0;
  seat_a_stone_ = Player::kNone;
  seat_b_stone_ = Player::kNone;
  result_ = Result::kUndetermined;
}

std::vector<int> Board::GetLegalActions() const {
  std::vector<int> actions;
  if (result_ != Result::kUndetermined) {
    return actions;
  }

  if (phase_ == Phase::kPlaceInitialThree || phase_ == Phase::kSwap2PlaceTwo ||
      phase_ == Phase::kStandard) {
    for (int i = 0; i < kNumCells; ++i) {
      if (cells_[i] == Player::kNone) {
        actions.push_back(i);
      }
    }
  } else if (phase_ == Phase::kSwap2Decision) {
    actions.push_back(Action::kSwap2ChooseWhite);
    actions.push_back(Action::kSwap2ChooseBlack);
    actions.push_back(Action::kSwap2PlaceTwo);
  } else if (phase_ == Phase::kChooseColor) {
    actions.push_back(Action::kChooseWhite);
    actions.push_back(Action::kChooseBlack);
  }
  return actions;
}

void Board::Apply(int action_id) {
  if (result_ != Result::kUndetermined) {
    return;  // Terminal state
  }

  Action a(action_id);

  if (a.IsPlacement()) {
    int x = a.id % kSize;
    int y = a.id / kSize;

    cells_[a.id] = stone_to_place_;
    move_count_++;

    if (phase_ == Phase::kStandard) {
      if (CheckWin(x, y, stone_to_place_)) {
        if (seat_a_stone_ == stone_to_place_)
          result_ = Result::kPlayerAWin;
        else
          result_ = Result::kPlayerBWin;
      } else if (move_count_ == kNumCells) {
        result_ = Result::kDraw;
      }
    }

    TransitionPhase();
  } else {
    // Control actions
    if (a.id == Action::kSwap2ChooseWhite) {
      seat_b_stone_ = Player::kWhite;
      seat_a_stone_ = Player::kBlack;
      phase_ = Phase::kStandard;
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kB;
    } else if (a.id == Action::kSwap2ChooseBlack) {
      seat_b_stone_ = Player::kBlack;
      seat_a_stone_ = Player::kWhite;
      phase_ = Phase::kStandard;
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kA;
    } else if (a.id == Action::kSwap2PlaceTwo) {
      phase_ = Phase::kSwap2PlaceTwo;
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kB;
    } else if (a.id == Action::kChooseWhite) {
      seat_a_stone_ = Player::kWhite;
      seat_b_stone_ = Player::kBlack;
      phase_ = Phase::kStandard;
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kA;
    } else if (a.id == Action::kChooseBlack) {
      seat_a_stone_ = Player::kBlack;
      seat_b_stone_ = Player::kWhite;
      phase_ = Phase::kStandard;
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kB;
    }
  }
}

void Board::TransitionPhase() {
  if (phase_ == Phase::kPlaceInitialThree) {
    if (move_count_ == 1) {
      stone_to_place_ = Player::kWhite;
    } else if (move_count_ == 2) {
      stone_to_place_ = Player::kBlack;
    } else if (move_count_ == 3) {
      phase_ = Phase::kSwap2Decision;
      stone_to_place_ = Player::kNone;
      current_player_ = Seat::kB;
    }
  } else if (phase_ == Phase::kSwap2PlaceTwo) {
    if (move_count_ == 4) {
      stone_to_place_ = Player::kBlack;
    } else if (move_count_ == 5) {
      phase_ = Phase::kChooseColor;
      stone_to_place_ = Player::kNone;
      current_player_ = Seat::kA;
    }
  } else if (phase_ == Phase::kStandard) {
    stone_to_place_ =
        (stone_to_place_ == Player::kBlack) ? Player::kWhite : Player::kBlack;
    current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
  }
}

bool Board::CheckWin(int x, int y, Player p) const {
  const int dx[] = {1, 0, 1, 1};
  const int dy[] = {0, 1, 1, -1};

  for (int d = 0; d < 4; ++d) {
    int count = 1;

    // Forward
    int fx = x + dx[d];
    int fy = y + dy[d];
    while (fx >= 0 && fx < kSize && fy >= 0 && fy < kSize &&
           cells_[fy * kSize + fx] == p) {
      count++;
      fx += dx[d];
      fy += dy[d];
    }

    // Backward
    int bx = x - dx[d];
    int by = y - dy[d];
    while (bx >= 0 && bx < kSize && by >= 0 && by < kSize &&
           cells_[by * kSize + bx] == p) {
      count++;
      bx -= dx[d];
      by -= dy[d];
    }

    if (count == 5) return true;  // Exactly five
  }
  return false;
}

float Board::GetValueForSeat(Seat seat) const {
  if (result_ == Result::kUndetermined) return 0.0f;
  if (result_ == Result::kDraw) return 0.0f;
  if (result_ == Result::kPlayerAWin) {
    return (seat == Seat::kA) ? 1.0f : -1.0f;
  }
  if (result_ == Result::kPlayerBWin) {
    return (seat == Seat::kB) ? 1.0f : -1.0f;
  }
  return 0.0f;
}

void Board::Retract(int action_id) {
  result_ = Result::kUndetermined;
  Action a(action_id);

  if (!a.IsPlacement()) {
    if (a.id == Action::kSwap2ChooseWhite || a.id == Action::kSwap2ChooseBlack || a.id == Action::kSwap2PlaceTwo) {
      seat_a_stone_ = Player::kNone;
      seat_b_stone_ = Player::kNone;
      phase_ = Phase::kSwap2Decision;
      stone_to_place_ = Player::kNone;
      current_player_ = Seat::kB;
    } else if (a.id == Action::kChooseWhite || a.id == Action::kChooseBlack) {
      seat_a_stone_ = Player::kNone;
      seat_b_stone_ = Player::kNone;
      phase_ = Phase::kChooseColor;
      stone_to_place_ = Player::kNone;
      current_player_ = Seat::kA;
    }
    return;
  }

  // Placement
  cells_[a.id] = Player::kNone;
  move_count_--;

  if (move_count_ == 0) {
    phase_ = Phase::kPlaceInitialThree;
    stone_to_place_ = Player::kBlack;
    current_player_ = Seat::kA;
  } else if (move_count_ == 1) {
    phase_ = Phase::kPlaceInitialThree;
    stone_to_place_ = Player::kWhite;
    current_player_ = Seat::kA;
  } else if (move_count_ == 2) {
    phase_ = Phase::kPlaceInitialThree;
    stone_to_place_ = Player::kBlack;
    current_player_ = Seat::kA;
  } else if (move_count_ == 3) {
    if (phase_ == Phase::kSwap2PlaceTwo) {
      stone_to_place_ = Player::kWhite;
      current_player_ = Seat::kB;
    } else {
      stone_to_place_ = Player::kWhite;
      current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
    }
  } else if (move_count_ == 4) {
    if (phase_ == Phase::kChooseColor) {
      phase_ = Phase::kSwap2PlaceTwo;
      stone_to_place_ = Player::kBlack;
      current_player_ = Seat::kB;
    } else {
      stone_to_place_ = (stone_to_place_ == Player::kBlack) ? Player::kWhite : Player::kBlack;
      current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
    }
  } else {
    stone_to_place_ = (stone_to_place_ == Player::kBlack) ? Player::kWhite : Player::kBlack;
    current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
  }
}

