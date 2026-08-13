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

namespace {

constexpr int kWorkerCount = 12;
constexpr int kSimulationsPerMove = 1000;
constexpr int kSearchBatchSize = 32;
constexpr int kInferenceBatchRequests = 6;
constexpr int kInferenceWaitMicroseconds = 500;
constexpr float kCPuct = 1.0f;
constexpr float kDirichletAlpha = 0.3f;
constexpr float kDirichletEpsilon = 0.25f;
constexpr double kConfidenceZ90 = 1.6448536269514722;

struct Arguments {
  int games = 0;
  std::filesystem::path champion_model_path;
  std::filesystem::path challenger_model_path;
  std::filesystem::path out_dir;
  // Kept as a compatibility alias for the pre-Phase-7 curriculum script.
  std::filesystem::path out_file;
};

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

void PrintUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " --games N --champion_model_path PATH"
               " --challenger_model_path PATH --out_dir PATH\n";
}

int ParseInt(const std::string& text, const char* option) {
  std::size_t consumed = 0;
  long long value = 0;
  try {
    value = std::stoll(text, &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(option) + " requires an integer");
  }
  if (consumed != text.size() || value <= 0 ||
      value > std::numeric_limits<int>::max()) {
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
    if (option == "--games") {
      arguments.games = ParseInt(value, "--games");
    } else if (option == "--champion_model_path") {
      arguments.champion_model_path = value;
    } else if (option == "--challenger_model_path") {
      arguments.challenger_model_path = value;
    } else if (option == "--out_dir") {
      arguments.out_dir = value;
    } else if (option == "--out_file") {
      arguments.out_file = value;
    } else {
      throw std::invalid_argument("Unknown option: " + option);
    }
  }

  if (arguments.games <= 0) {
    throw std::invalid_argument("--games is required and must be positive");
  }
  if (arguments.champion_model_path.empty()) {
    throw std::invalid_argument("--champion_model_path is required");
  }
  if (arguments.challenger_model_path.empty()) {
    throw std::invalid_argument("--challenger_model_path is required");
  }
  if (!arguments.out_dir.empty() && !arguments.out_file.empty()) {
    throw std::invalid_argument("Specify only one of --out_dir or --out_file");
  }
  if (arguments.out_dir.empty() && arguments.out_file.empty()) {
    throw std::invalid_argument("--out_dir is required");
  }
  return arguments;
}

std::uint64_t MakeSeed() {
  std::random_device random_device;
  return (static_cast<std::uint64_t>(random_device()) << 32) ^
         static_cast<std::uint64_t>(random_device());
}

std::filesystem::path ResolveOutputFile(const Arguments& arguments) {
  if (!arguments.out_file.empty()) return arguments.out_file;
  return arguments.out_dir / "evaluation.json";
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
    throw std::runtime_error("Unable to open evaluation output: " +
                             output_file.string());
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
    throw std::runtime_error("Failed while writing evaluation output: " +
                             output_file.string());
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    if (!std::filesystem::exists(arguments.champion_model_path)) {
      throw std::invalid_argument("Champion model does not exist: " +
                                  arguments.champion_model_path.string());
    }
    if (!std::filesystem::exists(arguments.challenger_model_path)) {
      throw std::invalid_argument("Challenger model does not exist: " +
                                  arguments.challenger_model_path.string());
    }
    if (!torch::cuda::is_available()) {
      throw std::runtime_error("CUDA is required for model evaluation");
    }

    const std::filesystem::path output_file = ResolveOutputFile(arguments);
    if (!arguments.out_dir.empty()) {
      std::filesystem::create_directories(arguments.out_dir);
    }

    // Each model gets one shared inference executor. Six queued 32-item
    // MCTS requests form a maximum 192-board GPU batch for each model.
    auto champion_executor = std::make_shared<BatchInferenceExecutor>(
        arguments.champion_model_path, torch::Device(torch::kCUDA),
        kInferenceBatchRequests,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    auto challenger_executor = std::make_shared<BatchInferenceExecutor>(
        arguments.challenger_model_path, torch::Device(torch::kCUDA),
        kInferenceBatchRequests,
        std::chrono::microseconds(kInferenceWaitMicroseconds));
    NeuralNetEvaluator champion_evaluator(champion_executor);
    NeuralNetEvaluator challenger_evaluator(challenger_executor);

    const std::uint64_t base_seed = MakeSeed();
    std::cout
        << "Evaluating " << arguments.games << " games with " << kWorkerCount
        << " workers, " << kSimulationsPerMove
        << " simulations/move; root noise enabled for the first 4 plies\n";

    EndgameSolver solver = [](const Board& board) { return SolveVCF(board); };

    std::vector<MatchOutcome> outcomes(arguments.games);
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
          if (game >= arguments.games) break;

          const bool champion_is_seat_a = (game % 2) == 0;
          Config config;
          config.simulations = kSimulationsPerMove;
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

          const GameResult result = PlayGame(config, selector, solver);
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
          if (completed % 10 == 0 || completed == arguments.games) {
            std::lock_guard<std::mutex> lock(progress_mutex);
            std::cout << "Evaluated " << completed << "/" << arguments.games
                      << " games\n";
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

    const Summary summary = CalculateSummary(outcomes);
    WriteSummary(output_file, summary);
    std::cout << std::fixed << std::setprecision(2) << "Challenger win rate: "
              << (summary.challenger_win_rate * 100.0) << "% (90% CI "
              << (summary.confidence_low * 100.0) << "%-"
              << (summary.confidence_high * 100.0) << "%)\n"
              << "Game length: mean " << summary.mean_length << ", median "
              << summary.median_length << ", std " << summary.std_length
              << ", min " << summary.min_length << ", max "
              << summary.max_length << "\n"
              << "Wrote evaluation results: " << output_file << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gomoku_model_evaluator: " << error.what() << "\n";
    PrintUsage(argv[0]);
    return 2;
  }
}
