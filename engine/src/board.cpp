#include "board.h"

#include <random>
#include <sstream>
#include <stdexcept>

namespace {

struct ZobristKeys {
  int64_t cells[kNumBoardHashes][Board::kNumCells][3];

  int64_t phase[kNumBoardHashes][5];

  int64_t player[kNumBoardHashes][2];  // Seat::kA, Seat::kB

  int64_t stone_to_place[kNumBoardHashes][3];  // Player::kNone, kBlack, kWhite

  int64_t seat_a[kNumBoardHashes][3];
  int64_t seat_b[kNumBoardHashes][3];

  int64_t result[kNumBoardHashes]
                [4];  // Result::kUndetermined, kPlayerAWin, kPlayerBWin, kDraw

  ZobristKeys() {
    std::mt19937_64 rng(13377331ULL);

    for (int hash = 0; hash < kNumBoardHashes; ++hash) {
      for (int i = 0; i < Board::kNumCells; ++i) {
        for (int p = 0; p < 3; ++p) {
          cells[hash][i][p] = static_cast<int64_t>(rng());
        }
      }
      for (int p = 0; p < 5; ++p) {
        phase[hash][p] = static_cast<int64_t>(rng());
      }
      for (int pl = 0; pl < 2; ++pl) {
        player[hash][pl] = static_cast<int64_t>(rng());
      }
      for (int p = 0; p < 3; ++p) {
        stone_to_place[hash][p] = static_cast<int64_t>(rng());
        seat_a[hash][p] = static_cast<int64_t>(rng());
        seat_b[hash][p] = static_cast<int64_t>(rng());
      }
      for (int r = 0; r < 4; ++r) {
        result[hash][r] = static_cast<int64_t>(rng());
      }
    }
  }
};

const ZobristKeys& GetZobristKeys() {
  static const ZobristKeys keys;
  return keys;
}

void XorCell(BoardSignature& sig, int index, Player player) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.cells[hash][index][static_cast<int>(player)];
  }
}

void XorPhase(BoardSignature& sig, Phase phase) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.phase[hash][static_cast<int>(phase)];
  }
}

void XorCurrentPlayer(BoardSignature& sig, Seat seat) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.player[hash][static_cast<int>(seat)];
  }
}

void XorStoneToPlace(BoardSignature& sig, Player player) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.stone_to_place[hash][static_cast<int>(player)];
  }
}

void XorSeatAStone(BoardSignature& sig, Player player) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.seat_a[hash][static_cast<int>(player)];
  }
}

void XorSeatBStone(BoardSignature& sig, Player player) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.seat_b[hash][static_cast<int>(player)];
  }
}

void XorResult(BoardSignature& sig, Result result) {
  const auto& keys = GetZobristKeys();
  for (int hash = 0; hash < kNumBoardHashes; ++hash) {
    sig[hash] ^= keys.result[hash][static_cast<int>(result)];
  }
}

}  // namespace

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

  zobrists_.fill(0);

  for (int i = 0; i < kNumCells; ++i) {
    XorCell(zobrists_, i, cells_[i]);
  }
  ToggleHash(-1);
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

  ToggleHash(a.id);

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

  ToggleHash(a.id);
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
  Action a(action_id);

  ToggleHash(a.id);

  if (!a.IsPlacement()) {
    result_ = Result::kUndetermined;

    if (a.id == Action::kSwap2ChooseWhite ||
        a.id == Action::kSwap2ChooseBlack || a.id == Action::kSwap2PlaceTwo) {
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
  } else {
    // Placement retraction
    result_ = Result::kUndetermined;
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
        stone_to_place_ =
            (stone_to_place_ == Player::kBlack) ? Player::kWhite : Player::kBlack;
        current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
      }
    } else {
      stone_to_place_ =
          (stone_to_place_ == Player::kBlack) ? Player::kWhite : Player::kBlack;
      current_player_ = (current_player_ == Seat::kA) ? Seat::kB : Seat::kA;
    }
  }

  ToggleHash(a.id);
}

void Board::ToggleHash(int action_id) {
  Action a(action_id);
  if (a.IsPlacement()) {
    XorCell(zobrists_, a.id, cells_[a.id]);
  }
  XorPhase(zobrists_, phase_);
  XorCurrentPlayer(zobrists_, current_player_);
  XorStoneToPlace(zobrists_, stone_to_place_);
  XorSeatAStone(zobrists_, seat_a_stone_);
  XorSeatBStone(zobrists_, seat_b_stone_);
  XorResult(zobrists_, result_);
}
