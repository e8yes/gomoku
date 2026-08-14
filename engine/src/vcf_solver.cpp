#include "vcf_solver.h"

#include <algorithm>
#include <cstddef>
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

using BoardKey = std::array<std::uint8_t, kVcfNumCells>;

struct BoardKeyHash {
  std::size_t operator()(const BoardKey& key) const {
    // Hash the complete board key. Equality still compares every cell, so a
    // hash collision cannot change the solver's answer.
    std::size_t hash = 1469598103934665603ULL;
    for (std::uint8_t cell : key) {
      hash ^= static_cast<std::size_t>(cell);
      hash *= 1099511628211ULL;
    }
    return hash;
  }
};

struct SearchContext {
  Stone attacker;
  Stone defender;
  int max_nodes = kDefaultMaxVcfNodes;
  int visited_nodes = 0;
  std::unordered_set<BoardKey, BoardKeyHash> failed_positions;
};

BoardKey MakeKey(const Position& position) {
  BoardKey key{};
  for (int action = 0; action < kVcfNumCells; ++action) {
    key[action] = static_cast<std::uint8_t>(position.At(action));
  }
  return key;
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

bool Search(const Position& position, SearchContext* context,
            std::vector<int>* line) {
  if (++context->visited_nodes > context->max_nodes) {
    return false;
  }

  // A VCF is a forcing continuation from a live position. A position that
  // already contains a five has already ended and is not a new winning line.
  if (HasExactFive(position, context->attacker) ||
      HasExactFive(position, context->defender)) {
    return false;
  }

  const BoardKey key = MakeKey(position);
  if (context->failed_positions.contains(key)) return false;

  // Prefer an immediate win. This also makes the result stable when several
  // winning moves exist: actions are examined in row-major action order.
  const std::vector<int> immediate_wins =
      WinningMoves(position, context->attacker);
  if (!immediate_wins.empty()) {
    line->assign(1, immediate_wins.front());
    return true;
  }

  // Every legal move is considered.
  for (int attack_action = 0; attack_action < kVcfNumCells; ++attack_action) {
    if (!position.IsEmpty(attack_action)) continue;

    Position after_attack = position;
    after_attack.Set(attack_action, context->attacker);

    // Check newly formed winning moves along lines passing through attack_action.
    const std::vector<int> attacker_wins =
        FindWinningMovesAround(after_attack, attack_action, context->attacker);
    if (attacker_wins.empty()) continue;

    // If the defender can win immediately, the attacker cannot force this
    // branch. Checking this before the threat count is important: a defender
    // is allowed to ignore a threat when doing so wins the game.
    if (!WinningMoves(after_attack, context->defender).empty()) continue;

    // Two or more winning squares cannot be covered by one defender move.
    if (attacker_wins.size() >= 2) {
      line->assign(1, attack_action);
      return true;
    }

    // With one winning square, the defender has exactly one relevant reply.
    // Apply it and continue searching for the next forcing attacker move.
    Position after_defense = after_attack;
    const int defense_action = attacker_wins.front();
    after_defense.Set(defense_action, context->defender);

    std::vector<int> continuation;
    if (!Search(after_defense, context, &continuation)) continue;

    line->clear();
    line->reserve(2 + continuation.size());
    line->push_back(attack_action);
    line->push_back(defense_action);
    line->insert(line->end(), continuation.begin(), continuation.end());
    return true;
  }

  context->failed_positions.insert(key);
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

  SearchContext context{
      position.current_player, OtherStone(position.current_player), max_nodes, 0, {}};
  std::vector<int> line;
  if (Search(position, &context, &line)) return line;
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
  auto add_candidate = [&](int act) {
    if (position.IsEmpty(act) &&
        std::find(candidate_actions.begin(), candidate_actions.end(), act) ==
            candidate_actions.end()) {
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
    if (!position.IsEmpty(action)) continue;
    if (std::find(candidate_actions.begin(), candidate_actions.end(), action) !=
        candidate_actions.end()) {
      continue;
    }

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
