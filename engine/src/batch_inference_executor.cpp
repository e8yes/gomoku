#include "batch_inference_executor.h"

#include <stdexcept>
#include <vector>

namespace {

// Concatenates batched CPU input tensors into a large batch, runs one GPU forward pass,
// brings outputs back to CPU, and returns one (policy_logits, value) pair per request
// by slicing the large output batch.
std::vector<BatchInferenceExecutor::Output> RunBatch(
    torch::jit::Module& model, torch::Device device,
    const std::vector<torch::Tensor>& inputs) {
  // Concatenate small-batch CPU float32 board tensors → [N_total, C, H, W], move to GPU.
  // Cast to FP16: the 4060 Ti has a 128-bit memory bus (288 GB/s), making
  // inference memory-bandwidth-bound. FP16 halves weight traffic per forward
  // pass and engages the tensor cores, roughly doubling throughput.
  torch::Tensor batch =
      torch::cat(inputs, /*dim=*/0).to(device).to(torch::kFloat16);

  torch::NoGradGuard no_grad;
  auto out = model.forward({batch}).toTuple();

  // Convert FP16 outputs back to FP32 on CPU so downstream consumers
  // (NeuralNetEvaluator::DecodeOutput) can use float accessors unchanged.
  torch::Tensor policy =
      out->elements()[0].toTensor().to(torch::kFloat32).cpu();  // [N_total, A]
  torch::Tensor values =
      out->elements()[1].toTensor().to(torch::kFloat32).cpu();  // [N_total, 1]


  const int N = static_cast<int>(inputs.size());
  std::vector<BatchInferenceExecutor::Output> results;
  results.reserve(N);
  
  int offset = 0;
  for (int i = 0; i < N; ++i) {
    int N_i = inputs[i].size(0);
    results.emplace_back(policy.slice(0, offset, offset + N_i).clone(),
                         values.slice(0, offset, offset + N_i).clone());
    offset += N_i;
  }
  return results;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
BatchInferenceExecutor::BatchInferenceExecutor(
    const std::filesystem::path& model_path, torch::Device device,
    int max_requests, std::chrono::microseconds max_wait_us)
    : device_(device), queue_(max_requests, max_wait_us) {
  try {
    model_ = torch::jit::load(model_path.string(), device_);
    model_.eval();
  } catch (const c10::Error& e) {
    throw std::runtime_error("Failed to load TorchScript model from '" +
                             model_path.string() + "': " + e.what());
  }
  inference_thread_ = std::thread(&BatchInferenceExecutor::InferenceLoop, this);
}

BatchInferenceExecutor::~BatchInferenceExecutor() {
  queue_.Shutdown();
  if (inference_thread_.joinable()) inference_thread_.join();
}

// ---------------------------------------------------------------------------
// Submit
// ---------------------------------------------------------------------------
std::future<BatchInferenceExecutor::Output> BatchInferenceExecutor::Submit(
    torch::Tensor input) {
  auto req = std::make_unique<Request>(std::move(input));
  std::future<Output> future = req->promise.get_future();
  queue_.Push(std::move(req));
  return future;
}

// ---------------------------------------------------------------------------
// InferenceLoop
// ---------------------------------------------------------------------------
void BatchInferenceExecutor::InferenceLoop() {
  while (true) {
    auto batch = queue_.PopBatch();
    if (batch.empty()) break;  // queue shut down and drained

    std::vector<torch::Tensor> inputs;
    inputs.reserve(batch.size());
    for (const auto& req : batch) inputs.push_back(req->input);

    auto results = RunBatch(model_, device_, inputs);

    for (size_t i = 0; i < batch.size(); ++i)
      batch[i]->promise.set_value(std::move(results[i]));
  }
}
