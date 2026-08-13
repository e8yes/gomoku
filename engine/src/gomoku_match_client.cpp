#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "match_client.h"

DEFINE_string(name, "", "Player name for protocol registration (required)");
DEFINE_string(host, "127.0.0.1", "Match server host IP or hostname");
DEFINE_int32(port, 7901, "Match server TCP port");
DEFINE_string(opponent, "", "Opponent player name to pair against");
DEFINE_string(seat, "", "Requested seat (A or B)");
DEFINE_string(auth_token, "", "Authentication token");
DEFINE_string(admin_token, "", "Admin token for match pairing");
DEFINE_string(model, "",
              "Path to AOTInductor model package (.pt2); omits for RandomEvaluator");
DEFINE_int32(simulations, 0,
             "Fixed simulation count search limit (0 to use deadline)");
DEFINE_int32(batch_size, 32, "MCTS search batch size");
DEFINE_int32(deadline_ms, 5000, "Move deadline in milliseconds");
DEFINE_int32(noise_plies, 4, "Root noise plies limit");
DEFINE_bool(disable_endgame_solver, false, "Disable VCF endgame solver");

namespace {

MatchClientOptions OptionsFromFlags() {
  if (FLAGS_name.empty()) {
    LOG(FATAL) << "--name is required";
  }
  if (FLAGS_port <= 0 || FLAGS_port > 65535) {
    LOG(FATAL) << "--port must be between 1 and 65535";
  }
  if (FLAGS_batch_size <= 0) {
    LOG(FATAL) << "--batch_size must be positive";
  }
  if (FLAGS_deadline_ms <= 0) {
    LOG(FATAL) << "--deadline_ms must be positive";
  }
  if (!FLAGS_seat.empty() && FLAGS_seat != "A" && FLAGS_seat != "B") {
    LOG(FATAL) << "--seat must be A or B";
  }
  if (!FLAGS_opponent.empty() && FLAGS_admin_token.empty()) {
    LOG(FATAL) << "--admin_token is required when --opponent is supplied";
  }

  MatchClientOptions options;
  options.host = FLAGS_host;
  options.port = FLAGS_port;
  options.name = FLAGS_name;
  options.opponent = FLAGS_opponent;
  options.seat = FLAGS_seat;
  options.auth_token = FLAGS_auth_token;
  options.admin_token = FLAGS_admin_token;
  options.model_path = FLAGS_model;
  if (FLAGS_simulations > 0) {
    options.simulation_limit = FLAGS_simulations;
  }
  options.batch_size = FLAGS_batch_size;
  options.deadline_ms_per_move = FLAGS_deadline_ms;
  options.noise_plies = FLAGS_noise_plies;
  options.disable_endgame_solver = FLAGS_disable_endgame_solver;
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Gomoku TCP match client.\n"
      "Usage: gomoku_match_client --name NAME [--host HOST] [--port PORT] "
      "[--model PATH] [--opponent NAME --seat A|B --admin_token TOKEN] "
      "[--auth_token TOKEN] [--simulations N] [--batch_size N] "
      "[--deadline_ms N] [--noise_plies N] [--disable_endgame_solver]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  try {
    return RunMatchClient(OptionsFromFlags());
  } catch (const std::exception& error) {
    LOG(ERROR) << "gomoku_match_client: " << error.what();
    return 2;
  }
}
