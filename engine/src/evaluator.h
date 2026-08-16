#pragma once
#include <functional>
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

  // Evaluates an ordered list of boards. Implementations MUST return exactly one
  // EvaluationResult per input board in identical 1-to-1 index order
  // (results.size() == boards.size()).
  // An optional on_submit_fn callback can be provided to execute side work
  // concurrently while asynchronous or batched inference is in flight.
  virtual std::vector<EvaluationResult> Evaluate(
      const std::vector<Board>& boards,
      const std::function<void()>& on_submit_fn = nullptr) = 0;
};



