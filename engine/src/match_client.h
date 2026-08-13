#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct MatchClientOptions {
  std::string host = "127.0.0.1";
  int port = 7901;
  std::string name;
  std::string opponent;
  std::string seat = "A";
  std::string auth_token;
  std::string admin_token;
  std::filesystem::path model_path;
  // When set, use a deterministic simulation-count search. The default is
  // unset so protocol matches use the server-provided deadline duration.
  std::optional<int> simulation_limit;
  int batch_size = 32;
  int deadline_ms_per_move = 5000;
  int noise_plies = 0;
  bool disable_endgame_solver = false;
  int match_timeout_seconds = 60;
};

// Runs one server match. If opponent is supplied, admin_token is used to
// create a match between name and opponent after both players register.
// Without a model path the client uses RandomEvaluator, which is useful for
// protocol smoke tests; production play should supply an exported .pt2 model.
int RunMatchClient(const MatchClientOptions& options);
