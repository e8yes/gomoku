#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "board.h"
#include "endgame_solver.h"
#include "mcts.h"

class Evaluator;

struct Config {
  int simulations = 128;
  int batch_size = 32;
  float c_puct = 1.0f;
  std::uint64_t seed = 0;
  int keep_last_moves = 3;
  std::optional<DirichletNoiseConfig> dirichlet_noise;
  // Zero means keep noise enabled for the whole game. A positive value
  // disables it after that many completed plies.
  int dirichlet_noise_plies = 0;
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

// Plays one game without retaining training positions. The selector is
// called at every ply, allowing evaluation matches to choose the champion or
// challenger based on the current seat. The evaluator pointers are borrowed.
GameResult PlayGame(const Config& config, const EvaluatorSelector& selector,
                    EndgameSolver endgame_solver = {});

// Runs one self-play game with a borrowed evaluator and endgame solver. The
// returned records are the final keep_last_moves non-terminal positions,
// labeled from each position's side-to-move perspective after the game ends.
// If evaluator is null, RandomEvaluator is used. If endgame_solver is empty,
// the production VCF solver is used.
std::vector<TrainingExample> GenerateGame(const Config& config,
                                          Evaluator* evaluator = nullptr,
                                          EndgameSolver endgame_solver = {});
