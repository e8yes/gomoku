#include "mcts.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

constexpr int kVirtualLoss = 3;

namespace {

// Calculates the PUCT (Polynomial Upper Confidence Trees) value for a child
// node.
float CalculatePUCT(const MCTSNode* parent, const MCTSNode* child,
                    float c_puct) {
  float q = 0.0f;
  int child_visits = child->visits();
  if (child_visits > 0) {
    float raw_q = child->value_sum() / child_visits;
    q = (parent->current_player() == child->current_player()) ? raw_q : -raw_q;
  }

  float u = c_puct * child->prior_prob() *
            std::sqrt(static_cast<float>(parent->visits())) /
            (1.0f + child_visits);

  return q + u;
}

bool IsLegalAction(const Board& board, int action_id) {
  const auto legal_actions = board.GetLegalActions();
  return std::find(legal_actions.begin(), legal_actions.end(), action_id) !=
         legal_actions.end();
}

int GetSolvedAction(const Board& board, const EndgameSolver& endgame_solver) {
  if (!endgame_solver) return -1;

  const std::vector<int> winning_line = endgame_solver(board);
  if (winning_line.empty() || !IsLegalAction(board, winning_line.front())) {
    return -1;
  }
  return winning_line.front();
}

EvaluationResult MakeSolvedEvaluation(int action_id) {
  EvaluationResult result;
  result.move_pmf.assign(Board::kNumActions, 0.0f);
  result.move_pmf[action_id] = 1.0f;
  result.value = 1.0f;
  return result;
}

std::vector<float> MakeSolvedPolicy(int action_id) {
  std::vector<float> policy(Board::kNumActions, 0.0f);
  policy[action_id] = 1.0f;
  return policy;
}

bool IsValidDirichletNoiseConfig(const DirichletNoiseConfig& config) {
  return config.alpha > 0.0f && config.epsilon >= 0.0f &&
         config.epsilon <= 1.0f;
}

std::vector<float> AddDirichletNoise(const Board& board,
                                     const std::vector<float>& move_pmf,
                                     const DirichletNoiseConfig& config,
                                     std::mt19937_64* random_engine) {
  if (!IsValidDirichletNoiseConfig(config) || config.epsilon == 0.0f) {
    return move_pmf;
  }

  const std::vector<int> legal_actions = board.GetLegalActions();
  if (legal_actions.empty()) return move_pmf;

  std::gamma_distribution<float> gamma(config.alpha, 1.0f);
  std::vector<float> noise(legal_actions.size());
  float noise_sum = 0.0f;
  for (float& value : noise) {
    value = gamma(*random_engine);
    noise_sum += value;
  }
  if (noise_sum <= 0.0f) return move_pmf;

  std::vector<float> noisy_move_pmf = move_pmf;
  const float prior_weight = 1.0f - config.epsilon;
  for (size_t i = 0; i < legal_actions.size(); ++i) {
    const int action = legal_actions[i];
    noisy_move_pmf[action] =
        prior_weight * move_pmf[action] + config.epsilon * noise[i] / noise_sum;
  }
  return noisy_move_pmf;
}

}  // namespace

int GetBestAction(const std::vector<float>& policy) {
  if (policy.empty()) {
    // Should not happen in practice.
    return -1;
  }
  auto it = std::max_element(policy.begin(), policy.end());
  return std::distance(policy.begin(), it);
}

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

MCTS::MCTS(int num_simulations, int batch_size, float c_puct,
           std::optional<DirichletNoiseConfig> dirichlet_noise)
    : num_simulations_(num_simulations),
      batch_size_(batch_size),
      c_puct_(c_puct),
      dirichlet_noise_(dirichlet_noise),
      random_engine_(dirichlet_noise.has_value() && dirichlet_noise->seed != 0
                         ? dirichlet_noise->seed
                         : std::random_device{}()) {}

std::vector<float> MCTS::Search(const Board& root_board, Evaluator* evaluator,
                                EndgameSolver endgame_solver) {
  // A proven root win can be returned immediately. This keeps the solver
  // callback useful even before the MCTS tree has been expanded.
  const int solved_root_action = GetSolvedAction(root_board, endgame_solver);
  if (solved_root_action >= 0) {
    return MakeSolvedPolicy(solved_root_action);
  }

  if (!root_) {
    root_ = std::make_unique<MCTSNode>(-1, 1.0f, root_board.current_player());
  }

  // Evaluate root if not expanded
  if (!root_->is_expanded()) {
    auto res = evaluator->Evaluate({root_board});
    if (dirichlet_noise_) {
      res[0].move_pmf = AddDirichletNoise(root_board, res[0].move_pmf,
                                          *dirichlet_noise_, &random_engine_);
    }
    root_->Expand(root_board, res[0].move_pmf);
  }

  int simulations_done = 0;
  while (simulations_done < num_simulations_) {
    int current_batch_size =
        std::min(batch_size_, num_simulations_ - simulations_done);

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
          float puct = CalculatePUCT(node, child.get(), c_puct_);
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

    if (!leaf_boards.empty()) {
      // Move the cache-missed leaves to their own vector, and copy the rest.
      std::vector<Board> missed_leaf_boards;
      std::vector<int> missed_leaf_indices;
      missed_leaf_boards.reserve(leaf_boards.size());
      missed_leaf_indices.reserve(leaf_boards.size());

      for (int i = 0; i < leaf_boards.size(); ++i) {
        if (evaluation_cache_.contains(leaf_boards[i].signature())) {
          continue;
        }

        bool already_missed = false;
        for (const auto& missed_board : missed_leaf_boards) {
          if (missed_board.signature() == leaf_boards[i].signature()) {
            already_missed = true;
            break;
          }
        }
        if (already_missed) {
          continue;
        }

        missed_leaf_boards.push_back(std::move(leaf_boards[i]));
        missed_leaf_indices.push_back(i);
      }

      if (!missed_leaf_boards.empty()) {
        // Let the endgame solver override evaluator results when it proves a
        // win. Unsolved leaves are sent to the evaluator in one batch.
        std::vector<Board> evaluator_boards;
        evaluator_boards.reserve(missed_leaf_boards.size());

        for (const Board& board : missed_leaf_boards) {
          const int solved_action = GetSolvedAction(board, endgame_solver);
          if (solved_action >= 0) {
            evaluation_cache_[board.signature()] =
                MakeSolvedEvaluation(solved_action);
          } else {
            evaluator_boards.push_back(board);
          }
        }

        if (!evaluator_boards.empty()) {
          std::vector<EvaluationResult> eval_results =
              evaluator->Evaluate(evaluator_boards);
          for (size_t i = 0; i < eval_results.size(); ++i) {
            evaluation_cache_[evaluator_boards[i].signature()] =
                eval_results[i];
          }
        }

        // Move the inference input back to the original vector.
        for (size_t i = 0; i < missed_leaf_boards.size(); ++i) {
          leaf_boards[missed_leaf_indices[i]] =
              std::move(missed_leaf_boards[i]);
        }
      }
    }

    for (int i = 0; i < current_batch_size; ++i) {
      float leaf_val = 0.0f;
      Seat leaf_seat = paths[i].back()->current_player();

      if (is_terminal[i]) {
        leaf_val = terminal_values[i];
      } else {
        const auto& eval_res =
            evaluation_cache_.at(leaf_boards[leaf_indices[i]].signature());
        paths[i].back()->Expand(leaf_boards[leaf_indices[i]],
                                eval_res.move_pmf);
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

  std::vector<float> policy(Board::kNumActions, 0.0f);
  int total_visits = 0;

  for (const auto& child : root_->children()) {
    total_visits += child->visits();
  }

  if (total_visits > 0) {
    for (const auto& child : root_->children()) {
      policy[child->action_id()] =
          static_cast<float>(child->visits()) / total_visits;
    }
  }

  return policy;
}

void MCTS::SelectAction(int action_id) {
  if (!root_) return;

  std::unique_ptr<MCTSNode> child = root_->DetachChild(action_id);
  if (child) {
    root_ = std::move(child);
  } else {
    // Action not found in children. Reset the tree.
    Reset();
  }
}

void MCTS::Reset() {
  root_ = nullptr;
  evaluation_cache_.clear();
}

void MCTS::SetDirichletNoise(
    std::optional<DirichletNoiseConfig> dirichlet_noise) {
  dirichlet_noise_ = dirichlet_noise;
  if (dirichlet_noise_ && dirichlet_noise_->seed != 0) {
    random_engine_.seed(dirichlet_noise_->seed);
  }
}
