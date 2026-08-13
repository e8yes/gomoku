#include <torch/cuda.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "batch_inference_executor.h"
#include "game_data.h"
#include "neural_net_evaluator.h"
#include "random_evaluator.h"
#include "self_play.h"
#include "vcf_solver.h"

namespace {

constexpr int kWorkerCount = 12;
constexpr int kSimulationsPerMove = 400;
constexpr int kSearchBatchSize = 32;
constexpr int kInferenceBatchRequests = 6;
constexpr int kInferenceWaitMicroseconds = 500;
constexpr float kCPuct = 1.0f;
constexpr float kDirichletAlpha = 0.3f;
constexpr float kDirichletEpsilon = 0.25f;

struct Arguments {
  int games = 0;
  int iteration = -1;
  std::filesystem::path out_dir;
  std::filesystem::path champion_model_path;
};

void PrintUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " --games N --iteration N --out_dir PATH"
               " [--champion_model_path PATH]\n";
}

int ParseInt(const std::string& text, const char* option) {
  std::size_t consumed = 0;
  long long value = 0;
  try {
    value = std::stoll(text, &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(option) + " requires an integer");
  }
  if (consumed != text.size() || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(option) + " requires an integer");
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
    if (option == "--games") {
      arguments.games = ParseInt(value, "--games");
    } else if (option == "--iteration") {
      arguments.iteration = ParseInt(value, "--iteration");
    } else if (option == "--out_dir") {
      arguments.out_dir = value;
    } else if (option == "--champion_model_path") {
      arguments.champion_model_path = value;
    } else {
      throw std::invalid_argument("Unknown option: " + option);
    }
  }

  if (arguments.games <= 0) {
    throw std::invalid_argument("--games must be greater than zero");
  }
  if (arguments.iteration < 0 || arguments.iteration > 50) {
    throw std::invalid_argument("--iteration must be between 0 and 50");
  }
  if (arguments.out_dir.empty()) {
    throw std::invalid_argument("--out_dir is required");
  }
  return arguments;
}

std::filesystem::path ShardPath(const Arguments& arguments) {
  std::ostringstream filename;
  filename << "iteration_" << std::setw(3) << std::setfill('0')
           << arguments.iteration << ".bin";
  return arguments.out_dir / filename.str();
}

std::uint64_t MakeSeed() {
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^
         static_cast<std::uint64_t>(random_device());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    std::filesystem::create_directories(arguments.out_dir);

    const std::filesystem::path shard_path = ShardPath(arguments);
    std::ofstream output(shard_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Unable to open output shard: " +
                               shard_path.string());
    }

    // The evaluator is shared by all game workers. BatchInferenceExecutor is
    // multi-producer/single-consumer, so each MCTS can submit its 32-leaf
    // small-batches while one inference thread coalesces up to 6 requests
    // (192 boards) for a GPU pass.
    std::shared_ptr<BatchInferenceExecutor> inference_executor;
    std::unique_ptr<NeuralNetEvaluator> neural_evaluator;
    std::unique_ptr<RandomEvaluator> random_evaluator;
    Evaluator* evaluator = nullptr;

    if (!arguments.champion_model_path.empty()) {
      if (!std::filesystem::exists(arguments.champion_model_path)) {
        throw std::invalid_argument("Champion model does not exist: " +
                                    arguments.champion_model_path.string());
      }
      if (!torch::cuda::is_available()) {
        throw std::runtime_error(
            "A champion model was supplied, but CUDA is not available");
      }

      inference_executor = std::make_shared<BatchInferenceExecutor>(
          arguments.champion_model_path, torch::Device(torch::kCUDA),
          kInferenceBatchRequests,
          std::chrono::microseconds(kInferenceWaitMicroseconds));
      neural_evaluator =
          std::make_unique<NeuralNetEvaluator>(std::move(inference_executor));
      evaluator = neural_evaluator.get();
      std::cout << "Using champion model: "
                << arguments.champion_model_path.string() << "\n";
    } else {
      random_evaluator = std::make_unique<RandomEvaluator>();
      evaluator = random_evaluator.get();
      std::cout << "No champion model supplied; using RandomEvaluator\n";
    }

    const std::uint64_t base_seed = MakeSeed();
    const int keep_last_moves = arguments.iteration + 3;
    std::cout << "Generating " << arguments.games << " games with "
              << kWorkerCount << " workers, " << kSimulationsPerMove
              << " simulations/move, keeping the final " << keep_last_moves
              << " positions\n";

    EndgameSolver solver = [](const Board& board) { return SolveVCF(board); };

    std::atomic<int> next_game{0};
    std::atomic<int> completed_games{0};
    std::atomic<bool> stop{false};
    std::mutex output_mutex;
    std::mutex error_mutex;
    std::exception_ptr first_error;

    auto worker = [&]() {
      try {
        while (!stop.load(std::memory_order_relaxed)) {
          const int game = next_game.fetch_add(1, std::memory_order_relaxed);
          if (game >= arguments.games) break;

          Config config;
          config.simulations = kSimulationsPerMove;
          config.batch_size = kSearchBatchSize;
          config.c_puct = kCPuct;
          config.seed = base_seed + static_cast<std::uint64_t>(game);
          config.keep_last_moves = keep_last_moves;
          config.dirichlet_noise = DirichletNoiseConfig{
              kDirichletAlpha, kDirichletEpsilon, config.seed};

          const std::vector<TrainingExample> examples =
              GenerateGame(config, evaluator, solver);

          {
            std::lock_guard<std::mutex> lock(output_mutex);
            for (const auto& example : examples) {
              if (!WriteExample(output, example.board, example.policy,
                                example.value)) {
                throw std::runtime_error("Failed while writing output shard: " +
                                         shard_path.string());
              }
            }

            const int completed =
                completed_games.fetch_add(1, std::memory_order_relaxed) + 1;
            if (completed % 100 == 0 || completed == arguments.games) {
              std::cout << "Generated " << completed << "/" << arguments.games
                        << " games\n";
            }
          }
        }
      } catch (...) {
        {
          std::lock_guard<std::mutex> lock(error_mutex);
          if (!first_error) first_error = std::current_exception();
        }
        stop.store(true, std::memory_order_relaxed);
      }
    };

    const int worker_count = std::min(kWorkerCount, arguments.games);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (std::thread& thread : workers) thread.join();

    if (first_error) std::rethrow_exception(first_error);
    output.close();
    if (!output) {
      throw std::runtime_error("Failed while closing output shard: " +
                               shard_path.string());
    }

    std::cout << "Wrote self-play shard: " << shard_path << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gomoku_game_generator: " << error.what() << "\n";
    PrintUsage(argv[0]);
    return 2;
  }
}
