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

  const Player actual_cur = board.stone_to_place();
  // Default to Black for first-person view if no stone is explicitly to be placed
  const Player cur = (actual_cur != Player::kNone) ? actual_cur : Player::kBlack;
  const Player opp = (cur == Player::kBlack) ? Player::kWhite : Player::kBlack;
  
  const float ch2_fill = (actual_cur == Player::kBlack) ? 1.0f : 0.0f;
  const float ch3_fill = (actual_cur == Player::kWhite) ? 1.0f : 0.0f;
  const float ch4_fill = (board.phase() == Phase::kPlaceInitialThree) ? 1.0f : 0.0f;
  const float ch5_fill = (board.phase() == Phase::kSwap2Decision) ? 1.0f : 0.0f;
  const float ch6_fill = (board.phase() == Phase::kSwap2PlaceTwo) ? 1.0f : 0.0f;
  const float ch7_fill = (board.phase() == Phase::kChooseColor) ? 1.0f : 0.0f;
  const float ch8_fill = (board.phase() == Phase::kStandard) ? 1.0f : 0.0f;

  for (int y = 0; y < Board::kSize; ++y) {
    for (int x = 0; x < Board::kSize; ++x) {
      const Player cell = board.cell(x, y);
      if (cell == cur) acc[0][y][x] = 1.0f;
      if (cell == opp) acc[1][y][x] = 1.0f;
      acc[2][y][x] = ch2_fill;
      acc[3][y][x] = ch3_fill;
      acc[4][y][x] = ch4_fill;
      acc[5][y][x] = ch5_fill;
      acc[6][y][x] = ch6_fill;
      acc[7][y][x] = ch7_fill;
      acc[8][y][x] = ch8_fill;
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

std::vector<EvaluationResult> NeuralNetEvaluator::Evaluate(const std::vector<Board>& boards) {
  if (boards.empty()) return {};

  int batch_size = boards.size();
  std::vector<torch::Tensor> tensor_list;
  tensor_list.reserve(batch_size);
  for (const auto& b : boards) {
      tensor_list.push_back(neural_net_evaluator_internal::BoardToTensor(b));
  }
  
  // Shape: [batch_size, 9, 15, 15]
  torch::Tensor batched_input = torch::stack(tensor_list, 0);

  auto future = executor_->Submit(std::move(batched_input));
  auto [policy_logits, values] = future.get(); // [batch_size, A], [batch_size, 1]

  std::vector<EvaluationResult> results;
  results.reserve(batch_size);
  for (int i = 0; i < batch_size; ++i) {
      results.push_back(DecodeOutput(policy_logits[i], values[i], boards[i].GetLegalActions()));
  }
  
  return results;
}
