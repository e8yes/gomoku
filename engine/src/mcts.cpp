#include "mcts.h"

#include <algorithm>
#include <cmath>
#include <limits>

constexpr int kVirtualLoss = 3;

MCTSNode::MCTSNode(int action_id, float prior_prob, Seat current_player)
    : action_id_(action_id),
      prior_prob_(prior_prob),
      current_player_(current_player) {}

MCTSNode::~MCTSNode() {}

void MCTSNode::Expand(const Board& board, const std::vector<float>& move_pmf) {
  if (is_expanded_) return;

  auto legal_actions = board.GetLegalActions();
  for (int action : legal_actions) {
    Board child_board = board;
    child_board.Apply(action);
    children_.push_back(std::make_unique<MCTSNode>(
        action, move_pmf[action], child_board.current_player()));
  }

  is_expanded_ = true;
}

void MCTSNode::Update(float value) {
  value_sum_ += value;
  visits_ += 1;
}

void MCTSNode::AddVirtualLoss() {
  visits_ += kVirtualLoss;
  value_sum_ -= static_cast<float>(kVirtualLoss);
}

void MCTSNode::RevertVirtualLoss() {
  visits_ -= kVirtualLoss;
  value_sum_ += static_cast<float>(kVirtualLoss);
}

MCTS::MCTS(int num_simulations, int batch_size, float c_puct)
    : num_simulations_(num_simulations),
      batch_size_(batch_size),
      c_puct_(c_puct) {}

void MCTS::Search(const Board& root_board, Evaluator* evaluator) {
  // TODO: Implement Tree Caching (Section 3.2). Currently we throw away the 
  // tree on every search. We should preserve it and advance the root pointer.
  root_ = std::make_unique<MCTSNode>(-1, 1.0f, root_board.current_player());

  // Evaluate root
  auto res = evaluator->Evaluate({root_board});
  root_->Expand(root_board, res[0].move_pmf);

  int simulations_done = 0;
  while (simulations_done < num_simulations_) {
    int current_batch_size = std::min(batch_size_, num_simulations_ - simulations_done);
    
    std::vector<Board> leaf_boards;
    std::vector<std::vector<MCTSNode*>> paths(current_batch_size);
    std::vector<int> leaf_indices(current_batch_size, -1);
    std::vector<float> terminal_values(current_batch_size, 0.0f);
    std::vector<bool> is_terminal(current_batch_size, false);

    for (int i = 0; i < current_batch_size; ++i) {
      MCTSNode* node = root_.get();
      Board board = root_board;
      paths[i].push_back(node);
      node->AddVirtualLoss();

      while (node->is_expanded() && !node->children().empty()) {
        float max_puct = -std::numeric_limits<float>::infinity();
        MCTSNode* best_child = nullptr;

        for (const auto& child : node->children()) {
          float puct = CalculatePUCT(node, child.get());
          if (puct > max_puct) {
            max_puct = puct;
            best_child = child.get();
          }
        }

        node = best_child;
        node->AddVirtualLoss();
        board.Apply(node->action_id());
        paths[i].push_back(node);
      }

      if (board.IsTerminal()) {
        is_terminal[i] = true;
        terminal_values[i] = board.GetValueForSeat(node->current_player());
      } else {
        leaf_indices[i] = leaf_boards.size();
        leaf_boards.push_back(board);
      }
    }

    std::vector<EvaluationResult> eval_results;
    if (!leaf_boards.empty()) {
      eval_results = evaluator->Evaluate(leaf_boards);
    }

    for (int i = 0; i < current_batch_size; ++i) {
      float leaf_val = 0.0f;
      Seat leaf_seat = paths[i].back()->current_player();

      if (is_terminal[i]) {
        leaf_val = terminal_values[i];
      } else {
        const auto& eval_res = eval_results[leaf_indices[i]];
        paths[i].back()->Expand(leaf_boards[leaf_indices[i]], eval_res.move_pmf);
        leaf_val = eval_res.value;
      }

      for (MCTSNode* n : paths[i]) {
        n->RevertVirtualLoss();
        float v = (n->current_player() == leaf_seat) ? leaf_val : -leaf_val;
        n->Update(v);
      }
    }

    simulations_done += current_batch_size;
  }
}

float MCTS::CalculatePUCT(const MCTSNode* parent, const MCTSNode* child) const {
  float q = 0.0f;
  int child_visits = child->visits();
  if (child_visits > 0) {
    float raw_q = child->value_sum() / child_visits;
    q = (parent->current_player() == child->current_player()) ? raw_q : -raw_q;
  }

  float u = c_puct_ * child->prior_prob() *
            std::sqrt(static_cast<float>(parent->visits())) /
            (1.0f + child_visits);

  return q + u;
}

int MCTS::GetBestMove() const {
  int best_action = -1;
  int max_visits = -1;

  for (const auto& child : root_->children()) {
    if (child->visits() > max_visits) {
      max_visits = child->visits();
      best_action = child->action_id();
    }
  }

  return best_action;
}
