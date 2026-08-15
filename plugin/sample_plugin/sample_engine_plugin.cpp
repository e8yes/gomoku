#define GOMOKU_PLUGIN_EXPORTS
#include "plugin/plugin_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "plugin/gomoku_types.h"

namespace {

using namespace gomoku::plugin;

class SampleEngine {
 public:
  SampleEngine() {
    win_rate_.store(0.5f, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(policy_mutex_);
    policy_.assign(kNumTotalActions, 0.0f);
  }

  void OnMatchStart(const GomokuMatchSettings* settings) {
    seat_ = static_cast<Seat>(settings->seat);
    difficulty_ = settings->difficulty;
    cancelled_.store(false, std::memory_order_relaxed);
    win_rate_.store(0.5f, std::memory_order_relaxed);
  }

  int InquireAction(const uint8_t* board_cells, int current_seat,
                    int stone_to_place, int phase) {
    cancelled_.store(false, std::memory_order_relaxed);

    BoardState board;
    for (size_t i = 0; i < kNumBoardCells; ++i) {
      board.cells[i] = static_cast<Stone>(board_cells[i]);
    }
    board.current_seat = static_cast<Seat>(current_seat);
    board.stone_to_place = static_cast<Stone>(stone_to_place);
    board.phase = static_cast<GamePhase>(phase);

    std::vector<int> legal_actions = board.GetLegalActions();
    if (legal_actions.empty()) return -1;

    // Opening Swap2 actions
    if (board.phase == GamePhase::kSwap2Decision) {
      UpdateTelemetry(0.52f, {Swap2Action::kChooseWhite, Swap2Action::kChooseBlack,
                              Swap2Action::kPlaceTwo});
      return Swap2Action::kChooseWhite;
    }
    if (board.phase == GamePhase::kChooseColor) {
      UpdateTelemetry(0.51f, {Swap2Action::kChooseWhiteAfterTwo,
                              Swap2Action::kChooseBlackAfterTwo});
      return Swap2Action::kChooseWhiteAfterTwo;
    }

    // Determine noise based on Difficulty
    float noise_weight = 0.0f;
    switch (difficulty_) {
      case GOMOKU_DIFF_APPRENTICE:
        noise_weight = 0.6f;
        break;
      case GOMOKU_DIFF_CASUAL:
        noise_weight = 0.25f;
        break;
      case GOMOKU_DIFF_CLUB:
        noise_weight = 0.1f;
        break;
      case GOMOKU_DIFF_VETERAN:
        noise_weight = 0.02f;
        break;
      case GOMOKU_DIFF_CHAMPION:
        noise_weight = 0.0f;
        break;
      case GOMOKU_DIFF_TRUTH:
        noise_weight = 0.0f;
        break;
    }

    // Evaluate actions
    std::vector<float> scores(legal_actions.size(), 0.0f);
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float best_score = -1e9f;
    int best_action = legal_actions[0];

    // Priority: immediate winning move or blocking opponent immediate win
    Stone my_stone = board.stone_to_place;
    Stone opp_stone = OtherStone(my_stone);

    for (size_t i = 0; i < legal_actions.size(); ++i) {
      if (cancelled_.load(std::memory_order_relaxed)) {
        return best_action;
      }

      int action = legal_actions[i];
      int x = BoardState::ActionX(action);
      int y = BoardState::ActionY(action);

      // Positional score (center bonus)
      float center_dist = std::abs(x - 7) + std::abs(y - 7);
      float score = 100.0f - center_dist;

      // Check tactical patterns
      board.SetCell(x, y, my_stone);
      if (CheckWin(board, x, y, my_stone)) {
        score += 100000.0f;  // Instant win
      }
      board.SetCell(x, y, opp_stone);
      if (CheckWin(board, x, y, opp_stone)) {
        score += 50000.0f;   // Block opponent win
      }
      board.SetCell(x, y, Stone::kEmpty);

      // Add noise according to difficulty
      score += noise_weight * dist(rng) * 200.0f;
      scores[i] = score;

      if (score > best_score) {
        best_score = score;
        best_action = action;
      }

      // Update in-flight telemetry during search iterations
      float current_wr = 0.5f + std::clamp(best_score / 200000.0f, -0.49f, 0.49f);
      UpdateTelemetryFromScores(current_wr, legal_actions, scores);
    }

    return best_action;
  }

  void ApplyAction(int action_id) {
    // Retain internal state / tracking
    last_applied_action_ = action_id;
  }

  void OnMatchEnd(const GomokuMatchResult* /*result*/) {
    cancelled_.store(false, std::memory_order_relaxed);
  }

  bool GetWinRate(float* out_win_rate) const {
    if (!out_win_rate) return false;
    *out_win_rate = win_rate_.load(std::memory_order_relaxed);
    return true;
  }

  int GetPolicy(float* out_policy, int max_actions) const {
    if (!out_policy || max_actions <= 0) return 0;
    std::lock_guard<std::mutex> lock(policy_mutex_);
    int count = std::min(max_actions, static_cast<int>(policy_.size()));
    std::memcpy(out_policy, policy_.data(), count * sizeof(float));
    return count;
  }

  void CancelInquiry() {
    cancelled_.store(true, std::memory_order_relaxed);
  }

 private:
  bool CheckWin(const BoardState& board, int x, int y, Stone s) const {
    constexpr int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (const auto& d : dirs) {
      int count = 1;
      int nx = x + d[0], ny = y + d[1];
      while (nx >= 0 && nx < kBoardSize && ny >= 0 && ny < kBoardSize &&
             board.cell(nx, ny) == s) {
        ++count;
        nx += d[0];
        ny += d[1];
      }
      nx = x - d[0];
      ny = y - d[1];
      while (nx >= 0 && nx < kBoardSize && ny >= 0 && ny < kBoardSize &&
             board.cell(nx, ny) == s) {
        ++count;
        nx -= d[0];
        ny -= d[1];
      }
      if (count == 5) return true;
    }
    return false;
  }

  void UpdateTelemetry(float win_rate, const std::vector<int>& actions) {
    win_rate_.store(win_rate, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(policy_mutex_);
    policy_.assign(kNumTotalActions, 0.0f);
    if (!actions.empty()) {
      float prob = 1.0f / static_cast<float>(actions.size());
      for (int a : actions) {
        if (a >= 0 && a < kNumTotalActions) policy_[a] = prob;
      }
    }
  }

  void UpdateTelemetryFromScores(float win_rate,
                                 const std::vector<int>& legal_actions,
                                 const std::vector<float>& scores) {
    win_rate_.store(win_rate, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(policy_mutex_);
    policy_.assign(kNumTotalActions, 0.0f);

    float max_s = -1e9f;
    for (float s : scores) max_s = std::max(max_s, s);

    float sum_exp = 0.0f;
    std::vector<float> exps(scores.size(), 0.0f);
    for (size_t i = 0; i < scores.size(); ++i) {
      exps[i] = std::exp(std::clamp((scores[i] - max_s) / 100.0f, -20.0f, 0.0f));
      sum_exp += exps[i];
    }
    if (sum_exp > 0.0f) {
      for (size_t i = 0; i < legal_actions.size(); ++i) {
        int a = legal_actions[i];
        if (a >= 0 && a < kNumTotalActions) {
          policy_[a] = exps[i] / sum_exp;
        }
      }
    }
  }

  Seat seat_{Seat::kA};
  GomokuDifficulty difficulty_{GOMOKU_DIFF_VETERAN};
  std::atomic<bool> cancelled_{false};
  std::atomic<float> win_rate_{0.5f};
  mutable std::mutex policy_mutex_;
  std::vector<float> policy_;
  int last_applied_action_{-1};
};

}  // namespace

extern "C" {

GOMOKU_PLUGIN_API GomokuPluginInfo gomoku_plugin_get_info(void) {
  GomokuPluginInfo info{};
  info.api_version_major = GOMOKU_PLUGIN_API_VERSION_MAJOR;
  info.api_version_minor = GOMOKU_PLUGIN_API_VERSION_MINOR;
  info.plugin_name = "SampleGomokuEngine";
  info.plugin_version = "1.0.0";
  info.author = "Antigravity Team";
  info.description = "Reference Gomoku Engine Plugin with 6 difficulty levels";
  info.capabilities_flags = 0x03;  // Supports win rate & policy telemetry
  return info;
}

GOMOKU_PLUGIN_API GomokuPlayerHandle gomoku_player_create(const char* /*config_json*/) {
  return reinterpret_cast<GomokuPlayerHandle>(new SampleEngine());
}

GOMOKU_PLUGIN_API void gomoku_player_destroy(GomokuPlayerHandle handle) {
  delete reinterpret_cast<SampleEngine*>(handle);
}

GOMOKU_PLUGIN_API void gomoku_player_on_match_start(
    GomokuPlayerHandle handle, const GomokuMatchSettings* settings) {
  if (handle && settings) {
    reinterpret_cast<SampleEngine*>(handle)->OnMatchStart(settings);
  }
}

GOMOKU_PLUGIN_API int gomoku_player_inquire_action(
    GomokuPlayerHandle handle,
    const uint8_t* board_cells,
    int current_seat,
    int stone_to_place,
    int phase) {
  if (!handle || !board_cells) return -1;
  return reinterpret_cast<SampleEngine*>(handle)->InquireAction(
      board_cells, current_seat, stone_to_place, phase);
}

GOMOKU_PLUGIN_API void gomoku_player_apply_action(
    GomokuPlayerHandle handle, int action_id) {
  if (handle) {
    reinterpret_cast<SampleEngine*>(handle)->ApplyAction(action_id);
  }
}

GOMOKU_PLUGIN_API void gomoku_player_on_match_end(
    GomokuPlayerHandle handle, const GomokuMatchResult* result) {
  if (handle) {
    reinterpret_cast<SampleEngine*>(handle)->OnMatchEnd(result);
  }
}

GOMOKU_PLUGIN_API int gomoku_player_get_win_rate(
    GomokuPlayerHandle handle, float* out_win_rate) {
  if (!handle) return 0;
  return reinterpret_cast<SampleEngine*>(handle)->GetWinRate(out_win_rate) ? 1 : 0;
}

GOMOKU_PLUGIN_API int gomoku_player_get_policy(
    GomokuPlayerHandle handle, float* out_policy, int max_actions) {
  if (!handle) return 0;
  return reinterpret_cast<SampleEngine*>(handle)->GetPolicy(out_policy, max_actions);
}

GOMOKU_PLUGIN_API void gomoku_player_cancel_inquiry(GomokuPlayerHandle handle) {
  if (handle) {
    reinterpret_cast<SampleEngine*>(handle)->CancelInquiry();
  }
}

}  // extern "C"
