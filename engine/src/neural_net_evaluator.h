#pragma once
#include <memory>
#include <vector>

#include <torch/torch.h>

#include "batch_inference_executor.h"
#include "board.h"
#include "evaluator.h"

// NeuralNetEvaluator is the Evaluator adapter that bridges game semantics
// (Board, EvaluationResult) and the low-level BatchInferenceExecutor.
//
// Responsibilities:
//   - Encode a Board into the float32 input tensor expected by the model.
//   - Submit the tensor to the shared executor and block until evaluated.
//   - Decode the raw model output (policy logits + value) into an
//     EvaluationResult, applying illegal-move masking before softmax.
//
// Board encoding — 4 feature planes (kNumInputChannels = 4):
//   [0] Current player's stones  (1.0 where stone present, else 0.0)
//   [1] Opponent's stones        (1.0 where stone present, else 0.0)
//   [2] Constant 1.0 if current player is Black, else 0.0
//   [3] Constant 1.0 if current player is White, else 0.0
//
// The executor is shared: one BatchInferenceExecutor can be handed to many
// NeuralNetEvaluator instances across concurrent game threads so that MCTS
// leaf evaluations from all games coalesce into the same GPU batches.
class NeuralNetEvaluator : public Evaluator {
 public:
  static constexpr int kNumInputChannels = 4;

  explicit NeuralNetEvaluator(
      std::shared_ptr<BatchInferenceExecutor> executor);

  // Encodes the board, submits to the executor, blocks until the batch is
  // processed, decodes the result, and returns an EvaluationResult.
  EvaluationResult Evaluate(const Board& board) override;

  // Encodes one board into a [kNumInputChannels, kBoardSize, kBoardSize]
  // float32 CPU tensor. Public so unit tests can verify encoding correctness
  // without needing a real model.
  static torch::Tensor BoardToTensor(const Board& board);

 private:
  std::shared_ptr<BatchInferenceExecutor> executor_;
};
