#include "match_client.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <glog/logging.h>
#include <torch/cuda.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <nlohmann/json.hpp>

#include "immediate_inference_executor.h"
#include "mcts.h"
#include "neural_net_evaluator.h"
#include "random_evaluator.h"
#include "self_play.h"
#include "vcf_solver.h"

namespace {

using JsonValue = nlohmann::json;

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
using SocketLength = int;

std::string SocketError(const char* operation) {
  return std::string(operation) + " failed with Winsock error " +
         std::to_string(WSAGetLastError());
}

void CloseSocket(SocketHandle socket) { closesocket(socket); }
void ShutdownSocket(SocketHandle socket) { shutdown(socket, SD_BOTH); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
using SocketLength = socklen_t;

std::string SocketError(const char* operation) {
  return std::string(operation) + " failed: " + std::strerror(errno);
}

void CloseSocket(SocketHandle socket) { close(socket); }
void ShutdownSocket(SocketHandle socket) { shutdown(socket, SHUT_RDWR); }
#endif

class TcpConnection {
 public:
  TcpConnection(const std::string& host, int port) {
#ifdef _WIN32
    WSAData data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
    winsock_started_ = true;
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    const int result =
        getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
    if (result != 0) {
#ifdef _WIN32
      const std::string message =
          "getaddrinfo failed with error " + std::to_string(result);
      CleanupWinsock();
#else
      const std::string message =
          std::string("getaddrinfo failed: ") + gai_strerror(result);
#endif
      throw std::runtime_error(message);
    }

    for (addrinfo* address = addresses; address != nullptr;
         address = address->ai_next) {
      socket_ = static_cast<SocketHandle>(::socket(
          address->ai_family, address->ai_socktype, address->ai_protocol));
      if (socket_ == kInvalidSocket) continue;
      if (::connect(socket_, address->ai_addr,
                    static_cast<SocketLength>(address->ai_addrlen)) == 0) {
        break;
      }
      CloseSocket(socket_);
      socket_ = kInvalidSocket;
    }
    freeaddrinfo(addresses);

    if (socket_ == kInvalidSocket) {
      CleanupWinsock();
      throw std::runtime_error(SocketError("connect"));
    }

    int enabled = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&enabled), sizeof(enabled));
  }

  ~TcpConnection() {
    Close();
    CleanupWinsock();
  }

  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  void SendLine(const std::string& line) {
    const std::string wire = line + "\n";
    std::lock_guard<std::mutex> lock(send_mutex_);
    std::size_t sent = 0;
    while (sent < wire.size()) {
      const auto chunk = static_cast<int>(std::min<std::size_t>(
          wire.size() - sent,
          static_cast<std::size_t>(std::numeric_limits<int>::max())));
      const int count = ::send(socket_, wire.data() + sent, chunk, 0);
      if (count <= 0) throw std::runtime_error(SocketError("send"));
      sent += static_cast<std::size_t>(count);
    }
  }

  // Returns false on a clean peer close. Throws on a socket error.
  bool ReadLine(std::string* line) {
    while (true) {
      const std::size_t newline = read_buffer_.find('\n');
      if (newline != std::string::npos) {
        *line = read_buffer_.substr(0, newline);
        read_buffer_.erase(0, newline + 1);
        if (!line->empty() && line->back() == '\r') line->pop_back();
        return true;
      }
      if (read_buffer_.size() > 4 * 1024 * 1024) {
        throw std::runtime_error("JSON-RPC line exceeded 4 MiB");
      }

      char buffer[8192];
      const int count = ::recv(socket_, buffer, sizeof(buffer), 0);
      if (count == 0) return false;
      if (count < 0) throw std::runtime_error(SocketError("recv"));
      read_buffer_.append(buffer, static_cast<std::size_t>(count));
    }
  }

  void Close() {
    if (socket_ != kInvalidSocket) {
      ShutdownSocket(socket_);
      CloseSocket(socket_);
      socket_ = kInvalidSocket;
    }
  }

 private:
  void CleanupWinsock() {
#ifdef _WIN32
    if (winsock_started_) {
      WSACleanup();
      winsock_started_ = false;
    }
#endif
  }

  SocketHandle socket_ = kInvalidSocket;
  std::string read_buffer_;
  std::mutex send_mutex_;
#ifdef _WIN32
  bool winsock_started_ = false;
#endif
};

class RpcError : public std::runtime_error {
 public:
  explicit RpcError(const std::string& message) : std::runtime_error(message) {}
};

class RpcClient {
 public:
  RpcClient(const std::string& host, int port) : connection_(host, port) {
    reader_ = std::thread(&RpcClient::ReaderLoop, this);
  }

  ~RpcClient() { Close(); }

  RpcClient(const RpcClient&) = delete;
  RpcClient& operator=(const RpcClient&) = delete;

  JsonValue Call(
      const std::string& method, JsonValue params,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const std::string id = "cpp-" + std::to_string(next_id_++);
    JsonValue request = JsonValue::object();
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = method;
    request["params"] = std::move(params);
    try {
      connection_.SendLine(request.dump());
    } catch (const std::exception& error) {
      Close();
      throw RpcError(error.what());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    const bool received = response_condition_.wait_for(lock, timeout, [&] {
      return responses_.find(id) != responses_.end() || closed_;
    });
    if (!received || responses_.find(id) == responses_.end()) {
      if (!reader_error_.empty()) {
        throw RpcError(method + " failed: " + reader_error_);
      }
      throw RpcError(method + " timed out or transport closed");
    }
    JsonValue response = std::move(responses_.at(id));
    responses_.erase(id);
    lock.unlock();

    if (response.contains("error") && !response["error"].is_null()) {
      const auto& error = response["error"];
      std::string code_name;
      if (error.contains("data") && error["data"].is_object()) {
        if (error["data"].contains("code_name") &&
            error["data"]["code_name"].is_string()) {
          code_name = error["data"]["code_name"].get<std::string>();
        }
      }
      const std::string message =
          error.contains("message") && error["message"].is_string()
              ? error["message"].get<std::string>()
              : "server returned an error";
      throw RpcError(method + " returned " +
                     (code_name.empty() ? "error" : code_name) + ": " +
                     message);
    }
    return response.at("result");
  }

  bool WaitForEvent(JsonValue* event, std::chrono::milliseconds timeout =
                                          std::chrono::milliseconds(1000)) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool available = event_condition_.wait_for(
        lock, timeout, [&] { return !events_.empty() || closed_; });
    if (!available || events_.empty()) return false;
    *event = std::move(events_.front());
    events_.pop_front();
    return true;
  }

  bool IsClosed() {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  void Close() {
    bool expected = false;
    if (!close_started_.compare_exchange_strong(expected, true)) return;
    connection_.Close();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    response_condition_.notify_all();
    event_condition_.notify_all();
    if (reader_.joinable()) reader_.join();
  }

 private:
  void ReaderLoop() {
    try {
      while (true) {
        std::string line;
        if (!connection_.ReadLine(&line)) break;
        if (line.empty()) continue;
        JsonValue message = JsonValue::parse(line);
        std::lock_guard<std::mutex> lock(mutex_);
        if (message.contains("id") && message["id"].is_string() &&
            (message.contains("result") || message.contains("error"))) {
          const std::string response_id = message["id"].get<std::string>();
          responses_[response_id] = std::move(message);
          response_condition_.notify_all();
        } else if (message.contains("method")) {
          events_.push_back(std::move(message));
          event_condition_.notify_all();
        }
      }
    } catch (const std::exception& error) {
      std::lock_guard<std::mutex> lock(mutex_);
      reader_error_ = error.what();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    response_condition_.notify_all();
    event_condition_.notify_all();
  }

  TcpConnection connection_;
  std::thread reader_;
  std::mutex mutex_;
  std::condition_variable response_condition_;
  std::condition_variable event_condition_;
  std::unordered_map<std::string, JsonValue> responses_;
  std::deque<JsonValue> events_;
  std::string reader_error_;
  std::uint64_t next_id_ = 1;
  bool closed_ = false;
  std::atomic<bool> close_started_{false};
};

JsonValue Params(
    std::initializer_list<std::pair<const std::string, JsonValue>> values) {
  JsonValue object = JsonValue::object();
  for (const auto& [key, val] : values) {
    object[key] = val;
  }
  return object;
}

std::string RequiredString(const JsonValue& object, const std::string& key) {
  if (!object.contains(key) || !object[key].is_string()) {
    throw std::runtime_error("match event is missing string field '" + key +
                             "'");
  }
  return object[key].get<std::string>();
}

std::int64_t RequiredInt(const JsonValue& object, const std::string& key) {
  if (!object.contains(key) || !object[key].is_number_integer()) {
    throw std::runtime_error("match state is missing integer field '" + key +
                             "'");
  }
  return object[key].get<std::int64_t>();
}

std::vector<int> ReadActions(const JsonValue& state) {
  if (!state.contains("moves") || !state["moves"].is_array()) {
    throw std::runtime_error("match state is missing the moves array");
  }
  std::vector<int> moves;
  for (const JsonValue& value : state["moves"]) {
    if (!value.is_number_integer()) {
      throw std::runtime_error("match state contains an invalid move format");
    }
    const std::int64_t action = value.get<std::int64_t>();
    if (action < 0 || action >= Board::kNumActions) {
      throw std::runtime_error("match state contains an invalid action id");
    }
    moves.push_back(static_cast<int>(action));
  }
  return moves;
}

Board ReconstructBoard(const std::vector<int>& actions) {
  Board board;
  for (int action : actions) {
    const std::vector<int> legal = board.GetLegalActions();
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
      throw std::runtime_error(
          "server state contains an illegal action history");
    }
    board.Apply(action);
  }
  return board;
}

int BestLegalAction(const Board& board, const std::vector<float>& policy) {
  int best = -1;
  float best_value = -1.0f;
  for (int action : board.GetLegalActions()) {
    if (action < static_cast<int>(policy.size()) &&
        policy[action] > best_value) {
      best = action;
      best_value = policy[action];
    }
  }
  return best;
}

class MatchGame {
 public:
  MatchGame(const MatchClientOptions& options, Evaluator* evaluator)
      : options_(options), evaluator_(evaluator) {
    if (options_.noise_plies > 0) {
      self_play_config_.dirichlet_noise = DirichletNoiseConfig{
          0.3f, 0.25f, static_cast<std::uint64_t>(std::random_device{}())};
      self_play_config_.dirichlet_noise_plies = options_.noise_plies;
    }
    self_play_config_.batch_size = options_.batch_size;
    self_play_config_.sample_actions = false;
    self_play_config_.keep_last_moves = 0;
    mcts_ = std::make_unique<MCTS>(self_play_config_.batch_size,
                                   self_play_config_.c_puct,
                                   self_play_config_.dirichlet_noise);
  }

  void Start(const JsonValue& params) {
    game_id_ = RequiredString(params, "game_id");
    if (!params.contains("players") || !params["players"].is_object()) {
      throw std::runtime_error("game_started is missing players");
    }
    const auto& players = params["players"];
    const std::string player_a = RequiredString(players, "A");
    const std::string player_b = RequiredString(players, "B");
    if (player_a == options_.name) {
      seat_ = "A";
    } else if (player_b == options_.name) {
      seat_ = "B";
    } else {
      throw std::runtime_error("game_started does not contain this client");
    }
    synced_actions_.clear();
    mcts_->Reset();
    LOG(INFO) << "game_started id=" << game_id_ << " seat=" << seat_
              << " opponent=" << (seat_ == "A" ? player_b : player_a);
  }

  bool IsFor(const JsonValue& params) const {
    return params.contains("game_id") && params["game_id"].is_string() &&
           params["game_id"].get<std::string>() == game_id_;
  }

  int HandleTurn(RpcClient* rpc, const JsonValue& params, EndgameSolver solver,
                 EndgameDefenseSolver defensive_solver) {
    if (!IsFor(params)) return 0;
    if (!params.contains("state") || !params["state"].is_object()) {
      throw std::runtime_error("your_turn is missing state");
    }
    const auto& state = params["state"];
    const std::vector<int> actions = ReadActions(state);
    Board board = ReconstructBoard(actions);
    if (RequiredString(state, "current_player") != seat_) {
      throw std::runtime_error("server sent your_turn to the wrong seat");
    }

    if (synced_actions_.size() > actions.size() ||
        !std::equal(synced_actions_.begin(), synced_actions_.end(),
                    actions.begin())) {
      mcts_->Reset();
      synced_actions_.clear();
    }
    for (std::size_t i = synced_actions_.size(); i < actions.size(); ++i) {
      mcts_->SelectAction(actions[i]);
    }

    const int deadline_ms = params.contains("deadline_ms") &&
                                    params["deadline_ms"].is_number_integer()
                                ? params["deadline_ms"].get<int>()
                                : 5000;

    // Stop search before the referee deadline so the final move submission
    // has a predictable transport margin. MCTS checks this deadline between
    // simulation batches; an in-flight evaluator batch is allowed to finish.
    constexpr int kMoveSubmissionReserveMs = 250;
    const SearchStoppingCriteria stopping_criteria =
        options_.simulation_limit.has_value()
            ? SearchStoppingCriteria{options_.simulation_limit.value()}
            : SearchStoppingCriteria{std::chrono::milliseconds(
                  std::max(0, deadline_ms - kMoveSubmissionReserveMs))};
    const std::vector<float> policy = mcts_->Search(
        board, evaluator_, stopping_criteria, solver, defensive_solver);
    const int action = BestLegalAction(board, policy);
    if (action < 0) throw std::runtime_error("MCTS returned no legal action");

    // Advance the cached root immediately. The server's next your_turn state
    // will contain the opponent's move, which is then selected as well.
    mcts_->SelectAction(action);
    synced_actions_ = actions;
    synced_actions_.push_back(action);
    if (options_.noise_plies > 0 &&
        static_cast<int>(synced_actions_.size()) >= options_.noise_plies) {
      mcts_->SetDirichletNoise(std::nullopt);
    }

    LOG(INFO) << "move game=" << game_id_ << " ply=" << actions.size()
              << " action=" << action << " label=" << Action(action).ToString()
              << " deadline_ms=" << deadline_ms;
    rpc->Call("submit_move",
              Params({{"game_id", game_id_}, {"action", action}}),
              std::chrono::milliseconds(std::max(1000, deadline_ms)));
    return 1;
  }

 private:
  const MatchClientOptions& options_;
  Evaluator* evaluator_;
  SelfPlayConfig self_play_config_;
  std::unique_ptr<MCTS> mcts_;
  std::string game_id_;
  std::string seat_;
  std::vector<int> synced_actions_;
};

}  // namespace

int RunMatchClient(const MatchClientOptions& options) {
  if (options.name.empty()) throw std::invalid_argument("--name is required");
  if (options.port <= 0 || options.port > 65535) {
    throw std::invalid_argument("--port must be between 1 and 65535");
  }
  if (options.batch_size <= 0) {
    throw std::invalid_argument("batch size must be positive");
  }
  if (options.deadline_ms_per_move <= 0) {
    throw std::invalid_argument("--deadline-ms must be positive");
  }
  if (options.seat != "A" && options.seat != "B") {
    throw std::invalid_argument("--seat must be A or B");
  }
  if (!options.opponent.empty() && options.admin_token.empty()) {
    throw std::invalid_argument(
        "--admin-token is required when --opponent is supplied");
  }

  std::shared_ptr<ImmediateInferenceExecutor> inference_executor;
  std::unique_ptr<NeuralNetEvaluator> neural_evaluator;
  std::unique_ptr<RandomEvaluator> random_evaluator;
  Evaluator* evaluator = nullptr;
  if (!options.model_path.empty()) {
    if (!std::filesystem::exists(options.model_path)) {
      throw std::invalid_argument("model does not exist: " +
                                  options.model_path.string());
    }
    if (!torch::cuda::is_available()) {
      throw std::runtime_error("CUDA is required when --model is supplied");
    }
    inference_executor = std::make_shared<ImmediateInferenceExecutor>(
        options.model_path, torch::Device(torch::kCUDA));
    neural_evaluator = std::make_unique<NeuralNetEvaluator>(inference_executor);
    evaluator = neural_evaluator.get();
    LOG(INFO) << "using model " << options.model_path.string()
              << " (ImmediateInferenceExecutor)";
  } else {
    random_evaluator = std::make_unique<RandomEvaluator>();
    evaluator = random_evaluator.get();
    LOG(INFO) << "using RandomEvaluator (protocol smoke-test mode)";
  }

  RpcClient rpc(options.host, options.port);
  JsonValue handshake = Params({
      {"protocol_version", "2.0"},
      {"client_name", options.name},
  });
  if (!options.auth_token.empty()) {
    handshake["auth_token"] = options.auth_token;
  }
  if (!options.admin_token.empty()) {
    handshake["admin_token"] = options.admin_token;
  }
  const JsonValue hello = rpc.Call("handshake", std::move(handshake));
  if (hello.at("protocol_version").get<std::string>() != "2.0") {
    throw std::runtime_error("server did not negotiate JSON-RPC protocol 2.0");
  }
  rpc.Call("register", Params({{"name", options.name}}));
  LOG(INFO) << "registered name=" << options.name;

  if (!options.opponent.empty()) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(options.match_timeout_seconds);
    std::string game_id;
    while (game_id.empty()) {
      try {
        game_id =
            rpc.Call(
                   "create_match",
                   Params({{"player_a", options.seat == "A" ? options.name
                                                            : options.opponent},
                           {"player_b", options.seat == "A" ? options.opponent
                                                            : options.name},
                           {"board_size", Board::kSize},
                           {"deadline_ms_per_move",
                            options.deadline_ms_per_move}}),
                   std::chrono::milliseconds(5000))
                .at("game_id")
                .get<std::string>();
      } catch (const RpcError& error) {
        if (std::chrono::steady_clock::now() >= deadline) throw;
        const std::string message = error.what();
        if (message.find("unknown_player") == std::string::npos) throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
    }
    LOG(INFO) << "created match id=" << game_id;
  }

  EndgameSolver solver;
  EndgameDefenseSolver defensive_solver;
  if (!options.disable_endgame_solver) {
    solver = [](const Board& board) { return SolveVCF(board); };
    defensive_solver = [](const Board& board) {
      return AnalyzeVCFDefense(board);
    };
  }

  std::unique_ptr<MatchGame> game;
  while (true) {
    JsonValue event;
    if (!rpc.WaitForEvent(&event, std::chrono::seconds(1))) {
      if (rpc.IsClosed()) {
        throw std::runtime_error(
            "match connection closed before game_finished");
      }
      continue;
    }
    const std::string method = event.at("method").get<std::string>();
    const auto& params = event.at("params");
    if (method == "game_started") {
      game = std::make_unique<MatchGame>(options, evaluator);
      game->Start(params);
    } else if (method == "your_turn") {
      if (game == nullptr)
        throw std::runtime_error("your_turn before game_started");
      game->HandleTurn(&rpc, params, solver, defensive_solver);
    } else if (method == "game_finished") {
      const std::string result = RequiredString(params, "result");
      const std::string reason = RequiredString(params, "reason");
      LOG(INFO) << "game_finished id=" << RequiredString(params, "game_id")
                << " result=" << result << " reason=" << reason;
      return 0;
    }
  }
}
