#include "self_play.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

#include "random_evaluator.h"
#include "vcf_solver.h"

namespace {

struct PositionRecord {
  Board board;
  std::vector<float> policy;
};

struct PlayedGame {
  Board final_board;
  std::vector<PositionRecord> history;
  int action_count = 0;
};

void ValidateConfig(const Config& config) {
  if (config.simulations <= 0 || config.batch_size <= 0 ||
      config.keep_last_moves < 0 || config.dirichlet_noise_plies < 0) {
    throw std::invalid_argument("Invalid self-play search configuration");
  }
}

int BestLegalAction(const std::vector<int>& legal_actions,
                    const std::vector<float>& policy) {
  int best_action = -1;
  float best_probability = -1.0f;
  for (int action : legal_actions) {
    if (policy[action] > best_probability) {
      best_probability = policy[action];
      best_action = action;
    }
  }
  return best_action;
}

int SampleAction(const Board& board, const std::vector<float>& policy,
                 bool sample_actions, std::mt19937_64* random_engine) {
  const std::vector<int> legal_actions = board.GetLegalActions();
  if (legal_actions.empty()) return -1;
  if (policy.size() != Board::kNumActions) {
    throw std::runtime_error("MCTS returned a policy with the wrong size");
  }
  if (!sample_actions) return BestLegalAction(legal_actions, policy);

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

  std::uniform_real_distribution<double> distribution(0.0, total);
  const double sample = distribution(*random_engine);
  double cumulative = 0.0;
  for (std::size_t i = 0; i < legal_actions.size(); ++i) {
    cumulative += weights[i];
    if (sample <= cumulative) return legal_actions[i];
  }
  return legal_actions.back();
}

std::uint64_t ResolveSeed(std::uint64_t seed) {
  if (seed != 0) return seed;
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^
         static_cast<std::uint64_t>(random_device());
}

PlayedGame RunGame(const Config& config, const EvaluatorSelector& selector,
                   EndgameSolver endgame_solver) {
  ValidateConfig(config);
  if (!selector) throw std::invalid_argument("Evaluator selector is empty");

  EndgameSolver solver = std::move(endgame_solver);
  if (!solver) {
    solver = [](const Board& board) { return SolveVCF(board); };
  }

  MCTS mcts(config.simulations, config.batch_size, config.c_puct,
            config.dirichlet_noise);
  std::mt19937_64 random_engine(ResolveSeed(config.seed));

  PlayedGame played_game;
  Board board;

  // There are 225 placements and at most five Swap2 control actions. The
  // guard also protects the executable from an accidental non-progressing
  // game if the board implementation changes.
  constexpr int kMaximumActions = Board::kNumCells + 5;
  for (int action_count = 0;
       !board.IsTerminal() && action_count < kMaximumActions; ++action_count) {
    Evaluator* evaluator = selector(board);
    if (evaluator == nullptr) {
      throw std::runtime_error("Evaluator selector returned null");
    }

    const std::vector<float> policy = mcts.Search(board, evaluator, solver);
    const int action =
        SampleAction(board, policy, config.sample_actions, &random_engine);
    if (action < 0) break;

    played_game.history.push_back(PositionRecord{board, policy});
    board.Apply(action);
    mcts.SelectAction(action);
    played_game.action_count = action_count + 1;

    if (config.dirichlet_noise && config.dirichlet_noise_plies > 0 &&
        played_game.action_count >= config.dirichlet_noise_plies) {
      mcts.SetDirichletNoise(std::nullopt);
    }
  }

  if (!board.IsTerminal() && !board.GetLegalActions().empty()) {
    throw std::runtime_error(
        "Self-play game exceeded its maximum action count");
  }

  played_game.final_board = board;
  return played_game;
}

}  // namespace

GameResult PlayGame(const Config& config, const EvaluatorSelector& selector,
                    EndgameSolver endgame_solver) {
  const PlayedGame played_game =
      RunGame(config, selector, std::move(endgame_solver));
  return GameResult{played_game.final_board.result(), played_game.action_count};
}

std::vector<TrainingExample> GenerateGame(const Config& config,
                                          Evaluator* evaluator,
                                          EndgameSolver endgame_solver) {
  RandomEvaluator fallback_evaluator;
  Evaluator* active_evaluator =
      evaluator != nullptr ? evaluator : &fallback_evaluator;
  const EvaluatorSelector selector = [active_evaluator](const Board&) {
    return active_evaluator;
  };
  const PlayedGame played_game =
      RunGame(config, selector, std::move(endgame_solver));

  const std::size_t keep =
      std::min<std::size_t>(static_cast<std::size_t>(config.keep_last_moves),
                            played_game.history.size());
  const std::size_t first = played_game.history.size() - keep;

  std::vector<TrainingExample> examples;
  examples.reserve(keep);
  for (std::size_t i = first; i < played_game.history.size(); ++i) {
    TrainingExample example;
    example.board = played_game.history[i].board;
    example.policy = std::move(played_game.history[i].policy);
    example.value =
        played_game.final_board.GetValueForSeat(example.board.current_player());
    examples.push_back(std::move(example));
  }
  return examples;
}
