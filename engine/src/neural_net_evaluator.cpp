#include "neural_net_evaluator.h"

#include <limits>

namespace neural_net_evaluator_internal {

// ---------------------------------------------------------------------------
// BoardToTensorImpl
//
// Encodes a single Board into a [kNumInputChannels, kBoardSize, kBoardSize]
// float32 CPU tensor. Not a class member because it has no dependency on
// NeuralNetEvaluator instance state.
// ---------------------------------------------------------------------------
torch::Tensor BoardToTensor(const Board& board) {
  auto t = torch::zeros(
      {NeuralNetEvaluator::kNumInputChannels, Board::kSize, Board::kSize});
  auto acc = t.accessor<float, 3>();

  const Player cur = board.stone_to_place();
  const Player opp = (cur == Player::kBlack) ? Player::kWhite : Player::kBlack;
  const float ch2_fill = (cur == Player::kBlack) ? 1.0f : 0.0f;
  const float ch3_fill = (cur == Player::kWhite) ? 1.0f : 0.0f;

  for (int y = 0; y < Board::kSize; ++y) {
    for (int x = 0; x < Board::kSize; ++x) {
      const Player cell = board.cell(x, y);
      if (cell == cur) acc[0][y][x] = 1.0f;
      if (cell == opp) acc[1][y][x] = 1.0f;
      acc[2][y][x] = ch2_fill;
      acc[3][y][x] = ch3_fill;
    }
  }
  return t;
}

}  // namespace neural_net_evaluator_internal

namespace {

// ---------------------------------------------------------------------------
// DecodeOutput
//
// Converts raw model output for one item into an EvaluationResult.
//   policy_logits : CPU tensor, shape [kNumActions]
//   value         : CPU tensor, shape [1]
//   legal_actions : list of legal action IDs for the current board
//
// Illegal actions are masked to -inf before softmax so they receive
// probability 0 in the returned move_pmf.
// ---------------------------------------------------------------------------
EvaluationResult DecodeOutput(const torch::Tensor& policy_logits,
                              const torch::Tensor& value,
                              const std::vector<int>& legal_actions) {
  auto mask = torch::full({Board::kNumActions},
                          -std::numeric_limits<float>::infinity());
  for (int a : legal_actions) mask[a] = 0.0f;

  auto probs = torch::softmax(policy_logits + mask, /*dim=*/0);
  auto probs_acc = probs.accessor<float, 1>();

  EvaluationResult result;
  result.move_pmf.resize(Board::kNumActions);
  for (int i = 0; i < Board::kNumActions; ++i)
    result.move_pmf[i] = probs_acc[i];

  result.value = value[0].item<float>();
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// NeuralNetEvaluator
// ---------------------------------------------------------------------------
NeuralNetEvaluator::NeuralNetEvaluator(
    std::shared_ptr<BatchInferenceExecutor> executor)
    : executor_(std::move(executor)) {}

EvaluationResult NeuralNetEvaluator::Evaluate(const Board& board) {
  auto future =
      executor_->Submit(neural_net_evaluator_internal::BoardToTensor(board));
  auto [policy_logits, value] = future.get();
  return DecodeOutput(policy_logits, value, board.GetLegalActions());
}
