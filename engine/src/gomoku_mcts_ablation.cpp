#include <gflags/gflags.h>
#include <glog/logging.h>
#include <torch/cuda.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "batch_inference_executor.h"
#include "mcts.h"
#include "neural_net_evaluator.h"

DEFINE_string(model, "", "Path to model package (.pt2) (required)");
DEFINE_int32(simulations, 800, "MCTS simulations per move (> 0)");

namespace {

constexpr int kInferenceWaitMicroseconds = 500;

struct SearchSetting {
  const char* name;
  int batch_size;
  float c_puct;
  int virtual_loss;
};

void ValidateFlags() {
  if (FLAGS_model.empty()) {
    LOG(FATAL) << "--model is required";
  }
  if (FLAGS_simulations <= 0) {
    LOG(FATAL) << "--simulations must be greater than zero";
  }
}

Board MakeQuietMidgame() {
  Board board;
  board.Apply(Action::FromXY(7, 7).id);
  board.Apply(Action::FromXY(7, 8).id);
  board.Apply(Action::FromXY(8, 7).id);
  board.Apply(Action::kSwap2ChooseBlack);

  for (const Action action :
       {Action::FromXY(6, 8), Action::FromXY(8, 8), Action::FromXY(6, 7),
        Action::FromXY(9, 8), Action::FromXY(7, 6), Action::FromXY(8, 9),
        Action::FromXY(6, 6), Action::FromXY(9, 7), Action::FromXY(5, 8),
        Action::FromXY(10, 8)}) {
    board.Apply(action.id);
  }
  return board;
}

Board MakeWhiteOpenThree() {
  Board board;
  board.Apply(Action::FromXY(7, 7).id);
  board.Apply(Action::FromXY(7, 8).id);
  board.Apply(Action::FromXY(8, 7).id);
  board.Apply(Action::kSwap2ChooseBlack);
  board.Apply(Action::FromXY(5, 9).id);  // White
  board.Apply(Action::FromXY(4, 4).id);  // Black
  board.Apply(Action::FromXY(6, 9).id);  // White
  board.Apply(Action::FromXY(4, 5).id);  // Black
  board.Apply(Action::FromXY(7, 9).id);  // White; Black must defend 4,9/8,9
  return board;
}

double Entropy(const std::vector<float>& policy) {
  double entropy = 0.0;
  for (float probability : policy) {
    if (probability > 0.0f) entropy -= probability * std::log(probability);
  }
  return entropy;
}

int NonzeroActions(const std::vector<float>& policy) {
  return static_cast<int>(std::count_if(policy.begin(), policy.end(),
                                        [](float p) { return p > 0.0f; }));
}

void RunScenario(const char* scenario_name, const Board& board,
                 NeuralNetEvaluator* evaluator, int simulations) {
  constexpr SearchSetting kSettings[] = {
      {"serial", 1, 1.0f, 0},
      {"batch32-vl0", 32, 1.0f, 0},
      {"batch32-vl1", 32, 1.0f, 1},
      {"current", 32, 1.0f, 3},
      {"batch32-c1.5-vl1", 32, 1.5f, 1},
  };

  std::cout
      << "\nScenario: " << scenario_name << " (phase "
      << static_cast<int>(board.phase()) << ", stone-to-place "
      << static_cast<int>(board.stone_to_place()) << ")\n"
      << "setting                 best move     p(best)  entropy  support\n";
  for (const SearchSetting& setting : kSettings) {
    MCTS mcts(setting.batch_size, setting.c_puct, std::nullopt,
              setting.virtual_loss);
    const std::vector<float> policy =
        mcts.Search(board, evaluator, SearchStoppingCriteria{simulations});
    const int best_action = GetBestAction(policy);
    std::cout << std::left << std::setw(23) << setting.name << std::right
              << std::setw(12) << Action(best_action).ToString() << std::fixed
              << std::setprecision(3) << std::setw(12) << policy[best_action]
              << std::setw(9) << Entropy(policy) << std::setw(9)
              << NonzeroActions(policy) << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Gomoku MCTS ablation diagnostics.\n"
      "Usage: gomoku_mcts_ablation --model PATH [--simulations N]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  try {
    ValidateFlags();
    const std::filesystem::path model_path(FLAGS_model);
    if (!std::filesystem::exists(model_path)) {
      LOG(FATAL) << "Model does not exist: " << model_path.string();
    }
    if (!torch::cuda::is_available()) {
      LOG(FATAL) << "CUDA is required for this ablation";
    }

    auto executor = std::make_shared<BatchInferenceExecutor>(
        model_path, torch::Device(torch::kCUDA), 1,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    NeuralNetEvaluator evaluator(executor);

    LOG(INFO) << "MCTS ablation using " << model_path.string() << " with "
              << FLAGS_simulations
              << " simulations, no Dirichlet noise or endgame solver.";
    RunScenario("quiet midgame", MakeQuietMidgame(), &evaluator,
                FLAGS_simulations);
    RunScenario("white open-three defense", MakeWhiteOpenThree(), &evaluator,
                FLAGS_simulations);
    return 0;
  } catch (const std::exception& error) {
    LOG(ERROR) << "gomoku_mcts_ablation: " << error.what();
    return 2;
  }
}
