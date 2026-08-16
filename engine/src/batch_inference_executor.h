#pragma once

#include <torch/csrc/inductor/aoti_package/model_package_loader.h>
#include <torch/torch.h>

#include <filesystem>
#include <future>
#include <thread>
#include <utility>

#include "batched_blocking_queue.h"
#include "inference_executor.h"

// BatchInferenceExecutor manages GPU batched inference for an AOTInductor
// model package. AOTInductor compiles the exported model to optimized
// CUDA/Triton kernels, while this class preserves the engine's batched
// request/future interface.
//
// MCTS search threads call Submit() with a batched CPU tensor (a
// "small-batch"). A dedicated inference thread collects concurrent submits via
// BatchedBlockingQueue, concatenates them into a single large GPU batch, runs
// one forward pass, and fulfils each caller's std::future with their output
// slices.
//
// The shared_ptr<BatchInferenceExecutor> pattern allows one executor to serve
// many NeuralNetEvaluator instances across multiple concurrent game threads.
class BatchInferenceExecutor : public InferenceExecutor {
 public:
  // Loads the AOTInductor .pt2 package and starts the inference thread.
  // max_requests defines the maximum number of small-batch requests to
  // accumulate per inference pass.
  BatchInferenceExecutor(const std::filesystem::path& model_path,
                         torch::Device device, int max_requests,
                         std::chrono::microseconds max_wait_us);

  ~BatchInferenceExecutor() override;

  BatchInferenceExecutor(const BatchInferenceExecutor&) = delete;
  BatchInferenceExecutor& operator=(const BatchInferenceExecutor&) = delete;

  // Thread-safe. Enqueues a pre-encoded CPU tensor and returns a future that
  // resolves to the model's output for that input, on CPU.
  std::future<Output> Submit(torch::Tensor input) override;

 private:
  struct Request {
    torch::Tensor input;
    std::promise<Output> promise;

    explicit Request(torch::Tensor t) : input(std::move(t)) {}
    Request(Request&&) = default;
    Request& operator=(Request&&) = default;
  };

  std::unique_ptr<torch::inductor::AOTIModelPackageLoader> model_;
  torch::Device device_;
  BatchedBlockingQueue<std::unique_ptr<Request>> queue_;
  std::thread inference_thread_;

  void InferenceLoop();
};
