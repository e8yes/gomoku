#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

#include "mcts.h"
#include "random_evaluator.h"
#include "vcf_solver.h"

namespace {

class CountingEvaluator final : public Evaluator {
 public:
  std::vector<EvaluationResult> Evaluate(
      const std::vector<Board>& boards) override {
    ++calls;
    evaluated_boards += boards.size();

    std::vector<EvaluationResult> results;
    results.reserve(boards.size());
    for (const Board& board : boards) {
      EvaluationResult result;
      result.move_pmf.assign(Board::kNumActions, 0.0f);
      const auto legal_actions = board.GetLegalActions();
      if (!legal_actions.empty()) {
        const float probability =
            1.0f / static_cast<float>(legal_actions.size());
        for (int action : legal_actions) {
          result.move_pmf[action] = probability;
        }
      }
      result.value = 0.0f;
      results.push_back(std::move(result));
    }
    return results;
  }

  int calls = 0;
  std::size_t evaluated_boards = 0;
};

Board MakeStandardBlackToMove() {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);  // Initial black stone.
  board.Apply(Action::FromXY(1, 0).id);  // Initial white stone.
  board.Apply(Action::FromXY(2, 0).id);  // Initial black stone.
  board.Apply(Action::kSwap2ChooseBlack);

  // B is black after the swap. Give A harmless white moves while B builds a
  // four at (0,1)..(3,1), then make one final white move so B is to move.
  board.Apply(Action::FromXY(10, 10).id);  // A white.
  board.Apply(Action::FromXY(0, 1).id);    // B black.
  board.Apply(Action::FromXY(12, 10).id);  // A white.
  board.Apply(Action::FromXY(1, 1).id);    // B black.
  board.Apply(Action::FromXY(14, 10).id);  // A white.
  board.Apply(Action::FromXY(2, 1).id);    // B black.
  board.Apply(Action::FromXY(10, 12).id);  // A white.
  board.Apply(Action::FromXY(3, 1).id);    // B black.
  board.Apply(Action::FromXY(12, 12).id);  // A white.

  return board;
}

}  // namespace

TEST(MCTSEndgameSolverTest, AcceptsFreeFunctionBoardAdapter) {
  Board board = MakeStandardBlackToMove();
  ASSERT_EQ(board.phase(), Phase::kStandard);
  ASSERT_EQ(board.stone_to_place(), Player::kBlack);

  CountingEvaluator evaluator;
  EndgameSolver solver = [](const Board& candidate) {
    return SolveVCF(candidate);
  };

  MCTS mcts(8, 4, 1.0f);
  const std::vector<float> policy = mcts.Search(board, &evaluator, solver);

  const int winning_action = Action::FromXY(4, 1).id;
  ASSERT_EQ(policy.size(), static_cast<std::size_t>(Board::kNumActions));
  EXPECT_FLOAT_EQ(policy[winning_action], 1.0f);
  EXPECT_EQ(GetBestAction(policy), winning_action);
  EXPECT_EQ(evaluator.calls, 0);
}

TEST(MCTSEndgameSolverTest, UnsolvedCallbackFallsBackToEvaluator) {
  Board board;
  CountingEvaluator evaluator;
  EndgameSolver solver = [](const Board&) { return std::vector<int>{}; };

  MCTS mcts(1, 1, 1.0f);
  mcts.Search(board, &evaluator, solver);

  EXPECT_GT(evaluator.calls, 0);
  EXPECT_GT(evaluator.evaluated_boards, 0u);
}

TEST(MCTSEndgameSolverTest, SolvedLeafOverridesPolicyAndValue) {
  Board board;
  const BoardSignature root_signature = board.signature();
  int solver_calls = 0;
  int solved_leaf_action = -1;

  EndgameSolver solver = [&](const Board& candidate) {
    ++solver_calls;
    if (candidate.signature() == root_signature) return std::vector<int>{};

    const auto legal_actions = candidate.GetLegalActions();
    if (legal_actions.empty()) return std::vector<int>{};
    solved_leaf_action = legal_actions.front();
    return std::vector<int>{solved_leaf_action};
  };

  CountingEvaluator evaluator;
  MCTS mcts(1, 1, 1.0f);
  mcts.Search(board, &evaluator, solver);

  EXPECT_GE(solver_calls, 2);
  EXPECT_EQ(evaluator.calls, 1);  // Root only; the leaf was solved.
  ASSERT_NE(mcts.root(), nullptr);

  const MCTSNode* expanded_child = nullptr;
  for (const auto& child : mcts.root()->children()) {
    if (child->is_expanded()) {
      expanded_child = child.get();
      break;
    }
  }
  ASSERT_NE(expanded_child, nullptr);
  ASSERT_NE(solved_leaf_action, -1);

  const MCTSNode* solved_action_child = nullptr;
  for (const auto& child : expanded_child->children()) {
    if (child->action_id() == solved_leaf_action) {
      solved_action_child = child.get();
      break;
    }
  }
  ASSERT_NE(solved_action_child, nullptr);
  EXPECT_FLOAT_EQ(solved_action_child->prior_prob(), 1.0f);
  EXPECT_EQ(expanded_child->visits(), 1);
  EXPECT_FLOAT_EQ(expanded_child->value_sum(), 1.0f);
}
