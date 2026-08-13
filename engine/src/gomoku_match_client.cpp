#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "match_client.h"

namespace {

void PrintUsage(const char* program) {
  std::cerr
      << "Usage: " << program
      << " --name NAME [--host HOST] [--port PORT] [--model PATH]"
         " [--opponent NAME --seat A|B --admin-token TOKEN]"
         " [--auth-token TOKEN]"
         " [--simulations N] [--batch-size N] [--deadline-ms N]"
         " [--noise-plies N]"
         " [--disable-endgame-solver]\n";
}

int ParsePositiveInt(const std::string& value, const char* option) {
  std::size_t consumed = 0;
  int parsed = 0;
  try {
    parsed = std::stoi(value, &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(option) + " requires an integer");
  }
  if (consumed != value.size() || parsed <= 0) {
    throw std::invalid_argument(std::string(option) +
                                " requires a positive integer");
  }
  return parsed;
}

MatchClientOptions ParseArguments(int argc, char** argv) {
  MatchClientOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help" || option == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    }
    if (option == "--disable-endgame-solver") {
      options.disable_endgame_solver = true;
      continue;
    }
    if (i + 1 >= argc) throw std::invalid_argument(option + " requires a value");
    const std::string value = argv[++i];
    if (option == "--host") {
      options.host = value;
    } else if (option == "--port") {
      options.port = ParsePositiveInt(value, "--port");
    } else if (option == "--name") {
      options.name = value;
    } else if (option == "--opponent") {
      options.opponent = value;
    } else if (option == "--seat") {
      options.seat = value;
    } else if (option == "--auth-token") {
      options.auth_token = value;
    } else if (option == "--admin-token") {
      options.admin_token = value;
    } else if (option == "--model") {
      options.model_path = value;
    } else if (option == "--simulations") {
      options.simulation_limit = ParsePositiveInt(value, "--simulations");
    } else if (option == "--batch-size") {
      options.batch_size = ParsePositiveInt(value, "--batch-size");
    } else if (option == "--deadline-ms") {
      options.deadline_ms_per_move = ParsePositiveInt(value, "--deadline-ms");
    } else if (option == "--noise-plies") {
      options.noise_plies = ParsePositiveInt(value, "--noise-plies");
    } else {
      throw std::invalid_argument("unknown option: " + option);
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return RunMatchClient(ParseArguments(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "gomoku_match_client: " << error.what() << "\n";
    PrintUsage(argv[0]);
    return 2;
  }
}
