#include "vcf_solver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "board.h"

namespace {

constexpr int kDirections[4][2] = {
    {1, 0},
    {0, 1},
    {1, 1},
    {1, -1},
};

bool IsInside(int x, int y) {
  return x >= 0 && x < kVcfBoardSize && y >= 0 && y < kVcfBoardSize;
}

int CountInDirection(const Position& position, int x, int y, int dx, int dy,
                     Stone stone) {
  int count = 0;
  x += dx;
  y += dy;
  while (IsInside(x, y) && position.At(x, y) == stone) {
    ++count;
    x += dx;
    y += dy;
  }
  return count;
}

std::uint64_t SplitMix64(std::uint64_t* state) {
  *state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = *state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// One 64-bit key per (cell, stone color). Positions are identified by the
// XOR of the keys of their occupied cells, updated incrementally on
// make/unmake. A collision could in principle prune a solvable branch, but
// at <= max_nodes distinct positions per search the probability is
// negligible, and the solver is already allowed to miss lines (node budget).
const std::array<std::array<std::uint64_t, 2>, kVcfNumCells>& ZobristKeys() {
  static const auto keys = [] {
    std::array<std::array<std::uint64_t, 2>, kVcfNumCells> table{};
    std::uint64_t state = 0x6D9F0A54C428F0B3ULL;
    for (auto& cell : table) {
      cell[0] = SplitMix64(&state);
      cell[1] = SplitMix64(&state);
    }
    return table;
  }();
  return keys;
}

int ZobristStoneIndex(Stone stone) { return stone == Stone::kBlack ? 0 : 1; }

std::uint64_t HashPosition(const Position& position) {
  std::uint64_t hash = 0;
  for (int action = 0; action < kVcfNumCells; ++action) {
    const Stone stone = position.At(action);
    if (stone != Stone::kEmpty) {
      hash ^= ZobristKeys()[action][ZobristStoneIndex(stone)];
    }
  }
  return hash;
}

struct SearchContext {
  Stone attacker;
  Stone defender;
  int max_nodes = kDefaultMaxVcfNodes;
  int visited_nodes = 0;
  std::uint64_t hash = 0;
  std::unordered_set<std::uint64_t> failed_positions;
};

void MakeMove(Position* position, SearchContext* context, int action,
              Stone stone) {
  position->Set(action, stone);
  context->hash ^= ZobristKeys()[action][ZobristStoneIndex(stone)];
}

void UnmakeMove(Position* position, SearchContext* context, int action,
                Stone stone) {
  position->Set(action, Stone::kEmpty);
  context->hash ^= ZobristKeys()[action][ZobristStoneIndex(stone)];
}

bool HasExactFive(const Position& position, Stone stone) {
  for (int action = 0; action < kVcfNumCells; ++action) {
    if (position.At(action) != stone) continue;

    const int x = VcfActionX(action);
    const int y = VcfActionY(action);
    for (const auto& direction : kDirections) {
      const int dx = direction[0];
      const int dy = direction[1];

      // Only inspect the beginning of a run. This prevents a run of six or
      // more from being misclassified as an exact five.
      if (IsInside(x - dx, y - dy) && position.At(x - dx, y - dy) == stone) {
        continue;
      }

      int run_length = 1;
      int next_x = x + dx;
      int next_y = y + dy;
      while (IsInside(next_x, next_y) && position.At(next_x, next_y) == stone) {
        ++run_length;
        next_x += dx;
        next_y += dy;
      }
      if (run_length == 5) return true;
    }
  }
  return false;
}

bool MakesExactFive(const Position& position, int action, Stone stone) {
  if (!position.IsEmpty(action)) return false;

  const int x = VcfActionX(action);
  const int y = VcfActionY(action);
  for (const auto& direction : kDirections) {
    const int count =
        1 +
        CountInDirection(position, x, y, direction[0], direction[1], stone) +
        CountInDirection(position, x, y, -direction[0], -direction[1], stone);
    if (count == 5) return true;
  }
  return false;
}

std::vector<int> WinningMoves(const Position& position, Stone stone) {
  std::vector<int> winning_moves;
  for (int action = 0; action < kVcfNumCells; ++action) {
    if (MakesExactFive(position, action, stone)) {
      winning_moves.push_back(action);
    }
  }
  return winning_moves;
}

// Finds winning squares for `stone` that pass through `placed_action`.
// When `placed_action` is played, any newly created four-threat must be
// collinear with `placed_action` along one of the 4 directions within distance 4.
std::vector<int> FindWinningMovesAround(const Position& position,
                                       int placed_action, Stone stone) {
  std::vector<int> winning_moves;
  const int px = VcfActionX(placed_action);
  const int py = VcfActionY(placed_action);

  for (const auto& direction : kDirections) {
    const int dx = direction[0];
    const int dy = direction[1];

    for (int k = -4; k <= 4; ++k) {
      if (k == 0) continue;
      const int x = px + k * dx;
      const int y = py + k * dy;
      if (!IsInside(x, y)) continue;

      const int action = VcfActionFromXY(x, y);
      if (position.IsEmpty(action) && MakesExactFive(position, action, stone)) {
        if (std::find(winning_moves.begin(), winning_moves.end(), action) ==
            winning_moves.end()) {
          winning_moves.push_back(action);
        }
      }
    }
  }
  return winning_moves;
}

// A move can only complete or create a five if a friendly stone lies within
// offset 4 along one of the 4 lines through it — exactly the region
// FindWinningMovesAround inspects — so this prune is lossless, including for
// gapped formations such as XXX_X.
bool HasStoneWithinLineDistance4(const Position& position, int x, int y,
                                 Stone stone) {
  for (const auto& direction : kDirections) {
    const int dx = direction[0];
    const int dy = direction[1];
    for (int k = -4; k <= 4; ++k) {
      if (k == 0) continue;
      if (position.At(x + k * dx, y + k * dy) == stone) return true;
    }
  }
  return false;
}

// Searches in place: `position` is mutated via make/unmake and is restored
// to its entry state on every return path.
bool Search(Position* position, SearchContext* context,
            std::vector<int>* line) {
  if (++context->visited_nodes > context->max_nodes) {
    return false;
  }

  // A VCF is a forcing continuation from a live position. A position that
  // already contains a five has already ended and is not a new winning line.
  if (HasExactFive(*position, context->attacker) ||
      HasExactFive(*position, context->defender)) {
    return false;
  }

  if (context->failed_positions.contains(context->hash)) return false;

  // Prefer an immediate win. This also makes the result stable when several
  // winning moves exist: actions are examined in row-major action order.
  const std::vector<int> immediate_wins =
      WinningMoves(*position, context->attacker);
  if (!immediate_wins.empty()) {
    line->assign(1, immediate_wins.front());
    return true;
  }

  // An attacker placement can never create a defender winning square and can
  // only remove one by occupying it, so the defender's winning squares are
  // computed once per node instead of once per candidate.
  const std::vector<int> defender_wins =
      WinningMoves(*position, context->defender);

  // Every legal move with a friendly stone within line distance 4 is
  // considered; other moves cannot create a four and the prune is lossless.
  for (int attack_action = 0; attack_action < kVcfNumCells; ++attack_action) {
    if (!position->IsEmpty(attack_action)) continue;

    // If the defender can still win immediately after the attack, the
    // attacker cannot force this branch: a defender is allowed to ignore a
    // threat when doing so wins the game.
    if (defender_wins.size() >= 2 ||
        (defender_wins.size() == 1 && defender_wins.front() != attack_action)) {
      continue;
    }

    const int ax = VcfActionX(attack_action);
    const int ay = VcfActionY(attack_action);
    if (!HasStoneWithinLineDistance4(*position, ax, ay, context->attacker)) {
      continue;
    }

    MakeMove(position, context, attack_action, context->attacker);

    // Check newly formed winning moves along lines passing through attack_action.
    const std::vector<int> attacker_wins =
        FindWinningMovesAround(*position, attack_action, context->attacker);
    if (attacker_wins.empty()) {
      UnmakeMove(position, context, attack_action, context->attacker);
      continue;
    }

    // Two or more winning squares cannot be covered by one defender move.
    if (attacker_wins.size() >= 2) {
      UnmakeMove(position, context, attack_action, context->attacker);
      line->assign(1, attack_action);
      return true;
    }

    // With one winning square, the defender has exactly one relevant reply.
    // Apply it and continue searching for the next forcing attacker move.
    const int defense_action = attacker_wins.front();
    MakeMove(position, context, defense_action, context->defender);

    std::vector<int> continuation;
    const bool solved = Search(position, context, &continuation);

    UnmakeMove(position, context, defense_action, context->defender);
    UnmakeMove(position, context, attack_action, context->attacker);
    if (!solved) continue;

    line->clear();
    line->reserve(2 + continuation.size());
    line->push_back(attack_action);
    line->push_back(defense_action);
    line->insert(line->end(), continuation.begin(), continuation.end());
    return true;
  }

  context->failed_positions.insert(context->hash);
  return false;
}

Stone ToVcfStone(Player player) {
  if (player == Player::kBlack) return Stone::kBlack;
  if (player == Player::kWhite) return Stone::kWhite;
  return Stone::kEmpty;
}

Position MakeVcfPosition(const ::Board& board) {
  Position position(ToVcfStone(board.stone_to_place()));
  for (int y = 0; y < kVcfBoardSize; ++y) {
    for (int x = 0; x < kVcfBoardSize; ++x) {
      position.Set(x, y, ToVcfStone(board.cell(x, y)));
    }
  }
  return position;
}

}  // namespace

bool Position::Set(int x, int y, Stone stone) {
  if (!IsInside(x, y)) return false;
  cells[VcfActionFromXY(x, y)] = stone;
  return true;
}

bool Position::Set(int action, Stone stone) {
  if (action < 0 || action >= kVcfNumCells) return false;
  cells[action] = stone;
  return true;
}

Stone Position::At(int x, int y) const {
  if (!IsInside(x, y)) return Stone::kEmpty;
  return cells[VcfActionFromXY(x, y)];
}

Stone Position::At(int action) const {
  if (action < 0 || action >= kVcfNumCells) return Stone::kEmpty;
  return cells[action];
}

bool Position::IsEmpty(int action) const {
  return action >= 0 && action < kVcfNumCells && cells[action] == Stone::kEmpty;
}

std::vector<int> SolveVCF(const Position& position, int max_nodes) {
  if (position.current_player != Stone::kBlack &&
      position.current_player != Stone::kWhite) {
    return {};
  }

  SearchContext context{position.current_player,
                        OtherStone(position.current_player),
                        max_nodes,
                        0,
                        HashPosition(position),
                        {}};
  Position scratch = position;
  std::vector<int> line;
  if (Search(&scratch, &context, &line)) return line;
  return {};
}

std::vector<int> SolveVCF(const ::Board& board, int max_nodes) {
  if (board.phase() != Phase::kStandard || board.IsTerminal()) return {};

  return SolveVCF(MakeVcfPosition(board), max_nodes);
}

EndgameDefenseAnalysis AnalyzeVCFDefense(const Position& position, int max_nodes) {
  if (position.current_player != Stone::kBlack &&
      position.current_player != Stone::kWhite) {
    return {};
  }

  EndgameDefenseAnalysis analysis;
  const Stone defender = position.current_player;
  const Stone attacker = OtherStone(defender);

  // Adding a defender stone can only remove attacker's legal placements; it
  // cannot create a new forcing line for the attacker. Therefore an empty
  // opponent-to-move VCF result proves that no candidate defensive move can
  // expose an opponent VCF, and avoids probing every legal action in ordinary
  // positions.
  Position attacker_to_move = position;
  attacker_to_move.current_player = attacker;
  const std::vector<int> threat_line = SolveVCF(attacker_to_move, max_nodes);
  if (threat_line.empty()) return analysis;

  analysis.threat_detected = true;

  // Targeted defense: only test actions that intersect the discovered threat line,
  // its winning squares, its collinear line segments, immediate wins, or counter-fours.
  std::vector<int> candidate_actions;
  std::array<bool, kVcfNumCells> is_candidate{};
  auto add_candidate = [&](int act) {
    if (position.IsEmpty(act) && !is_candidate[act]) {
      is_candidate[act] = true;
      candidate_actions.push_back(act);
    }
  };

  Position replay = position;
  for (size_t i = 0; i < threat_line.size(); ++i) {
    const int act = threat_line[i];
    add_candidate(act);

    const bool is_attacker_ply = (i % 2 == 0);
    const Stone stone = is_attacker_ply ? attacker : defender;
    replay.Set(act, stone);

    if (is_attacker_ply) {
      const auto wins = FindWinningMovesAround(replay, act, attacker);
      for (int w : wins) add_candidate(w);

      const int ax = VcfActionX(act);
      const int ay = VcfActionY(act);
      for (const auto& direction : kDirections) {
        const int dx = direction[0];
        const int dy = direction[1];
        for (int k = -4; k <= 4; ++k) {
          if (k == 0) continue;
          const int x = ax + k * dx;
          const int y = ay + k * dy;
          if (IsInside(x, y)) {
            add_candidate(VcfActionFromXY(x, y));
          }
        }
      }
    }
  }

  const std::vector<int> defender_wins = WinningMoves(position, defender);
  for (int act : defender_wins) add_candidate(act);

  for (int action = 0; action < kVcfNumCells; ++action) {
    if (!position.IsEmpty(action) || is_candidate[action]) continue;

    const int ax = VcfActionX(action);
    const int ay = VcfActionY(action);
    if (!HasStoneWithinLineDistance4(position, ax, ay, defender)) continue;

    Position after_counter = position;
    after_counter.Set(action, defender);
    if (!FindWinningMovesAround(after_counter, action, defender).empty()) {
      add_candidate(action);
    }
  }

  for (int action : candidate_actions) {
    Position after_defense = position;
    after_defense.Set(action, defender);
    after_defense.current_player = attacker;

    // A non-empty line means the candidate move allows the opponent to force
    // a VCF. A terminal win for the defender naturally returns an empty line
    // from SolveVCF and is therefore safe.
    if (SolveVCF(after_defense, max_nodes).empty()) {
      analysis.safe_actions.push_back(action);
    }
  }

  std::sort(analysis.safe_actions.begin(), analysis.safe_actions.end());
  return analysis;
}

EndgameDefenseAnalysis AnalyzeVCFDefense(const ::Board& board, int max_nodes) {
  if (board.phase() != Phase::kStandard || board.IsTerminal()) return {};

  const Position position = MakeVcfPosition(board);
  if (position.current_player == Stone::kEmpty) return {};
  return AnalyzeVCFDefense(position, max_nodes);
}
