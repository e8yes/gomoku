#include "self_play.h"

#include <gtest/gtest.h>

#include <utility>

#include "evaluator.h"

namespace {

class CountingEvaluator final : public Evaluator {
 public:
  std::vector<EvaluationResult> Evaluate(
      const std::vector<Board>& boards) override {
    calls += static_cast<int>(boards.size());
    std::vector<EvaluationResult> results;
    results.reserve(boards.size());
    for (const Board& board : boards) {
      EvaluationResult result;
      result.move_pmf.assign(Board::kNumActions, 0.0f);
      const std::vector<int> legal_actions = board.GetLegalActions();
      if (!legal_actions.empty()) {
        const float probability =
            1.0f / static_cast<float>(legal_actions.size());
        for (int action : legal_actions) result.move_pmf[action] = probability;
      }
      result.value = 0.0f;
      results.push_back(std::move(result));
    }
    return results;
  }

  int calls = 0;
};

}  // namespace

TEST(SelfPlayTest, KeepsAndLabelsTheFinalThreePositions) {
  Config config;
  config.simulations = 1;
  config.batch_size = 1;
  config.seed = 1234;
  config.keep_last_moves = 3;

  const std::vector<TrainingExample> examples = GenerateGame(config);

  ASSERT_EQ(examples.size(), 3u);
  for (const auto& example : examples) {
    EXPECT_FALSE(example.board.IsTerminal());
    ASSERT_EQ(example.policy.size(),
              static_cast<std::size_t>(Board::kNumActions));
    EXPECT_GE(example.value, -1.0f);
    EXPECT_LE(example.value, 1.0f);
  }
}

TEST(SelfPlayTest, AcceptsBorrowedEvaluatorAndOptionalRootNoise) {
  Config config;
  config.simulations = 1;
  config.batch_size = 1;
  config.seed = 5678;
  config.keep_last_moves = 4;
  config.dirichlet_noise = DirichletNoiseConfig{0.3f, 0.25f, config.seed};

  CountingEvaluator evaluator;
  const std::vector<TrainingExample> examples =
      GenerateGame(config, &evaluator);

  EXPECT_EQ(examples.size(), 4u);
  EXPECT_GT(evaluator.calls, 0);
  for (const auto& example : examples) {
    EXPECT_FALSE(example.board.IsTerminal());
    EXPECT_EQ(example.policy.size(),
              static_cast<std::size_t>(Board::kNumActions));
  }
}

TEST(SelfPlayTest, EvaluatorSelectorSupportsTwoModelMatches) {
  Config config;
  config.simulations = 1;
  config.batch_size = 1;
  config.seed = 9012;
  config.keep_last_moves = 0;
  config.sample_actions = false;

  CountingEvaluator seat_a_evaluator;
  CountingEvaluator seat_b_evaluator;
  const EvaluatorSelector selector = [&](const Board& board) -> Evaluator* {
    return board.current_player() == Seat::kA ? &seat_a_evaluator
                                              : &seat_b_evaluator;
  };

  const GameResult result = PlayGame(config, selector);

  EXPECT_GT(result.action_count, 0);
  EXPECT_GT(seat_a_evaluator.calls, 0);
  EXPECT_GT(seat_b_evaluator.calls, 0);
  EXPECT_NE(result.result, Result::kUndetermined);
}
