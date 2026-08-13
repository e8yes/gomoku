#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <variant>
#include <vector>

#include "board.h"
#include "endgame_solver.h"
#include "evaluator.h"

// AlphaZero-style root noise. Noise is opt-in; an omitted configuration keeps
// MCTS deterministic and suitable for evaluation.
struct DirichletNoiseConfig {
  float alpha = 0.3f;
  float epsilon = 0.25f;
  std::uint64_t seed = 0;  // Zero selects a nondeterministic seed.
};

// Search limits are selected per invocation. A simulation count is useful for
// deterministic self-play and tests; a duration lets a match client spend
// the available clock without retaining a hidden fixed simulation cap in the
// persistent MCTS object.
using SearchStoppingCriteria =
    std::variant<int, std::chrono::milliseconds>;

class MCTSNode {
 public:
  MCTSNode(int action_id, float prior_prob, Seat current_player);
  ~MCTSNode();

  // Prevent copying
  MCTSNode(const MCTSNode&) = delete;
  MCTSNode& operator=(const MCTSNode&) = delete;

  void Expand(const Board& board, const std::vector<float>& move_pmf);
  void Update(float value);

  int action_id() const { return action_id_; }
  float prior_prob() const { return prior_prob_; }
  void set_prior_prob(float prior_prob) { prior_prob_ = prior_prob; }
  int visits() const { return visits_; }
  float value_sum() const { return value_sum_; }
  bool is_expanded() const { return is_expanded_; }
  Seat current_player() const { return current_player_; }

  // Applies a temporary loss from `selecting_player`'s perspective. Node
  // values are stored from current_player(), so the sign depends on whether
  // a Swap2 action changed the player to move.
  void AddVirtualLoss(Seat selecting_player, int virtual_loss);
  void RevertVirtualLoss(Seat selecting_player, int virtual_loss);

  const std::vector<std::unique_ptr<MCTSNode>>& children() const {
    return children_;
  }

  // Removes and returns the child node corresponding to the given action_id.
  // Returns nullptr if no such child exists.
  std::unique_ptr<MCTSNode> DetachChild(int action_id) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
      if ((*it)->action_id() == action_id) {
        std::unique_ptr<MCTSNode> child = std::move(*it);
        children_.erase(it);
        return child;
      }
    }
    return nullptr;
  }

 private:
  int action_id_;
  float prior_prob_;
  Seat current_player_;  // The player to move AT this node

  int visits_{0};
  float value_sum_{0.0f};
  bool is_expanded_{false};

  std::vector<std::unique_ptr<MCTSNode>> children_;
};

class MCTS {
 public:
  // Initializes the persistent MCTS engine with search hyperparameters.
  MCTS(int batch_size, float c_puct,
       std::optional<DirichletNoiseConfig> dirichlet_noise = std::nullopt,
       int virtual_loss = 3);

  // Performs MCTS search from the current root_board and returns the
  // simulated policy (normalized visit counts for each action). When an
  // endgame solver is supplied, a proven root win is returned immediately and
  // proven leaf wins override neural evaluation. An optional defensive solver
  // can mask root actions that expose the opponent to a proven VCF.
  std::vector<float> Search(const Board& root_board, Evaluator* evaluator,
                            SearchStoppingCriteria stopping_criteria,
                            EndgameSolver endgame_solver = {},
                            EndgameDefenseSolver defensive_solver = {});

  // Advances the root node to the child corresponding to action_id, preserving
  // the search tree for future searches. Discards the rest of the tree.
  void SelectAction(int action_id);

  // Clears the cached search tree.
  void Reset();

  // Changes root-noise behavior for subsequent active roots. This is used by
  // evaluation matches to enable exploration for the opening plies and then
  // switch to deterministic search without discarding the tree cache.
  void SetDirichletNoise(std::optional<DirichletNoiseConfig> dirichlet_noise);

  // Returns the root node of the search tree.
  const MCTSNode* root() const { return root_.get(); }

 private:
  void ApplyRootNoise(const Board& root_board);
  void ApplyDefensiveFilter(const Board& root_board,
                            const EndgameDefenseSolver& defensive_solver);

  int batch_size_;
  float c_puct_;
  int virtual_loss_;
  std::optional<DirichletNoiseConfig> dirichlet_noise_;
  std::mt19937_64 random_engine_;
  // Self-play keeps this enabled after root noise is disabled so exact PUCT
  // ties cannot fall back to the board's row-major action order. MCTS remains
  // deterministic when it was constructed without exploration enabled.
  bool randomize_ties_{false};
  bool root_noise_applied_{false};
  bool defensive_filter_applied_{false};
  // True only when the defensive callback identified a threat and supplied
  // safe root moves. Zero-prior unsafe children must then be excluded from
  // selection, rather than merely discouraged.
  bool root_defensive_mask_active_{false};

  std::unique_ptr<MCTSNode> root_;
  std::unordered_map<BoardSignature, EvaluationResult, BoardSignatureHash>
      evaluation_cache_;
};

// Helper to extract the action ID with the highest probability from a policy.
int GetBestAction(const std::vector<float>& policy);
