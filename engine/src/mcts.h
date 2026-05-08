#pragma once
#include <atomic>
#include <memory>
#include <mutex>
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
  int visits() const { return visits_.load(std::memory_order_relaxed); }
  float value_sum() const { return value_sum_.load(std::memory_order_relaxed); }
  bool is_expanded() const {
    return is_expanded_.load(std::memory_order_acquire);
  }
  Seat current_player() const {
    return current_player_;
  }  // The player who is to move AT this node's board state

  void AddVirtualLoss();
  void RevertVirtualLoss();

  const std::vector<std::unique_ptr<MCTSNode>>& children() const {
    return children_;
  }

  // Use a mutex just for expanding children to ensure thread safety
  std::mutex expand_mutex_;

 private:
  int action_id_;
  float prior_prob_;
  Seat current_player_;  // The player to move AT this node

  std::atomic<int> visits_{0};
  std::atomic<float> value_sum_{0.0f};
  std::atomic<bool> is_expanded_{false};

  std::vector<std::unique_ptr<MCTSNode>> children_;
};

class MCTS {
 public:
  MCTS(int num_simulations, int num_threads, float c_puct);

  void Search(const Board& root_board, Evaluator* evaluator);
  int GetBestMove() const;  // Temperature=0 implicitly

  const MCTSNode* root() const { return root_.get(); }

 private:
  int num_simulations_;
  int num_threads_;
  float c_puct_;

  std::unique_ptr<MCTSNode> root_;

  void SearchOnce(const Board& root_board, Evaluator* evaluator);
  float CalculatePUCT(const MCTSNode* parent, const MCTSNode* child) const;
};
