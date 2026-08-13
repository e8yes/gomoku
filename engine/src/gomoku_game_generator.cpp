#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "game_data.h"
#include "seed_game.h"

namespace {

struct Arguments {
  int games = 0;
  int iteration = -1;
  std::filesystem::path out_dir;
  std::string champion_model_path;
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
  if (arguments.iteration < 0) {
    throw std::invalid_argument("--iteration must be non-negative");
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

}  // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    std::filesystem::create_directories(arguments.out_dir);

    if (!arguments.champion_model_path.empty()) {
      std::cerr << "Warning: --champion_model_path is reserved for Phase 6; "
                   "Phase 5 uses RandomEvaluator.\n";
    }

    const std::filesystem::path shard_path = ShardPath(arguments);
    std::ofstream output(shard_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Unable to open output shard: " +
                               shard_path.string());
    }

    std::random_device random_device;
    const std::uint64_t base_seed =
        (static_cast<std::uint64_t>(random_device()) << 32) ^ random_device();

    Config config;
    for (int game = 0; game < arguments.games; ++game) {
      config.seed = base_seed + static_cast<std::uint64_t>(game);
      const std::vector<TrainingExample> examples = GenerateGame(config);
      for (const auto& example : examples) {
        if (!WriteExample(output, example.board, example.policy,
                          example.value)) {
          throw std::runtime_error("Failed while writing output shard: " +
                                   shard_path.string());
        }
      }
      if ((game + 1) % 100 == 0 || game + 1 == arguments.games) {
        std::cout << "Generated " << (game + 1) << "/" << arguments.games
                  << " games\n";
      }
    }

    output.close();
    std::cout << "Wrote seed shard: " << shard_path << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gomoku_game_generator: " << error.what() << "\n";
    PrintUsage(argv[0]);
    return 2;
  }
}
