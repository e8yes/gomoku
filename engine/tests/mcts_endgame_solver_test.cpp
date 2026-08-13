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

Board MakeStandardBlackToMoveWithWhiteOpenThree() {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);  // Initial black stone.
  board.Apply(Action::FromXY(1, 0).id);  // Initial white stone.
  board.Apply(Action::FromXY(2, 0).id);  // Initial black stone.
  board.Apply(Action::kSwap2ChooseBlack);

  // Start standard play with one quiet White move, then add White's open
  // three while preserving Black's turn at the root. The Black placements
  // are deliberately scattered and create no forcing line of their own.
  board.Apply(Action::FromXY(10, 10).id);  // A white.
  board.Apply(Action::FromXY(14, 14).id);  // B black.
  board.Apply(Action::FromXY(5, 7).id);    // A white.
  board.Apply(Action::FromXY(14, 12).id);  // B black.
  board.Apply(Action::FromXY(6, 7).id);    // A white.
  board.Apply(Action::FromXY(12, 14).id);  // B black.
  board.Apply(Action::FromXY(7, 7).id);    // A white.

  return board;
}

}  // namespace

TEST(MCTSEndgameSolverTest, AcceptsFreeFunctionBoardAdapter) {
  Board board = MakeStandardBlackToMove();
  ASSERT_EQ(board.phase(), Phase::kStandard);
  ASSERT_EQ(board.stone_to_place(), Player::kBlack);

  RandomEvaluator evaluator;
  EndgameSolver solver = [](const Board& candidate) {
    return SolveVCF(candidate);
  };

  MCTS mcts(4, 1.0f);
  const std::vector<float> policy =
      mcts.Search(board, &evaluator, SearchStoppingCriteria{8}, solver);

  const int winning_action = Action::FromXY(4, 1).id;
  ASSERT_EQ(policy.size(), static_cast<std::size_t>(Board::kNumActions));
  EXPECT_FLOAT_EQ(policy[winning_action], 1.0f);
  EXPECT_EQ(GetBestAction(policy), winning_action);
}

TEST(MCTSEndgameSolverTest, DoesNotGenerateDefenseWithoutDefensiveCallback) {
  Board board = MakeStandardBlackToMoveWithWhiteOpenThree();
  ASSERT_EQ(board.phase(), Phase::kStandard);
  ASSERT_EQ(board.stone_to_place(), Player::kBlack);

  // The current callback proves wins for the side to move. It does not turn
  // White's open three into a forced Black defense at the root.
  ASSERT_TRUE(SolveVCF(board).empty());

  CountingEvaluator evaluator;
  EndgameSolver solver = [](const Board& candidate) {
    return SolveVCF(candidate);
  };

  // With one deterministic simulation, a defensive tactical override would
  // need to select one of the two endpoints. The current MCTS shape instead
  // falls through to the evaluator and visits the first legal root action.
  MCTS mcts(1, 1.0f);
  const std::vector<float> policy =
      mcts.Search(board, &evaluator, SearchStoppingCriteria{1}, solver);

  const int left_defense = Action::FromXY(4, 7).id;
  const int right_defense = Action::FromXY(8, 7).id;
  ASSERT_EQ(policy.size(), static_cast<std::size_t>(Board::kNumActions));
  EXPECT_FLOAT_EQ(policy[left_defense], 0.0f);
  EXPECT_FLOAT_EQ(policy[right_defense], 0.0f);
  EXPECT_NE(GetBestAction(policy), left_defense);
  EXPECT_NE(GetBestAction(policy), right_defense);
  EXPECT_GT(evaluator.calls, 0);
}

TEST(MCTSEndgameSolverTest, DefensiveVcfCallbackMasksWhiteOpenThreeThreat) {
  Board board = MakeStandardBlackToMoveWithWhiteOpenThree();
  CountingEvaluator evaluator;
  EndgameSolver solver = [](const Board& candidate) {
    return SolveVCF(candidate);
  };
  EndgameDefenseSolver defensive_solver = [](const Board& candidate) {
    return AnalyzeVCFDefense(candidate);
  };

  MCTS mcts(1, 1.0f);
  const std::vector<float> policy = mcts.Search(
      board, &evaluator, SearchStoppingCriteria{1}, solver, defensive_solver);

  const int left_defense = Action::FromXY(4, 7).id;
  const int right_defense = Action::FromXY(8, 7).id;
  ASSERT_EQ(policy.size(), static_cast<std::size_t>(Board::kNumActions));
  EXPECT_EQ(GetBestAction(policy), left_defense);
  EXPECT_FLOAT_EQ(policy[left_defense], 1.0f);
  EXPECT_FLOAT_EQ(policy[right_defense], 0.0f);
  for (int action : board.GetLegalActions()) {
    if (action == left_defense || action == right_defense) continue;
    EXPECT_FLOAT_EQ(policy[action], 0.0f);
  }
}

TEST(MCTSEndgameSolverTest, UnsolvedCallbackFallsBackToEvaluator) {
  Board board;
  CountingEvaluator evaluator;
  EndgameSolver solver = [](const Board&) { return std::vector<int>{}; };

  MCTS mcts(1, 1.0f);
  mcts.Search(board, &evaluator, SearchStoppingCriteria{1}, solver);

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
  MCTS mcts(1, 1.0f);
  mcts.Search(board, &evaluator, SearchStoppingCriteria{1}, solver);

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
