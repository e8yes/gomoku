#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "board.h"
#include "evaluator.h"

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
  int visits() const { return visits_; }
  float value_sum() const { return value_sum_; }
  bool is_expanded() const { return is_expanded_; }
  Seat current_player() const { return current_player_; }

  void AddVirtualLoss();
  void RevertVirtualLoss();

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
  // Initializes the MCTS engine with search hyperparameters.
  MCTS(int num_simulations, int batch_size, float c_puct);

  // Performs MCTS search from the current root_board and returns the
  // simulated policy (normalized visit counts for each action).
  std::vector<float> Search(const Board& root_board, Evaluator* evaluator);

  // Advances the root node to the child corresponding to action_id, preserving
  // the search tree for future searches. Discards the rest of the tree.
  void SelectAction(int action_id);

  // Clears the cached search tree.
  void Reset();

  // Returns the root node of the search tree.
  const MCTSNode* root() const { return root_.get(); }

 private:
  int num_simulations_;
  int batch_size_;
  float c_puct_;

  std::unique_ptr<MCTSNode> root_;
  std::unordered_map<BoardSignature, EvaluationResult, BoardSignatureHash>
      evaluation_cache_;
};

// Helper to extract the action ID with the highest probability from a policy.
int GetBestAction(const std::vector<float>& policy);
