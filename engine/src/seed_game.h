#pragma once

#include <cstdint>
#include <vector>

#include "board.h"

struct Config {
  int simulations = 128;
  int batch_size = 32;
  float c_puct = 1.0f;
  std::uint64_t seed = 0;
  int keep_last_moves = 3;
};

struct TrainingExample {
  Board board;
  std::vector<float> policy;
  float value = 0.0f;
};

// Runs one seed game with a RandomEvaluator and the production VCF solver.
// The returned records are the final keep_last_moves non-terminal positions,
// labeled from each position's side-to-move perspective after the game ends.
std::vector<TrainingExample> GenerateGame(const Config& config);
