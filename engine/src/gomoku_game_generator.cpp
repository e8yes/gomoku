#include <torch/cuda.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
constexpr int kDefaultSimulationsPerMove = 800;
constexpr int kSearchBatchSize = 32;
constexpr int kInferenceBatchRequests = 6;
constexpr int kInferenceWaitMicroseconds = 500;
constexpr float kCPuct = 1.0f;
constexpr float kDirichletAlpha = 0.3f;
constexpr float kDirichletEpsilon = 0.25f;
constexpr int kExplorationPlies = 6;

struct Arguments {
  int games = 0;
  int iteration = -1;
  int simulations = kDefaultSimulationsPerMove;
  std::filesystem::path out_dir;
  std::filesystem::path champion_model_path;
  std::filesystem::path previous_champion_model_path;
  double previous_champion_mix_fraction = 0.0;
  bool disable_endgame_solver = false;
};

void PrintUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " --games N --iteration N --out_dir PATH"
               " [--simulations N] [--champion_model_path PATH]"
               " [--previous_champion_model_path PATH"
               " --previous_champion_mix_fraction F]"
               " [--disable_endgame_solver]\n";
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

double ParseDouble(const std::string& text, const char* option) {
  std::size_t consumed = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(option) + " requires a number");
  }
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::invalid_argument(std::string(option) + " requires a number");
  }
  return value;
}

Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help" || option == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    }
    if (option == "--disable_endgame_solver") {
      arguments.disable_endgame_solver = true;
      continue;
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument(option + " requires a value");
    }

    const std::string value = argv[++i];
    if (option == "--games") {
      arguments.games = ParseInt(value, "--games");
    } else if (option == "--iteration") {
      arguments.iteration = ParseInt(value, "--iteration");
    } else if (option == "--simulations") {
      arguments.simulations = ParseInt(value, "--simulations");
    } else if (option == "--out_dir") {
      arguments.out_dir = value;
    } else if (option == "--champion_model_path") {
      arguments.champion_model_path = value;
    } else if (option == "--previous_champion_model_path") {
      arguments.previous_champion_model_path = value;
    } else if (option == "--previous_champion_mix_fraction") {
      arguments.previous_champion_mix_fraction =
          ParseDouble(value, "--previous_champion_mix_fraction");
    } else {
      throw std::invalid_argument("Unknown option: " + option);
    }
  }

  if (arguments.games <= 0) {
    throw std::invalid_argument("--games must be greater than zero");
  }
  if (arguments.simulations <= 0) {
    throw std::invalid_argument("--simulations must be greater than zero");
  }
  if (arguments.iteration < 0 || arguments.iteration > 50) {
    throw std::invalid_argument("--iteration must be between 0 and 50");
  }
  if (arguments.out_dir.empty()) {
    throw std::invalid_argument("--out_dir is required");
  }
  if (arguments.previous_champion_mix_fraction < 0.0 ||
      arguments.previous_champion_mix_fraction > 1.0) {
    throw std::invalid_argument(
        "--previous_champion_mix_fraction must be between zero and one");
  }
  const bool mixing_requested = arguments.previous_champion_mix_fraction > 0.0;
  if (mixing_requested != !arguments.previous_champion_model_path.empty()) {
    throw std::invalid_argument(
        "Previous-champion model and positive mix fraction must be supplied "
        "together");
  }
  if (mixing_requested && arguments.champion_model_path.empty()) {
    throw std::invalid_argument(
        "Previous-champion mixing requires a current champion model");
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
    std::shared_ptr<BatchInferenceExecutor> previous_inference_executor;
    std::unique_ptr<NeuralNetEvaluator> previous_neural_evaluator;
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

      if (!arguments.previous_champion_model_path.empty()) {
        if (!std::filesystem::exists(arguments.previous_champion_model_path)) {
          throw std::invalid_argument(
              "Previous champion model does not exist: " +
              arguments.previous_champion_model_path.string());
        }
        previous_inference_executor = std::make_shared<BatchInferenceExecutor>(
            arguments.previous_champion_model_path, torch::Device(torch::kCUDA),
            kInferenceBatchRequests,
            std::chrono::microseconds(kInferenceWaitMicroseconds));
        previous_neural_evaluator = std::make_unique<NeuralNetEvaluator>(
            std::move(previous_inference_executor));
        std::cout << "Using previous champion model: "
                  << arguments.previous_champion_model_path.string() << "\n";
      }
    } else {
      random_evaluator = std::make_unique<RandomEvaluator>();
      evaluator = random_evaluator.get();
      std::cout << "No champion model supplied; using RandomEvaluator\n";
    }

    const std::uint64_t base_seed = MakeSeed();
    // Emit every non-terminal position. Recency is controlled by the Python
    // dataset sampler instead of an iteration-dependent horizon window.
    const int keep_last_moves = 0;
    const int mixed_game_count = static_cast<int>(std::llround(
        arguments.previous_champion_mix_fraction * arguments.games));
    std::cout << "Generating " << arguments.games << " games with "
              << kWorkerCount << " workers, " << arguments.simulations
              << " simulations/move, root noise and stochastic actions for "
              << "the first " << kExplorationPlies
              << " decision plies, then argmax action selection with "
              << "neutral PUCT tie-breaking, keeping all non-terminal "
              << "positions"
              << (arguments.disable_endgame_solver
                      ? "; endgame solver disabled"
                      : "; VCF attacker/defender enabled")
              << "\n";
    if (mixed_game_count > 0) {
      std::cout << "Mixing " << mixed_game_count
                << " games against the previous champion with seats balanced; "
                   "only current-champion positions will be emitted from "
                   "those games\n";
    }

    EndgameSolver solver;
    EndgameDefenseSolver defensive_solver;
    if (!arguments.disable_endgame_solver) {
      solver = [](const Board& board) { return SolveVCF(board); };
      defensive_solver = [](const Board& board) {
        return AnalyzeVCFDefense(board);
      };
    }

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

          SelfPlayConfig config;
          config.simulations = arguments.simulations;
          config.batch_size = kSearchBatchSize;
          config.c_puct = kCPuct;
          config.seed = base_seed + static_cast<std::uint64_t>(game);
          config.keep_last_moves = keep_last_moves;
          config.dirichlet_noise = DirichletNoiseConfig{
              kDirichletAlpha, kDirichletEpsilon, config.seed};
          config.dirichlet_noise_plies = kExplorationPlies;
          config.stochastic_action_plies = kExplorationPlies;
          std::vector<TrainingExample> examples;
          const std::int64_t mixed_before = static_cast<std::int64_t>(game) *
                                            mixed_game_count / arguments.games;
          const std::int64_t mixed_through =
              static_cast<std::int64_t>(game + 1) * mixed_game_count /
              arguments.games;
          const bool mixed_game = mixed_through > mixed_before;
          if (mixed_game) {
            const int mixed_ordinal = static_cast<int>(mixed_through - 1);
            const Seat current_champion_seat =
                mixed_ordinal % 2 == 0 ? Seat::kA : Seat::kB;
            const EvaluatorSelector selector =
                [&](const Board& board) -> Evaluator* {
              return board.current_player() == current_champion_seat
                         ? evaluator
                         : previous_neural_evaluator.get();
            };
            const TrainingPositionFilter keep_current_champion =
                [&](const Board& board) {
                  return board.current_player() == current_champion_seat;
                };
            examples = GenerateGame(config, selector, keep_current_champion,
                                    solver, defensive_solver);
          } else {
            examples =
                GenerateGame(config, evaluator, solver, defensive_solver);
          }

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
