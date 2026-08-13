#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "mcts.h"

namespace {

class UniformEvaluator final : public Evaluator {
 public:
  std::vector<EvaluationResult> Evaluate(
      const std::vector<Board>& boards) override {
    ++calls;

    std::vector<EvaluationResult> results;
    results.reserve(boards.size());
    for (const Board& board : boards) {
      EvaluationResult result;
      result.move_pmf.assign(Board::kNumActions, 0.0f);
      const std::vector<int> legal_actions = board.GetLegalActions();
      const float probability =
          legal_actions.empty()
              ? 0.0f
              : 1.0f / static_cast<float>(legal_actions.size());
      for (int action : legal_actions) {
        result.move_pmf[action] = probability;
      }
      result.value = 0.0f;
      results.push_back(std::move(result));
    }
    return results;
  }

  int calls = 0;
};

std::vector<float> RootPriors(const MCTS& mcts) {
  std::vector<float> priors(Board::kNumActions, -1.0f);
  if (mcts.root() == nullptr) return priors;
  for (const auto& child : mcts.root()->children()) {
    priors[child->action_id()] = child->prior_prob();
  }
  return priors;
}

}  // namespace

TEST(MCTSNoiseTest, NoiseIsOptIn) {
  Board board;
  UniformEvaluator evaluator;
  MCTS mcts(1, 1, 1.0f);

  mcts.Search(board, &evaluator);

  const float expected_prior = 1.0f / 225.0f;
  ASSERT_NE(mcts.root(), nullptr);
  ASSERT_EQ(mcts.root()->children().size(), 225u);
  for (const auto& child : mcts.root()->children()) {
    EXPECT_FLOAT_EQ(child->prior_prob(), expected_prior);
  }
}

TEST(MCTSNoiseTest, AddsNoiseOnlyToLegalRootActions) {
  Board board;
  UniformEvaluator evaluator;
  const DirichletNoiseConfig config{0.3f, 0.25f, 1234};
  MCTS mcts(1, 1, 1.0f, config);

  mcts.Search(board, &evaluator);

  ASSERT_NE(mcts.root(), nullptr);
  ASSERT_EQ(mcts.root()->children().size(), 225u);

  float prior_sum = 0.0f;
  for (const auto& child : mcts.root()->children()) {
    EXPECT_TRUE(child->action_id() >= 0 && child->action_id() < 225);
    EXPECT_GT(child->prior_prob(), 0.0f);
    prior_sum += child->prior_prob();
  }
  EXPECT_NEAR(prior_sum, 1.0f, 1e-5f);

  // Uniform evaluator priors would all be exactly 1/225. Noise must change
  // at least one legal root prior.
  bool changed = false;
  for (const auto& child : mcts.root()->children()) {
    if (std::abs(child->prior_prob() - 1.0f / 225.0f) > 1e-6f) {
      changed = true;
      break;
    }
  }
  EXPECT_TRUE(changed);
}

TEST(MCTSNoiseTest, SeedMakesRootNoiseReproducible) {
  Board board;
  const DirichletNoiseConfig config{0.3f, 0.25f, 9876};

  UniformEvaluator first_evaluator;
  MCTS first(1, 1, 1.0f, config);
  first.Search(board, &first_evaluator);

  UniformEvaluator second_evaluator;
  MCTS second(1, 1, 1.0f, config);
  second.Search(board, &second_evaluator);

  EXPECT_EQ(RootPriors(first), RootPriors(second));
}

TEST(MCTSNoiseTest, NoiseIsAppliedOnceWhenRootIsExpanded) {
  Board board;
  UniformEvaluator evaluator;
  const DirichletNoiseConfig config{0.3f, 0.25f, 4567};
  MCTS mcts(1, 1, 1.0f, config);

  mcts.Search(board, &evaluator);
  const std::vector<float> first_priors = RootPriors(mcts);
  mcts.Search(board, &evaluator);

  EXPECT_EQ(first_priors, RootPriors(mcts));
}

TEST(MCTSNoiseTest, NoiseDoesNotAffectLeafPriors) {
  Board board;
  UniformEvaluator evaluator;
  const DirichletNoiseConfig config{0.3f, 0.25f, 7654};
  MCTS mcts(1, 1, 1.0f, config);

  mcts.Search(board, &evaluator);

  const MCTSNode* expanded_child = nullptr;
  for (const auto& child : mcts.root()->children()) {
    if (child->is_expanded()) {
      expanded_child = child.get();
      break;
    }
  }
  ASSERT_NE(expanded_child, nullptr);
  ASSERT_EQ(expanded_child->children().size(), 224u);

  const float expected_prior = 1.0f / 224.0f;
  for (const auto& child : expanded_child->children()) {
    EXPECT_FLOAT_EQ(child->prior_prob(), expected_prior);
  }
}

TEST(MCTSNoiseTest, SolvedRootBypassesNoise) {
  Board board;
  UniformEvaluator evaluator;
  const DirichletNoiseConfig config{0.3f, 0.25f, 2468};
  MCTS mcts(1, 1, 1.0f, config);
  EndgameSolver solver = [](const Board& candidate) {
    const std::vector<int> legal_actions = candidate.GetLegalActions();
    if (legal_actions.empty()) return std::vector<int>{};
    return std::vector<int>{legal_actions.front()};
  };

  const std::vector<float> policy = mcts.Search(board, &evaluator, solver);

  ASSERT_EQ(policy.size(), static_cast<std::size_t>(Board::kNumActions));
  EXPECT_FLOAT_EQ(policy[0], 1.0f);
  EXPECT_EQ(GetBestAction(policy), 0);
  EXPECT_EQ(evaluator.calls, 0);
  EXPECT_EQ(mcts.root(), nullptr);
}
