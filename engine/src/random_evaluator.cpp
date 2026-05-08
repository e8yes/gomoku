#include "random_evaluator.h"

EvaluationResult RandomEvaluator::Evaluate(const Board& board) {
  EvaluationResult res;
  res.move_pmf.assign(Board::kNumActions, 0.0f);
  res.value = 0.0f;

  auto legal_actions = board.GetLegalActions();
  if (legal_actions.empty()) {
    return res;
  }

  float prob = 1.0f / legal_actions.size();
  for (int action : legal_actions) {
    res.move_pmf[action] = prob;
  }

  return res;
}
