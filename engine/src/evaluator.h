#pragma once
#include <vector>

#include "board.h"

struct EvaluationResult {
  std::vector<float> move_pmf;  // Probability mass function over actions, size
                                // Board::kNumActions
  float value;  // Value from the perspective of the current player [-1.0, 1.0]
};

class Evaluator {
 public:
  virtual ~Evaluator() = default;
  virtual std::vector<EvaluationResult> Evaluate(const std::vector<Board>& boards) = 0;
};
