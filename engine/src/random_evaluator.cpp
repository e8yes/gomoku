#include "random_evaluator.h"

std::vector<EvaluationResult> RandomEvaluator::Evaluate(const std::vector<Board>& boards) {
  std::vector<EvaluationResult> results;
  results.reserve(boards.size());

  for (const auto& board : boards) {
    EvaluationResult res;
    res.move_pmf.assign(Board::kNumActions, 0.0f);
    res.value = 0.0f;

    auto legal_actions = board.GetLegalActions();
    if (legal_actions.empty()) {
      results.push_back(res);
      continue;
    }

    float prob = 1.0f / legal_actions.size();
    for (int action : legal_actions) {
      res.move_pmf[action] = prob;
    }
    
    results.push_back(res);
  }

  return results;
}
