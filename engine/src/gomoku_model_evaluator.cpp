#include <gflags/gflags.h>
#include <glog/logging.h>
#include <torch/cuda.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "batch_inference_executor.h"
#include "neural_net_evaluator.h"
#include "self_play.h"
#include "vcf_solver.h"

DEFINE_int32(games, 0, "Number of evaluation matches (required, > 0)");
DEFINE_int32(simulations, 1000, "MCTS simulations per move (> 0)");
DEFINE_string(champion_model_path, "",
              "Path to champion model package (.pt2) (required)");
DEFINE_string(challenger_model_path, "",
              "Path to challenger model package (.pt2) (required)");
DEFINE_string(out_dir, "", "Output directory path for evaluation.json");
DEFINE_string(out_file, "", "Direct output JSON file path");
DEFINE_bool(disable_endgame_solver, false, "Disable VCF endgame solver");

namespace {

constexpr int kWorkerCount = 12;
constexpr int kSearchBatchSize = 32;
constexpr int kInferenceBatchRequests = 6;
constexpr int kInferenceWaitMicroseconds = 500;
constexpr float kCPuct = 1.0f;
constexpr float kDirichletAlpha = 0.3f;
constexpr float kDirichletEpsilon = 0.25f;
constexpr double kConfidenceZ90 = 1.6448536269514722;

struct MatchOutcome {
  bool challenger_won = false;
  bool champion_won = false;
  bool draw = false;
  int action_count = 0;
};

struct Summary {
  int games = 0;
  int challenger_wins = 0;
  int champion_wins = 0;
  int draws = 0;
  double challenger_win_rate = 0.0;
  double confidence_low = 0.0;
  double confidence_high = 0.0;
  double mean_length = 0.0;
  double median_length = 0.0;
  double std_length = 0.0;
  int min_length = 0;
  int max_length = 0;
};

void ValidateFlags() {
  if (FLAGS_games <= 0) {
    LOG(FATAL) << "--games is required and must be positive";
  }
  if (FLAGS_simulations <= 0) {
    LOG(FATAL) << "--simulations must be greater than zero";
  }
  if (FLAGS_champion_model_path.empty()) {
    LOG(FATAL) << "--champion_model_path is required";
  }
  if (FLAGS_challenger_model_path.empty()) {
    LOG(FATAL) << "--challenger_model_path is required";
  }
  if (!FLAGS_out_dir.empty() && !FLAGS_out_file.empty()) {
    LOG(FATAL) << "Specify only one of --out_dir or --out_file";
  }
  if (FLAGS_out_dir.empty() && FLAGS_out_file.empty()) {
    LOG(FATAL) << "--out_dir is required";
  }
}

std::uint64_t MakeSeed() {
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^
         static_cast<std::uint64_t>(random_device());
}

std::filesystem::path ResolveOutputFile() {
  if (!FLAGS_out_file.empty()) return FLAGS_out_file;
  return std::filesystem::path(FLAGS_out_dir) / "evaluation.json";
}

std::pair<double, double> WilsonInterval90(int wins, int games) {
  if (games <= 0) return {0.0, 0.0};

  const double n = static_cast<double>(games);
  const double p = static_cast<double>(wins) / n;
  const double z_squared = kConfidenceZ90 * kConfidenceZ90;
  const double denominator = 1.0 + z_squared / n;
  const double center = (p + z_squared / (2.0 * n)) / denominator;
  const double margin =
      kConfidenceZ90 *
      std::sqrt((p * (1.0 - p) / n) + z_squared / (4.0 * n * n)) / denominator;
  return {std::max(0.0, center - margin), std::min(1.0, center + margin)};
}

Summary CalculateSummary(const std::vector<MatchOutcome>& outcomes) {
  Summary summary;
  summary.games = static_cast<int>(outcomes.size());
  if (outcomes.empty()) return summary;

  std::vector<int> lengths;
  lengths.reserve(outcomes.size());
  for (const MatchOutcome& outcome : outcomes) {
    summary.challenger_wins += outcome.challenger_won ? 1 : 0;
    summary.champion_wins += outcome.champion_won ? 1 : 0;
    summary.draws += outcome.draw ? 1 : 0;
    lengths.push_back(outcome.action_count);
  }

  summary.challenger_win_rate =
      static_cast<double>(summary.challenger_wins) / summary.games;
  std::tie(summary.confidence_low, summary.confidence_high) =
      WilsonInterval90(summary.challenger_wins, summary.games);

  std::sort(lengths.begin(), lengths.end());
  summary.min_length = lengths.front();
  summary.max_length = lengths.back();
  summary.mean_length =
      static_cast<double>(std::accumulate(lengths.begin(), lengths.end(), 0)) /
      lengths.size();
  if (lengths.size() % 2 == 0) {
    const std::size_t middle = lengths.size() / 2;
    summary.median_length = (lengths[middle - 1] + lengths[middle]) / 2.0;
  } else {
    summary.median_length = lengths[lengths.size() / 2];
  }

  double squared_error = 0.0;
  for (int length : lengths) {
    const double error = static_cast<double>(length) - summary.mean_length;
    squared_error += error * error;
  }
  summary.std_length = std::sqrt(squared_error / lengths.size());
  return summary;
}

void WriteSummary(const std::filesystem::path& output_file,
                  const Summary& summary) {
  const std::filesystem::path parent = output_file.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);

  std::ofstream output(output_file, std::ios::trunc);
  if (!output) {
    LOG(FATAL) << "Unable to open evaluation output: " << output_file.string();
  }

  output << std::setprecision(10);
  output << "{\n"
         << "  \"games\": " << summary.games << ",\n"
         << "  \"challenger_wins\": " << summary.challenger_wins << ",\n"
         << "  \"champion_wins\": " << summary.champion_wins << ",\n"
         << "  \"draws\": " << summary.draws << ",\n"
         << "  \"challenger_win_rate\": " << summary.challenger_win_rate
         << ",\n"
         << "  \"challenger_win_rate_ci_90\": {\n"
         << "    \"low\": " << summary.confidence_low << ",\n"
         << "    \"high\": " << summary.confidence_high << "\n"
         << "  },\n"
         << "  \"game_length\": {\n"
         << "    \"mean\": " << summary.mean_length << ",\n"
         << "    \"median\": " << summary.median_length << ",\n"
         << "    \"std\": " << summary.std_length << ",\n"
         << "    \"min\": " << summary.min_length << ",\n"
         << "    \"max\": " << summary.max_length << "\n"
         << "  }\n"
         << "}\n";
  if (!output) {
    LOG(FATAL) << "Failed while writing evaluation output: "
               << output_file.string();
  }
}

}  // namespace

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Gomoku champion vs challenger evaluator.\n"
      "Usage: gomoku_model_evaluator --games N --champion_model_path PATH "
      "--challenger_model_path PATH --out_dir PATH [--simulations N] "
      "[--disable_endgame_solver]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  try {
    ValidateFlags();
    const std::filesystem::path champion_path(FLAGS_champion_model_path);
    const std::filesystem::path challenger_path(FLAGS_challenger_model_path);

    if (!std::filesystem::exists(champion_path)) {
      LOG(FATAL) << "Champion model does not exist: " << champion_path.string();
    }
    if (!std::filesystem::exists(challenger_path)) {
      LOG(FATAL) << "Challenger model does not exist: "
                 << challenger_path.string();
    }
    if (!torch::cuda::is_available()) {
      LOG(FATAL) << "CUDA is required for model evaluation";
    }

    const std::filesystem::path output_file = ResolveOutputFile();
    if (!FLAGS_out_dir.empty()) {
      std::filesystem::create_directories(FLAGS_out_dir);
    }

    auto champion_executor = std::make_shared<BatchInferenceExecutor>(
        champion_path, torch::Device(torch::kCUDA), kInferenceBatchRequests,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    auto challenger_executor = std::make_shared<BatchInferenceExecutor>(
        challenger_path, torch::Device(torch::kCUDA), kInferenceBatchRequests,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    NeuralNetEvaluator champion_evaluator(champion_executor);
    NeuralNetEvaluator challenger_evaluator(challenger_executor);

    const std::uint64_t base_seed = MakeSeed();
    LOG(INFO) << "Evaluating " << FLAGS_games << " games with "
              << kWorkerCount << " workers, " << FLAGS_simulations
              << " simulations/move; root noise enabled for the first 4 plies";

    EndgameSolver solver;
    EndgameDefenseSolver defensive_solver;
    if (!FLAGS_disable_endgame_solver) {
      solver = [](const Board& board) { return SolveVCF(board); };
      defensive_solver = [](const Board& board) {
        return AnalyzeVCFDefense(board);
      };
    }
    LOG(INFO) << (FLAGS_disable_endgame_solver
                      ? "Endgame solver disabled"
                      : "VCF attacker/defender enabled");

    std::vector<MatchOutcome> outcomes(FLAGS_games);
    std::atomic<int> next_game{0};
    std::atomic<int> completed_games{0};
    std::atomic<bool> stop{false};
    std::mutex progress_mutex;
    std::mutex error_mutex;
    std::exception_ptr first_error;

    auto worker = [&]() {
      try {
        while (!stop.load(std::memory_order_relaxed)) {
          const int game = next_game.fetch_add(1, std::memory_order_relaxed);
          if (game >= FLAGS_games) break;

          const bool champion_is_seat_a = (game % 2) == 0;
          SelfPlayConfig config;
          config.simulations = FLAGS_simulations;
          config.batch_size = kSearchBatchSize;
          config.c_puct = kCPuct;
          config.seed = base_seed + static_cast<std::uint64_t>(game);
          config.keep_last_moves = 0;
          config.dirichlet_noise = DirichletNoiseConfig{
              kDirichletAlpha, kDirichletEpsilon, config.seed};
          config.dirichlet_noise_plies = 4;
          config.sample_actions = false;
          const EvaluatorSelector selector =
              [&](const Board& board) -> Evaluator* {
            const bool seat_a_to_move = board.current_player() == Seat::kA;
            const bool champion_to_move =
                champion_is_seat_a ? seat_a_to_move : !seat_a_to_move;
            return champion_to_move
                       ? static_cast<Evaluator*>(&champion_evaluator)
                       : static_cast<Evaluator*>(&challenger_evaluator);
          };

          const GameResult result =
              PlayGame(config, selector, solver, defensive_solver);
          const bool challenger_is_seat_a = !champion_is_seat_a;
          const bool winner_is_seat_a = result.result == Result::kPlayerAWin;
          const bool winner_is_seat_b = result.result == Result::kPlayerBWin;

          MatchOutcome outcome;
          outcome.challenger_won = (challenger_is_seat_a && winner_is_seat_a) ||
                                   (!challenger_is_seat_a && winner_is_seat_b);
          outcome.champion_won = (champion_is_seat_a && winner_is_seat_a) ||
                                 (!champion_is_seat_a && winner_is_seat_b);
          outcome.draw = result.result == Result::kDraw ||
                         result.result == Result::kUndetermined;
          outcome.action_count = result.action_count;
          outcomes[game] = outcome;

          const int completed =
              completed_games.fetch_add(1, std::memory_order_relaxed) + 1;
          if (completed % 10 == 0 || completed == FLAGS_games) {
            std::lock_guard<std::mutex> lock(progress_mutex);
            LOG(INFO) << "Evaluated " << completed << "/" << FLAGS_games
                      << " games";
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

    const int worker_count = std::min(kWorkerCount, FLAGS_games);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (std::thread& thread : workers) thread.join();
    if (first_error) std::rethrow_exception(first_error);

    const Summary summary = CalculateSummary(outcomes);
    WriteSummary(output_file, summary);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "Challenger win rate: "
       << (summary.challenger_win_rate * 100.0) << "% (90% CI "
       << (summary.confidence_low * 100.0) << "%-"
       << (summary.confidence_high * 100.0) << "%)\n"
       << "Game length: mean " << summary.mean_length << ", median "
       << summary.median_length << ", std " << summary.std_length
       << ", min " << summary.min_length << ", max "
       << summary.max_length << "\n"
       << "Wrote evaluation results: \"" << output_file.string() << "\"";
    LOG(INFO) << ss.str();
    return 0;
  } catch (const std::exception& error) {
    LOG(ERROR) << "gomoku_model_evaluator: " << error.what();
    return 2;
  }
}
