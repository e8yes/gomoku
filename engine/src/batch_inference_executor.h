#pragma once
#include <filesystem>
#include <future>
#include <thread>
#include <utility>

#include <torch/script.h>
#include <torch/torch.h>

#include "batched_blocking_queue.h"

// BatchInferenceExecutor manages GPU batched inference for a TorchScript model.
//
// MCTS search threads call Submit() with a pre-encoded CPU tensor. A dedicated
// inference thread collects concurrent submits via BatchedBlockingQueue, stacks
// them into a single GPU batch, runs one forward pass, and fulfils each
// caller's std::future with their output slice (returned to CPU).
//
// The shared_ptr<BatchInferenceExecutor> pattern allows one executor to serve
// many NeuralNetEvaluator instances across multiple concurrent game threads.
class BatchInferenceExecutor {
 public:
  // Two CPU tensors from the model output, one per submitted input.
  using Output = std::pair<torch::Tensor, torch::Tensor>;

  // Loads the TorchScript model onto device and starts the inference thread.
  BatchInferenceExecutor(const std::filesystem::path& model_path,
                         torch::Device device, int max_batch_size,
                         std::chrono::microseconds max_wait_us);

  ~BatchInferenceExecutor();

  BatchInferenceExecutor(const BatchInferenceExecutor&) = delete;
  BatchInferenceExecutor& operator=(const BatchInferenceExecutor&) = delete;

  // Thread-safe. Enqueues a pre-encoded CPU tensor and returns a future that
  // resolves to the model's output for that input, on CPU.
  std::future<Output> Submit(torch::Tensor input);

 private:
  struct Request {
    torch::Tensor input;
    std::promise<Output> promise;

    explicit Request(torch::Tensor t) : input(std::move(t)) {}
    Request(Request&&) = default;
    Request& operator=(Request&&) = default;
  };

  torch::jit::Module model_;
  torch::Device device_;
  BatchedBlockingQueue<std::unique_ptr<Request>> queue_;
  std::thread inference_thread_;

  void InferenceLoop();
};
