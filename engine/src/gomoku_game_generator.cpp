#include <gflags/gflags.h>
#include <glog/logging.h>
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

DEFINE_int32(games, 0, "Number of games to generate (required, > 0)");
DEFINE_int32(iteration, -1, "Curriculum iteration number (required, 0..50)");
DEFINE_int32(simulations, 800, "MCTS simulations per move (> 0)");
DEFINE_string(out_dir, "", "Output directory path for dataset shards (required)");
DEFINE_string(champion_model_path, "",
              "Path to current champion model package (.pt2)");
DEFINE_string(previous_champion_model_path, "",
              "Path to previous champion model package (.pt2)");
DEFINE_double(previous_champion_mix_fraction, 0.0,
              "Fraction of self-play games against previous champion (0.0..1.0)");
DEFINE_bool(disable_endgame_solver, false, "Disable VCF endgame solver");
DEFINE_int32(workers, 12, "Number of concurrent worker threads (> 0)");

namespace {

constexpr int kSearchBatchSize = 32;
constexpr int kInferenceBatchRequests = 6;
constexpr int kInferenceWaitMicroseconds = 500;
constexpr float kCPuct = 1.0f;
constexpr float kDirichletAlpha = 0.3f;
constexpr float kDirichletEpsilon = 0.25f;
constexpr int kExplorationPlies = 6;

void ValidateFlags() {
  if (FLAGS_games <= 0) {
    LOG(FATAL) << "--games must be greater than zero";
  }
  if (FLAGS_workers <= 0) {
    LOG(FATAL) << "--workers must be greater than zero";
  }
  if (FLAGS_simulations <= 0) {
    LOG(FATAL) << "--simulations must be greater than zero";
  }
  if (FLAGS_iteration < 0 || FLAGS_iteration > 50) {
    LOG(FATAL) << "--iteration must be between 0 and 50";
  }
  if (FLAGS_out_dir.empty()) {
    LOG(FATAL) << "--out_dir is required";
  }
  if (FLAGS_previous_champion_mix_fraction < 0.0 ||
      FLAGS_previous_champion_mix_fraction > 1.0) {
    LOG(FATAL) << "--previous_champion_mix_fraction must be between 0.0 and 1.0";
  }
  const bool mixing_requested = FLAGS_previous_champion_mix_fraction > 0.0;
  if (mixing_requested != !FLAGS_previous_champion_model_path.empty()) {
    LOG(FATAL) << "Previous-champion model and positive mix fraction must be "
                  "supplied together";
  }
  if (mixing_requested && FLAGS_champion_model_path.empty()) {
    LOG(FATAL) << "Previous-champion mixing requires a current champion model";
  }
}

std::filesystem::path ShardPath() {
  std::ostringstream filename;
  filename << "iteration_" << std::setw(3) << std::setfill('0')
           << FLAGS_iteration << ".bin";
  return std::filesystem::path(FLAGS_out_dir) / filename.str();
}

std::uint64_t MakeSeed() {
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^
         static_cast<std::uint64_t>(random_device());
}

}  // namespace

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Gomoku self-play game generator.\n"
      "Usage: gomoku_game_generator --games N --iteration N --out_dir PATH "
      "[--simulations N] [--champion_model_path PATH] "
      "[--previous_champion_model_path PATH --previous_champion_mix_fraction F] "
      "[--disable_endgame_solver]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  try {
    ValidateFlags();
    const std::filesystem::path out_dir_path(FLAGS_out_dir);
    std::filesystem::create_directories(out_dir_path);

    const std::filesystem::path shard_path = ShardPath();
    std::ofstream output(shard_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      LOG(FATAL) << "Unable to open output shard: " << shard_path.string();
    }

    std::shared_ptr<BatchInferenceExecutor> inference_executor;
    std::unique_ptr<NeuralNetEvaluator> neural_evaluator;
    std::shared_ptr<BatchInferenceExecutor> previous_inference_executor;
    std::unique_ptr<NeuralNetEvaluator> previous_neural_evaluator;
    std::unique_ptr<RandomEvaluator> random_evaluator;
    Evaluator* evaluator = nullptr;

    const std::filesystem::path champion_path(FLAGS_champion_model_path);
    const std::filesystem::path previous_champion_path(
        FLAGS_previous_champion_model_path);

    if (!champion_path.empty()) {
      if (!std::filesystem::exists(champion_path)) {
        LOG(FATAL) << "Champion model does not exist: "
                   << champion_path.string();
      }
      if (!torch::cuda::is_available()) {
        LOG(FATAL) << "A champion model was supplied, but CUDA is not available";
      }

      inference_executor = std::make_shared<BatchInferenceExecutor>(
          champion_path, torch::Device(torch::kCUDA), kInferenceBatchRequests,
          std::chrono::microseconds(kInferenceWaitMicroseconds));
      neural_evaluator =
          std::make_unique<NeuralNetEvaluator>(std::move(inference_executor));
      evaluator = neural_evaluator.get();
      LOG(INFO) << "Using champion model: " << champion_path.string();

      if (!previous_champion_path.empty()) {
        if (!std::filesystem::exists(previous_champion_path)) {
          LOG(FATAL) << "Previous champion model does not exist: "
                     << previous_champion_path.string();
        }
        previous_inference_executor = std::make_shared<BatchInferenceExecutor>(
            previous_champion_path, torch::Device(torch::kCUDA),
            kInferenceBatchRequests,
            std::chrono::microseconds(kInferenceWaitMicroseconds));
        previous_neural_evaluator = std::make_unique<NeuralNetEvaluator>(
            std::move(previous_inference_executor));
        LOG(INFO) << "Using previous champion model: "
                  << previous_champion_path.string();
      }
    } else {
      random_evaluator = std::make_unique<RandomEvaluator>();
      evaluator = random_evaluator.get();
      LOG(INFO) << "No champion model supplied; using RandomEvaluator";
    }

    const std::uint64_t base_seed = MakeSeed();
    const int keep_last_moves = 0;
    const int mixed_game_count = static_cast<int>(std::llround(
        FLAGS_previous_champion_mix_fraction * FLAGS_games));
    const int worker_count = std::min(FLAGS_workers, FLAGS_games);
    LOG(INFO) << "Generating " << FLAGS_games << " games with "
              << worker_count << " workers, " << FLAGS_simulations
              << " simulations/move, root noise and stochastic actions for "
              << "the first " << kExplorationPlies
              << " decision plies, then argmax action selection with "
              << "neutral PUCT tie-breaking, keeping all non-terminal "
              << "positions"
              << (FLAGS_disable_endgame_solver
                      ? "; endgame solver disabled"
                      : "; VCF attacker enabled (defensive VCF disabled for self-play data generation)");
    if (mixed_game_count > 0) {
      LOG(INFO) << "Mixing " << mixed_game_count
                << " games against the previous champion with seats balanced; "
                   "only current-champion positions will be emitted from "
                   "those games";
    }

    EndgameSolver solver;
    EndgameDefenseSolver defensive_solver;
    if (!FLAGS_disable_endgame_solver) {
      solver = [](const Board& board) { return SolveVCF(board); };
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
          if (game >= FLAGS_games) break;

          SelfPlayConfig config;
          config.simulations = FLAGS_simulations;
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
                                             mixed_game_count / FLAGS_games;
          const std::int64_t mixed_through =
              static_cast<std::int64_t>(game + 1) * mixed_game_count /
              FLAGS_games;
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
                LOG(FATAL) << "Failed while writing output shard: "
                           << shard_path.string();
              }
            }

            const int completed =
                completed_games.fetch_add(1, std::memory_order_relaxed) + 1;
            if (completed % 100 == 0 || completed == FLAGS_games) {
              LOG(INFO) << "Generated " << completed << "/" << FLAGS_games
                        << " games";
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

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (std::thread& thread : workers) thread.join();

    if (first_error) std::rethrow_exception(first_error);
    output.close();
    if (!output) {
      LOG(FATAL) << "Failed while closing output shard: "
                 << shard_path.string();
    }

    LOG(INFO) << "Wrote self-play shard: " << shard_path.string();
    return 0;
  } catch (const std::exception& error) {
    LOG(ERROR) << "gomoku_game_generator: " << error.what();
    return 2;
  }
}
