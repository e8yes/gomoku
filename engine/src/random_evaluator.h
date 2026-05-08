#pragma once
#include "evaluator.h"

class RandomEvaluator : public Evaluator {
 public:
  EvaluationResult Evaluate(const Board& board) override;
};
