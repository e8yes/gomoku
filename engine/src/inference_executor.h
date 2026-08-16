#pragma once

#include <torch/torch.h>

#include <future>
#include <utility>

// Abstract base class for model inference executors.
class InferenceExecutor {
 public:
  // Two CPU tensors from the model output, representing the batched results
  // (policy and value) for the submitted batch.
  using Output = std::pair<torch::Tensor, torch::Tensor>;

  virtual ~InferenceExecutor() = default;

  // Submits a batched CPU tensor and returns a future resolving to (policy, value)
  // tensors on CPU.
  virtual std::future<Output> Submit(torch::Tensor input) = 0;
};
