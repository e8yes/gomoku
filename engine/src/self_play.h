#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "board.h"
#include "endgame_solver.h"
#include "mcts.h"

class Evaluator;

struct SelfPlayConfig {
  int simulations = 128;
  int batch_size = 32;
  float c_puct = 1.0f;
  std::uint64_t seed = 0;
  // A positive value keeps only the final positions; zero keeps every
  // non-terminal position for training data generation.
  int keep_last_moves = 3;
  std::optional<DirichletNoiseConfig> dirichlet_noise;
  // Zero means keep noise enabled for the whole game. A positive value
  // disables it after that many completed plies.
  int dirichlet_noise_plies = 0;
  // Zero means sample actions for the whole game when sample_actions is true.
  // A positive value samples only the first N decision plies, then selects
  // the highest-visit legal action deterministically.
  int stochastic_action_plies = 0;
  bool sample_actions = true;
};

struct TrainingExample {
  Board board;
  std::vector<float> policy;
  float value = 0.0f;
};

struct GameResult {
  Result result = Result::kUndetermined;
  int action_count = 0;
};

using EvaluatorSelector = std::function<Evaluator*(const Board&)>;
using TrainingPositionFilter = std::function<bool(const Board&)>;

// Returns whether action selection should remain stochastic at a zero-based
// decision ply. This shares the exploration-window semantics used by the
// production self-play and evaluation loops.
bool ShouldSampleAction(const SelfPlayConfig& config, int decision_ply);

// Plays one game without retaining training positions. The selector is
// called at every ply, allowing evaluation matches to choose the champion or
// challenger based on the current seat. The evaluator pointers are borrowed.
GameResult PlayGame(const SelfPlayConfig& config,
                    const EvaluatorSelector& selector,
                    EndgameSolver endgame_solver = {},
                    EndgameDefenseSolver defensive_solver = {});

// Runs a game whose players may use different evaluators and retains only
// positions accepted by training_position_filter. Each distinct evaluator
// owns an independent MCTS tree and evaluation cache.
std::vector<TrainingExample> GenerateGame(
    const SelfPlayConfig& config, const EvaluatorSelector& selector,
    const TrainingPositionFilter& training_position_filter = {},
    EndgameSolver endgame_solver = {},
    EndgameDefenseSolver defensive_solver = {});

// Runs one self-play game with a borrowed evaluator and endgame solver. The
// returned records are the final keep_last_moves non-terminal positions, or
// every non-terminal position when keep_last_moves is zero. They are labeled
// from each position's side-to-move perspective after the game ends.
// If evaluator is null, RandomEvaluator is used. An empty endgame_solver
// disables endgame solving. The defensive solver is optional.
std::vector<TrainingExample> GenerateGame(
    const SelfPlayConfig& config, Evaluator* evaluator = nullptr,
    EndgameSolver endgame_solver = {},
    EndgameDefenseSolver defensive_solver = {});
