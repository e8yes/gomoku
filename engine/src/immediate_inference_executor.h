#pragma once

#include <torch/csrc/inductor/aoti_package/model_package_loader.h>
#include <torch/torch.h>

#include <filesystem>
#include <future>
#include <memory>

#include "inference_executor.h"

// ImmediateInferenceExecutor runs model inference synchronously on the calling
// thread without background threads, queuing, or timeout waits.
// Ideal for single-worker / match-time latency where multi-thread batch pooling
// is unnecessary.
class ImmediateInferenceExecutor : public InferenceExecutor {
 public:
  ImmediateInferenceExecutor(const std::filesystem::path& model_path,
                             torch::Device device);

  ~ImmediateInferenceExecutor() override = default;

  ImmediateInferenceExecutor(const ImmediateInferenceExecutor&) = delete;
  ImmediateInferenceExecutor& operator=(const ImmediateInferenceExecutor&) = delete;

  // Synchronously evaluates the input tensor on GPU and returns a ready future.
  std::future<Output> Submit(torch::Tensor input) override;

 private:
  std::unique_ptr<torch::inductor::AOTIModelPackageLoader> model_;
  torch::Device device_;
};
