#include "immediate_inference_executor.h"

#include <stdexcept>
#include <utility>

ImmediateInferenceExecutor::ImmediateInferenceExecutor(
    const std::filesystem::path& model_path, torch::Device device)
    : device_(device) {
  try {
    model_ = std::make_unique<torch::inductor::AOTIModelPackageLoader>(
        model_path.string(), "model", /*run_single_threaded=*/true);
  } catch (const std::exception& e) {
    throw std::runtime_error("Failed to load AOTInductor model package from '" +
                             model_path.string() + "': " + e.what());
  }
}

std::future<InferenceExecutor::Output> ImmediateInferenceExecutor::Submit(
    torch::Tensor input) {
  // Move CPU float32 tensor to GPU device in FP16 format for tensor core execution.
  torch::Tensor batch = input.to(device_).to(torch::kFloat16);

  auto outputs = model_->run({batch});
  if (outputs.size() != 2) {
    throw std::runtime_error(
        "AOTInductor model must return exactly policy and value tensors");
  }

  torch::Tensor policy = outputs[0].to(torch::kFloat32).cpu();
  torch::Tensor values = outputs[1].to(torch::kFloat32).cpu();

  std::promise<Output> promise;
  promise.set_value({std::move(policy), std::move(values)});
  return promise.get_future();
}
