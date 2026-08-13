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

namespace {

constexpr int kDefaultSimulations = 800;
constexpr int kInferenceWaitMicroseconds = 500;

struct Arguments {
  std::filesystem::path model_path;
  int simulations = kDefaultSimulations;
};

struct SearchSetting {
  const char* name;
  int batch_size;
  float c_puct;
  int virtual_loss;
};

void PrintUsage(const char* program) {
  std::cerr << "Usage: " << program << " --model PATH [--simulations N]\n";
}

int ParsePositiveInt(const std::string& text, const char* option) {
  std::size_t consumed = 0;
  const long value = std::stol(text, &consumed);
  if (consumed != text.size() || value <= 0 || value > INT_MAX) {
    throw std::invalid_argument(std::string(option) +
                                " requires a positive integer");
  }
  return static_cast<int>(value);
}

Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help" || option == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument(option + " requires a value");
    }
    const std::string value = argv[++i];
    if (option == "--model") {
      arguments.model_path = value;
    } else if (option == "--simulations") {
      arguments.simulations = ParsePositiveInt(value, "--simulations");
    } else {
      throw std::invalid_argument("Unknown option: " + option);
    }
  }
  if (arguments.model_path.empty()) {
    throw std::invalid_argument("--model is required");
  }
  return arguments;
}

Board MakeQuietMidgame() {
  Board board;
  board.Apply(Action::FromXY(7, 7).id);
  board.Apply(Action::FromXY(7, 8).id);
  board.Apply(Action::FromXY(8, 7).id);
  board.Apply(Action::kSwap2ChooseBlack);

  // A non-forcing central position after the direct Swap2 colour choice.
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
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    if (!std::filesystem::exists(arguments.model_path)) {
      throw std::invalid_argument("Model does not exist: " +
                                  arguments.model_path.string());
    }
    if (!torch::cuda::is_available()) {
      throw std::runtime_error("CUDA is required for this ablation");
    }

    // One request at a time makes each MCTS setting's batch size the only
    // batching variable; no cross-search queue coalescing can affect it.
    auto executor = std::make_shared<BatchInferenceExecutor>(
        arguments.model_path, torch::Device(torch::kCUDA), 1,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    NeuralNetEvaluator evaluator(executor);

    std::cout << "MCTS ablation using " << arguments.model_path << " with "
              << arguments.simulations
              << " simulations, no Dirichlet noise or endgame solver.\n";
    RunScenario("quiet midgame", MakeQuietMidgame(), &evaluator,
                arguments.simulations);
    RunScenario("white open-three defense", MakeWhiteOpenThree(), &evaluator,
                arguments.simulations);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gomoku_mcts_ablation: " << error.what() << "\n";
    PrintUsage(argv[0]);
    return 2;
  }
}
