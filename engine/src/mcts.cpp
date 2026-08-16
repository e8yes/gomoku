#include "mcts.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

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

std::vector<float> MakeUniformLegalPolicy(const Board& board) {
  std::vector<float> policy(Board::kNumActions, 0.0f);
  const std::vector<int> legal_actions = board.GetLegalActions();
  if (legal_actions.empty()) return policy;

  const float uniform = 1.0f / static_cast<float>(legal_actions.size());
  for (int action : legal_actions) policy[action] = uniform;
  return policy;
}

std::vector<float> MakePriorPolicy(const MCTSNode* root) {
  std::vector<float> policy(Board::kNumActions, 0.0f);
  if (root == nullptr) return policy;

  float prior_sum = 0.0f;
  for (const auto& child : root->children()) {
    prior_sum += std::max(0.0f, child->prior_prob());
  }
  if (prior_sum > 0.0f) {
    for (const auto& child : root->children()) {
      policy[child->action_id()] =
          std::max(0.0f, child->prior_prob()) / prior_sum;
    }
    return policy;
  }

  if (!root->children().empty()) {
    const float uniform = 1.0f / static_cast<float>(root->children().size());
    for (const auto& child : root->children()) {
      policy[child->action_id()] = uniform;
    }
  }
  return policy;
}

void MaskPolicyToSafeActions(const Board& board,
                             const EndgameDefenseAnalysis& analysis,
                             std::vector<float>* move_pmf) {
  if (!analysis.threat_detected || analysis.safe_actions.empty() ||
      move_pmf->size() != Board::kNumActions) {
    return;
  }

  std::vector<bool> is_safe(Board::kNumActions, false);
  for (int action : analysis.safe_actions) {
    if (action >= 0 && action < Board::kNumActions &&
        IsLegalAction(board, action)) {
      is_safe[action] = true;
    }
  }

  int safe_count = 0;
  float safe_prior_sum = 0.0f;
  for (int action : board.GetLegalActions()) {
    if (!is_safe[action]) continue;
    ++safe_count;
    safe_prior_sum += std::max(0.0f, (*move_pmf)[action]);
  }
  if (safe_count == 0) return;

  for (int action : board.GetLegalActions()) {
    if (!is_safe[action]) {
      (*move_pmf)[action] = 0.0f;
    } else if (safe_prior_sum > 0.0f) {
      (*move_pmf)[action] =
          std::max(0.0f, (*move_pmf)[action]) / safe_prior_sum;
    } else {
      (*move_pmf)[action] = 1.0f / static_cast<float>(safe_count);
    }
  }
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

  const auto legal_actions = board.GetLegalActions();
  children_.reserve(legal_actions.size());

  if (board.phase() == Phase::kStandard) {
    const Seat next_player =
        (board.current_player() == Seat::kA) ? Seat::kB : Seat::kA;
    for (int action : legal_actions) {
      children_.push_back(
          std::make_unique<MCTSNode>(action, move_pmf[action], next_player));
    }
  } else {
    for (int action : legal_actions) {
      const Seat next_player = board.GetChildCurrentPlayer(action);
      children_.push_back(
          std::make_unique<MCTSNode>(action, move_pmf[action], next_player));
    }
  }

  is_expanded_ = true;
}

void MCTSNode::Update(float value) {
  value_sum_ += value;
  visits_ += 1;
}

void MCTSNode::AddVirtualLoss(Seat selecting_player, int virtual_loss) {
  visits_ += virtual_loss;
  // value_sum_ is stored from this node's current-player perspective, while
  // PUCT compares the child from its selector's perspective. Most turns swap
  // the seat, but Swap2's initial placements do not. Choose the sign that
  // makes this edge a loss in either case.
  const float value = static_cast<float>(virtual_loss);
  value_sum_ += current_player_ == selecting_player ? -value : value;
}

void MCTSNode::RevertVirtualLoss(Seat selecting_player, int virtual_loss) {
  visits_ -= virtual_loss;
  const float value = static_cast<float>(virtual_loss);
  value_sum_ -= current_player_ == selecting_player ? -value : value;
}

MCTS::MCTS(int batch_size, float c_puct,
           std::optional<DirichletNoiseConfig> dirichlet_noise,
           int virtual_loss)
    : batch_size_(batch_size),
      c_puct_(c_puct),
      virtual_loss_(virtual_loss),
      dirichlet_noise_(dirichlet_noise),
      random_engine_(dirichlet_noise.has_value() && dirichlet_noise->seed != 0
                         ? dirichlet_noise->seed
                         : std::random_device{}()),
      randomize_ties_(dirichlet_noise.has_value()) {
  if (batch_size_ <= 0) {
    throw std::invalid_argument("MCTS batch size must be positive");
  }
  if (c_puct_ < 0.0f) {
    throw std::invalid_argument("MCTS c_puct must not be negative");
  }
  if (virtual_loss_ < 0) {
    throw std::invalid_argument("MCTS virtual loss must not be negative");
  }
}

std::vector<float> MCTS::Search(const Board& root_board, Evaluator* evaluator,
                                SearchStoppingCriteria stopping_criteria,
                                EndgameSolver endgame_solver,
                                EndgameDefenseSolver defensive_solver) {
  int simulation_limit = 0;
  std::optional<std::chrono::steady_clock::time_point> deadline;
  if (std::holds_alternative<int>(stopping_criteria)) {
    simulation_limit = std::get<int>(stopping_criteria);
    if (simulation_limit <= 0) {
      throw std::invalid_argument(
          "MCTS simulation stopping criterion must be positive");
    }
  } else {
    const std::chrono::milliseconds duration =
        std::get<std::chrono::milliseconds>(stopping_criteria);
    if (duration.count() < 0) {
      throw std::invalid_argument(
          "MCTS deadline stopping criterion must not be negative");
    }
    deadline = std::chrono::steady_clock::now() + duration;
  }

  const auto deadline_reached = [&]() {
    return deadline.has_value() &&
           std::chrono::steady_clock::now() >= deadline.value();
  };

  // A deadline is a best-effort search cutoff. Evaluator and solver callbacks
  // are synchronous, so an in-flight callback is allowed to finish; callers
  // should reserve time for that work and for move submission.
  if (deadline_reached()) {
    return root_ == nullptr ? MakeUniformLegalPolicy(root_board)
                            : MakePriorPolicy(root_.get());
  }

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
    if (res.empty()) {
      throw std::runtime_error("Evaluator returned empty result for root board");
    }
    if (defensive_solver) {
      const EndgameDefenseAnalysis analysis = defensive_solver(root_board);
      MaskPolicyToSafeActions(root_board, analysis, &res[0].move_pmf);
      root_defensive_mask_active_ =
          analysis.threat_detected && !analysis.safe_actions.empty();
      defensive_filter_applied_ = true;
    }
    root_->Expand(root_board, res[0].move_pmf);
  }

  if (deadline_reached()) return MakePriorPolicy(root_.get());

  // A preserved child can already be expanded when it becomes the root. In
  // that case apply the defensive mask to its cached priors before searching.
  if (defensive_solver && !defensive_filter_applied_) {
    ApplyDefensiveFilter(root_board, defensive_solver);
    defensive_filter_applied_ = true;
  }

  // The tree is reused after every move, so the child that becomes the next
  // root is normally already expanded. Apply root noise after that transition
  // as well, exactly once for each root, rather than only when the tree is
  // created at the beginning of the game.
  if (!root_noise_applied_) {
    ApplyRootNoise(root_board);
    root_noise_applied_ = true;
  }

  int simulations_done = 0;
  while (simulations_done < simulation_limit || deadline.has_value()) {
    if (deadline_reached()) break;

    const int current_batch_size =
        deadline.has_value()
            ? batch_size_
            : std::min(batch_size_, simulation_limit - simulations_done);

    std::vector<Board> leaf_boards;
    std::vector<std::vector<MCTSNode*>> paths(current_batch_size);
    std::vector<int> leaf_indices(current_batch_size, -1);
    std::vector<float> terminal_values(current_batch_size, 0.0f);
    std::vector<bool> is_terminal(current_batch_size, false);

    for (int i = 0; i < current_batch_size; ++i) {
      MCTSNode* node = root_.get();
      Board board = root_board;
      paths[i].push_back(node);

      while (node->is_expanded() && !node->children().empty()) {
        float max_puct = -std::numeric_limits<float>::infinity();
        MCTSNode* best_child = nullptr;
        int max_puct_ties = 0;

        for (const auto& child : node->children()) {
          if (node == root_.get() && root_defensive_mask_active_ &&
              child->prior_prob() <= 0.0f) {
            continue;
          }
          float puct = CalculatePUCT(node, child.get(), c_puct_);
          if (puct > max_puct) {
            max_puct = puct;
            best_child = child.get();
            max_puct_ties = 1;
          } else if (puct == max_puct) {
            ++max_puct_ties;
            if (randomize_ties_ && std::uniform_int_distribution<int>(
                                       1, max_puct_ties)(random_engine_) == 1) {
              best_child = child.get();
            }
          }
        }

        MCTSNode* parent = node;
        node = best_child;
        node->AddVirtualLoss(parent->current_player(), virtual_loss_);
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
        std::vector<int> solved_actions(missed_leaf_boards.size(), -1);
        auto side_work_fn = [&]() {
          if (endgame_solver) {
            for (size_t i = 0; i < missed_leaf_boards.size(); ++i) {
              solved_actions[i] =
                  GetSolvedAction(missed_leaf_boards[i], endgame_solver);
            }
          }
        };

        // Submit un-cached leaves to the evaluator, executing VCF side work
        // on this thread while GPU inference is in flight.
        std::vector<EvaluationResult> eval_results =
            evaluator->Evaluate(missed_leaf_boards, side_work_fn);
        if (eval_results.size() != missed_leaf_boards.size()) {
          throw std::runtime_error(
              "Evaluator returned " + std::to_string(eval_results.size()) +
              " results for " + std::to_string(missed_leaf_boards.size()) +
              " candidate boards");
        }

        // Populate evaluation cache, using VCF solved actions to override
        // neural net results.
        for (size_t i = 0; i < missed_leaf_boards.size(); ++i) {
          if (solved_actions[i] >= 0) {
            evaluation_cache_[missed_leaf_boards[i].signature()] =
                MakeSolvedEvaluation(solved_actions[i]);
          } else {
            evaluation_cache_[missed_leaf_boards[i].signature()] =
                std::move(eval_results[i]);
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

      for (size_t path_index = 0; path_index < paths[i].size(); ++path_index) {
        MCTSNode* n = paths[i][path_index];
        if (path_index > 0) {
          n->RevertVirtualLoss(paths[i][path_index - 1]->current_player(),
                               virtual_loss_);
        }
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
  } else {
    policy = MakePriorPolicy(root_.get());
  }

  return policy;
}

void MCTS::SelectAction(int action_id) {
  if (!root_) return;

  std::unique_ptr<MCTSNode> child = root_->DetachChild(action_id);
  if (child) {
    root_ = std::move(child);
    root_noise_applied_ = false;
    defensive_filter_applied_ = false;
    root_defensive_mask_active_ = false;
  } else {
    // Action not found in children. Reset the tree.
    Reset();
  }
}

void MCTS::Reset() {
  root_ = nullptr;
  root_noise_applied_ = false;
  defensive_filter_applied_ = false;
  root_defensive_mask_active_ = false;
  evaluation_cache_.clear();
}

void MCTS::SetDirichletNoise(
    std::optional<DirichletNoiseConfig> dirichlet_noise) {
  dirichlet_noise_ = dirichlet_noise;
  if (dirichlet_noise_) randomize_ties_ = true;
  root_noise_applied_ = false;
  if (dirichlet_noise_ && dirichlet_noise_->seed != 0) {
    random_engine_.seed(dirichlet_noise_->seed);
  }
}

void MCTS::ApplyRootNoise(const Board& root_board) {
  if (!dirichlet_noise_ || !root_ || root_->children().empty()) return;

  std::vector<float> priors(Board::kNumActions, 0.0f);
  for (const auto& child : root_->children()) {
    priors[child->action_id()] = child->prior_prob();
  }

  const std::vector<float> noisy_priors =
      AddDirichletNoise(root_board, priors, *dirichlet_noise_, &random_engine_);
  for (const auto& child : root_->children()) {
    child->set_prior_prob(noisy_priors[child->action_id()]);
  }
}

void MCTS::ApplyDefensiveFilter(const Board& root_board,
                                const EndgameDefenseSolver& defensive_solver) {
  if (!root_ || !defensive_solver) return;

  const EndgameDefenseAnalysis analysis = defensive_solver(root_board);
  if (!analysis.threat_detected || analysis.safe_actions.empty()) return;

  root_defensive_mask_active_ = true;

  std::vector<float> root_priors(Board::kNumActions, 0.0f);
  for (const auto& child : root_->children()) {
    root_priors[child->action_id()] = child->prior_prob();
  }
  MaskPolicyToSafeActions(root_board, analysis, &root_priors);
  for (const auto& child : root_->children()) {
    child->set_prior_prob(root_priors[child->action_id()]);
  }
}
