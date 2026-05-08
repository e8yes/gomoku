#include "mcts.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

constexpr int kVirtualLoss = 3;

MCTSNode::MCTSNode(int action_id, float prior_prob, Seat current_player)
    : action_id_(action_id),
      prior_prob_(prior_prob),
      current_player_(current_player) {}

MCTSNode::~MCTSNode() {}

void MCTSNode::Expand(const Board& board, const std::vector<float>& move_pmf) {
  if (is_expanded_.load(std::memory_order_acquire)) return;

  std::lock_guard<std::mutex> lock(expand_mutex_);
  if (is_expanded_.load(std::memory_order_relaxed)) return;  // Double check

  auto legal_actions = board.GetLegalActions();
  for (int action : legal_actions) {
    Board child_board = board;
    child_board.Apply(action);
    children_.push_back(std::make_unique<MCTSNode>(
        action, move_pmf[action], child_board.current_player()));
  }

  is_expanded_.store(true, std::memory_order_release);
}

void MCTSNode::Update(float value) {
  float current_val = value_sum_.load(std::memory_order_relaxed);
  while (!value_sum_.compare_exchange_weak(current_val, current_val + value,
                                           std::memory_order_relaxed)) {
  }
  visits_.fetch_add(1, std::memory_order_relaxed);
}

void MCTSNode::AddVirtualLoss() {
  visits_.fetch_add(kVirtualLoss, std::memory_order_relaxed);
  float loss = -1.0f * kVirtualLoss;
  float current_val = value_sum_.load(std::memory_order_relaxed);
  while (!value_sum_.compare_exchange_weak(current_val, current_val + loss,
                                           std::memory_order_relaxed)) {
  }
}

void MCTSNode::RevertVirtualLoss() {
  visits_.fetch_sub(kVirtualLoss, std::memory_order_relaxed);
  float loss = 1.0f * kVirtualLoss;
  float current_val = value_sum_.load(std::memory_order_relaxed);
  while (!value_sum_.compare_exchange_weak(current_val, current_val + loss,
                                           std::memory_order_relaxed)) {
  }
}

MCTS::MCTS(int num_simulations, int num_threads, float c_puct)
    : num_simulations_(num_simulations),
      num_threads_(num_threads),
      c_puct_(c_puct) {}

void MCTS::Search(const Board& root_board, Evaluator* evaluator) {
  root_ = std::make_unique<MCTSNode>(-1, 1.0f, root_board.current_player());

  // Evaluate root
  EvaluationResult res = evaluator->Evaluate(root_board);
  root_->Expand(root_board, res.move_pmf);

  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads_; ++i) {
    threads.emplace_back([this, root_board, evaluator]() {
      for (int s = 0; s < num_simulations_ / num_threads_; ++s) {
        this->SearchOnce(root_board, evaluator);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

void MCTS::SearchOnce(const Board& root_board, Evaluator* evaluator) {
  MCTSNode* node = root_.get();
  Board board = root_board;
  std::vector<MCTSNode*> search_path;
  node->AddVirtualLoss();
  search_path.push_back(node);

  // Selection
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

    // IMPORTANT: Apply virtual loss IMMEDIATELY upon selection during descent.
    // This acts as a lock-free penalty. If multiple threads start simultaneously, 
    // the moment one thread selects this child, its UCB score artificially drops 
    // for any concurrent threads, naturally steering them to explore different 
    // parallel branches instead of redundantly evaluating the same path.
    node->AddVirtualLoss();
    board.Apply(node->action_id());
    search_path.push_back(node);
  }

  // Expansion & Evaluation
  float leaf_val = 0.0f;
  Seat leaf_seat = node->current_player();

  if (board.IsTerminal()) {
    // Value from perspective of the leaf's current player (who just
    // lost/won/drew) Wait, if it's terminal, the game is over. GetValueForSeat
    // gets the result for leaf_seat.
    leaf_val = board.GetValueForSeat(leaf_seat);
  } else {
    EvaluationResult res = evaluator->Evaluate(board);
    node->Expand(board, res.move_pmf);
    leaf_val = res.value;
  }

  // Backpropagation
  for (MCTSNode* n : search_path) {
    n->RevertVirtualLoss();
    float v = (n->current_player() == leaf_seat) ? leaf_val : -leaf_val;
    n->Update(v);
  }
}

float MCTS::CalculatePUCT(const MCTSNode* parent, const MCTSNode* child) const {
  float q = 0.0f;
  int child_visits = child->visits();
  if (child_visits > 0) {
    // q is the average value of the child node.
    // It's from the child's perspective.
    // But UCB is for the parent choosing.
    // If child's current_player is the same as parent's, then q is good.
    // If child's current_player is different, parent wants to minimize child's
    // value, so we use -q.
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
