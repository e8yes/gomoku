#pragma once
#include <functional>
#include <vector>

#include "evaluator.h"

// Uniform policy/value fallback used for bootstrapping. Endgame solving is
// deliberately not embedded here: MCTS receives an optional EndgameSolver and
// can override evaluator output with a proven tactical result, regardless of
// which Evaluator implementation supplied the prior.
class RandomEvaluator : public Evaluator {
 public:
  std::vector<EvaluationResult> Evaluate(
      const std::vector<Board>& boards,
      const std::function<void()>& on_submit_fn = nullptr) override;
};


