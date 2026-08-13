#include "seed_game.h"

#include <algorithm>
#include <random>
#include <stdexcept>

#include "board.h"
#include "mcts.h"
#include "random_evaluator.h"
#include "vcf_solver.h"

namespace {

int SampleAction(const Board& board, const std::vector<float>& policy,
                 std::mt19937_64* random_engine) {
  const std::vector<int> legal_actions = board.GetLegalActions();
  if (legal_actions.empty()) return -1;
  if (policy.size() != Board::kNumActions) {
    throw std::runtime_error("MCTS returned a policy with the wrong size");
  }

  std::vector<double> weights;
  weights.reserve(legal_actions.size());
  double total = 0.0;
  for (int action : legal_actions) {
    const double weight = std::max(0.0f, policy[action]);
    weights.push_back(weight);
    total += weight;
  }

  if (total <= 0.0) {
    std::uniform_int_distribution<std::size_t> distribution(
        0, legal_actions.size() - 1);
    return legal_actions[distribution(*random_engine)];
  }

  std::discrete_distribution<std::size_t> distribution(weights.begin(),
                                                       weights.end());
  return legal_actions[distribution(*random_engine)];
}

std::uint64_t ResolveSeed(std::uint64_t seed) {
  if (seed != 0) return seed;
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^ random_device();
}

}  // namespace

std::vector<TrainingExample> GenerateGame(const Config& config) {
  if (config.simulations <= 0 || config.batch_size <= 0 ||
      config.keep_last_moves < 0) {
    throw std::invalid_argument("Invalid seed-game search configuration");
  }

  RandomEvaluator evaluator;
  MCTS mcts(config.simulations, config.batch_size, config.c_puct);
  EndgameSolver solver = [](const Board& board) { return SolveVCF(board); };
  std::mt19937_64 random_engine(ResolveSeed(config.seed));

  struct PositionRecord {
    Board board;
    std::vector<float> policy;
  };
  std::vector<PositionRecord> history;
  Board board;

  // There are 225 placements and at most five Swap2 control actions. The
  // guard also protects the executable from an accidental non-progressing
  // game if the board implementation changes.
  constexpr int kMaximumActions = Board::kNumCells + 5;
  for (int action_count = 0;
       !board.IsTerminal() && action_count < kMaximumActions; ++action_count) {
    const std::vector<float> policy = mcts.Search(board, &evaluator, solver);
    const int action = SampleAction(board, policy, &random_engine);
    if (action < 0) break;

    history.push_back(PositionRecord{board, policy});
    board.Apply(action);
    mcts.SelectAction(action);
  }

  if (!board.IsTerminal() && !board.GetLegalActions().empty()) {
    throw std::runtime_error("Seed game exceeded its maximum action count");
  }

  const std::size_t keep = std::min<std::size_t>(
      static_cast<std::size_t>(config.keep_last_moves), history.size());
  const std::size_t first = history.size() - keep;

  std::vector<TrainingExample> examples;
  examples.reserve(keep);
  for (std::size_t i = first; i < history.size(); ++i) {
    TrainingExample example;
    example.board = history[i].board;
    example.policy = std::move(history[i].policy);
    example.value = board.GetValueForSeat(example.board.current_player());
    examples.push_back(std::move(example));
  }
  return examples;
}
