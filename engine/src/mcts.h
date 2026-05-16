#pragma once
#include <memory>
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
  MCTS(int num_simulations, int batch_size, float c_puct);

  void Search(const Board& root_board, Evaluator* evaluator);
  int GetBestMove() const;  // Temperature=0 implicitly

  const MCTSNode* root() const { return root_.get(); }

 private:
  int num_simulations_;
  int batch_size_;
  float c_puct_;

  std::unique_ptr<MCTSNode> root_;

  float CalculatePUCT(const MCTSNode* parent, const MCTSNode* child) const;
};
