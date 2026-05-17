#pragma once
#include "evaluator.h"

class RandomEvaluator : public Evaluator {
 public:
  std::vector<EvaluationResult> Evaluate(const std::vector<Board>& boards) override;
};
