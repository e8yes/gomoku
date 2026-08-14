#include "neural_net_evaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neural_net_evaluator_internal {

// ---------------------------------------------------------------------------
// EncodeBoard
//
// Encodes a single Board directly into raw float buffer [9, 15, 15] in C-order.
// ---------------------------------------------------------------------------
void EncodeBoard(const Board& board, float* out) {
  constexpr int kPlaneSize = Board::kNumCells;  // 225
  std::fill_n(out, NeuralNetEvaluator::kNumInputChannels * kPlaneSize, 0.0f);

  const Player actual_cur = board.stone_to_place();
  // Default to Black for first-person view if no stone is explicitly to be placed
  const Player cur = (actual_cur != Player::kNone) ? actual_cur : Player::kBlack;
  const Player opp = (cur == Player::kBlack) ? Player::kWhite : Player::kBlack;

  float* ch0 = out;
  float* ch1 = out + kPlaneSize;

  for (int y = 0; y < Board::kSize; ++y) {
    for (int x = 0; x < Board::kSize; ++x) {
      const Player cell = board.cell(x, y);
      const int idx = y * Board::kSize + x;
      if (cell == cur) ch0[idx] = 1.0f;
      else if (cell == opp) ch1[idx] = 1.0f;
    }
  }

  const float ch2_fill = (actual_cur == Player::kBlack) ? 1.0f : 0.0f;
  const float ch3_fill = (actual_cur == Player::kWhite) ? 1.0f : 0.0f;
  const float ch4_fill = (board.phase() == Phase::kPlaceInitialThree) ? 1.0f : 0.0f;
  const float ch5_fill = (board.phase() == Phase::kSwap2Decision) ? 1.0f : 0.0f;
  const float ch6_fill = (board.phase() == Phase::kSwap2PlaceTwo) ? 1.0f : 0.0f;
  const float ch7_fill = (board.phase() == Phase::kChooseColor) ? 1.0f : 0.0f;
  const float ch8_fill = (board.phase() == Phase::kStandard) ? 1.0f : 0.0f;

  if (ch2_fill != 0.0f) std::fill_n(out + 2 * kPlaneSize, kPlaneSize, ch2_fill);
  if (ch3_fill != 0.0f) std::fill_n(out + 3 * kPlaneSize, kPlaneSize, ch3_fill);
  if (ch4_fill != 0.0f) std::fill_n(out + 4 * kPlaneSize, kPlaneSize, ch4_fill);
  if (ch5_fill != 0.0f) std::fill_n(out + 5 * kPlaneSize, kPlaneSize, ch5_fill);
  if (ch6_fill != 0.0f) std::fill_n(out + 6 * kPlaneSize, kPlaneSize, ch6_fill);
  if (ch7_fill != 0.0f) std::fill_n(out + 7 * kPlaneSize, kPlaneSize, ch7_fill);
  if (ch8_fill != 0.0f) std::fill_n(out + 8 * kPlaneSize, kPlaneSize, ch8_fill);
}

// Encodes a single Board into a [kNumInputChannels, kBoardSize, kBoardSize] CPU tensor.
torch::Tensor BoardToTensor(const Board& board) {
  auto t = torch::empty(
      {NeuralNetEvaluator::kNumInputChannels, Board::kSize, Board::kSize},
      torch::kFloat32);
  EncodeBoard(board, t.data_ptr<float>());
  return t;
}

}  // namespace neural_net_evaluator_internal

// ---------------------------------------------------------------------------
// NeuralNetEvaluator
// ---------------------------------------------------------------------------
NeuralNetEvaluator::NeuralNetEvaluator(
    std::shared_ptr<BatchInferenceExecutor> executor)
    : executor_(std::move(executor)) {}

std::vector<EvaluationResult> NeuralNetEvaluator::Evaluate(
    const std::vector<Board>& boards) {
  if (boards.empty()) return {};

  const int batch_size = static_cast<int>(boards.size());
  constexpr int kFloatsPerBoard =
      NeuralNetEvaluator::kNumInputChannels * Board::kNumCells;

  auto batched_input = torch::empty(
      {batch_size, NeuralNetEvaluator::kNumInputChannels, Board::kSize,
       Board::kSize},
      torch::kFloat32);
  float* input_ptr = batched_input.data_ptr<float>();

  for (int i = 0; i < batch_size; ++i) {
    neural_net_evaluator_internal::EncodeBoard(
        boards[i], input_ptr + i * kFloatsPerBoard);
  }

  auto future = executor_->Submit(std::move(batched_input));
  auto [policy_logits, values] = future.get();  // [batch_size, A], [batch_size, 1]

  const float* logits_ptr = policy_logits.data_ptr<float>();
  const float* values_ptr = values.data_ptr<float>();

  std::vector<EvaluationResult> results;
  results.reserve(batch_size);

  for (int i = 0; i < batch_size; ++i) {
    const float* board_logits = logits_ptr + i * Board::kNumActions;
    const std::vector<int> legal_actions = boards[i].GetLegalActions();

    EvaluationResult result;
    result.move_pmf.assign(Board::kNumActions, 0.0f);
    result.value = values_ptr[i];

    if (!legal_actions.empty()) {
      float max_logit = -std::numeric_limits<float>::infinity();
      for (int a : legal_actions) {
        if (board_logits[a] > max_logit) {
          max_logit = board_logits[a];
        }
      }

      float sum_exp = 0.0f;
      for (int a : legal_actions) {
        const float exp_val = std::exp(board_logits[a] - max_logit);
        result.move_pmf[a] = exp_val;
        sum_exp += exp_val;
      }

      if (sum_exp > 0.0f) {
        const float inv_sum = 1.0f / sum_exp;
        for (int a : legal_actions) {
          result.move_pmf[a] *= inv_sum;
        }
      }
    }

    results.push_back(std::move(result));
  }

  return results;
}

