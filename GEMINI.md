# Gomoku Engine Development Plan (AlphaZero Approach)

This document outlines the development plan for our Gomoku engine with Swap2 support, competing against Claude Code on the `gomoku_match` server. We are utilizing an **AlphaZero-style architecture (MCTS + Neural Network)** trained over 15 days on an RTX 4060 Ti.

## 1. Architecture: C++ Engine & Python Training

- **C++ Search Engine**: The MCTS core, board logic, and batched inference manager will be written purely in C++. During self-play, this C++ engine handles the tree search and queries the GPU.
- **Python Training Pipeline**: Python will be used to manage the replay buffer, parse data, and train the PyTorch ResNet model. The trained weights will be exported (e.g., via TorchScript/ONNX) to be loaded by the C++ engine.

## 2. Advanced Training & Data Strategy

### 2.1 Curriculum Learning (Endgame First)
Instead of keeping all moves from early random self-play (which is mostly noise), we will use an **incremental horizon strategy**:
- **Iteration 1**: Only keep the final 1-2 moves of each self-play game. This trains the network on absolute tactical truths (immediate wins/losses).
- **Subsequent Iterations**: Incrementally add ~2 more moves from the end of the game per iteration. By Iteration 15, we will be keeping nearly all moves.
- **Benefit**: This acts as curriculum learning. The value network perfectly learns the endgame first, and as the horizon expands, it learns to accurately evaluate mid-game states that lead to those known endgames.

### 2.2 Data Augmentation & Minimax Perturbation
- **Symmetries**: Every position generated will be augmented using the 8 dihedral symmetries (rotations and flips) of the Gomoku board.
- **Minimax Perturbation**: To quickly bootstrap tactical knowledge in early iterations, we will retract a few moves from the endgame of self-play games and run a fast, shallow **Alpha-Beta/Minimax search** to find the exact tactical evaluations and forced sequences. These perfectly evaluated "perturbed" positions will be injected into the training data.

## 3. MCTS Implementation Details

### 3.1 Zero Rollout (AlphaZero Style)
We will completely abandon random rollouts. The MCTS leaf evaluations will rely **100% on the Neural Network's Value head**. This prevents the search from being distorted by the blind blunders typical of random Gomoku rollouts.

### 3.2 Tree Caching
The MCTS tree will persist between turns. When the opponent makes a move, we simply advance the root pointer to the corresponding child node, preserving all the search statistics (visit counts, Q-values) accumulated in that subtree from previous thinking time.

### 3.3 Global Batched Inference Manager
To maximize the throughput of the RTX 4060 Ti during self-play, the C++ engine will instantiate **multiple parallel games** sharing a single GPU.
- **Queuing**: When an MCTS search in a game hits a leaf node, it suspends its search and enqueues the board state to a global inference manager.
- **Batching**: A dedicated inference thread dequeues these states, batches them into a single tensor (e.g., batch size 64-256), and runs a single forward pass through the Neural Network via `libtorch` or TensorRT.
- **Resumption**: The policy and value results are mapped back to the suspended MCTS searches, which then update their trees and continue.

## 4. Implementation Phases (Divide & Conquer)

### Phase 1: Core Board & Parallel MCTS Foundation
- **Fast Board Engine**: Implement an extremely efficient C++ board. Focus on **O(1)** move placement, **O(1)** retraction, and **O(1)** exact-five endgame detection. 
- **Validation**: Ensure 100% logic coverage with `googletest`.
- **Evaluator Interface**: Design a generic `Evaluator` interface that MCTS will call to get policy and value for a leaf node.
- **Random Evaluator & Parallel Search**: Implement a `RandomEvaluator`. Implement multi-threaded MCTS (using virtual loss to prevent thread collisions). Verify that the parallel MCTS with the random evaluator can reliably find simple endgame combinations.

### Phase 2: LibTorch Integration & Single-Game Batching
- **LibTorch Evaluator**: Implement the Neural Net evaluator using `libtorch`, inheriting from our `Evaluator` interface.
- **Parallel Tree Search Batching**: Implement the queuing mechanism to batch node evaluations from the multiple MCTS threads exploring the *single* game tree.

### Phase 3: Python Training Pipeline
- Build the PyTorch ResNet model.
- Implement the replay buffer, data parser, and the iterative training loop.

### Phase 4: Self-Play Data Generation & Augmentation
- **Multi-Game Orchestration**: Expand the batching manager to handle high-throughput self-play data generation by orchestrating multiple parallel games sharing the same global inference thread.
- **Data Augmentation**: Incorporate the data augmentation tricks mentioned in Section 2.2 (dihedral symmetries and Minimax perturbation).
- **15-Day Run**: Start the iterative cycle of self-play and training using the Curriculum Learning (endgame first) strategy.

### Phase 5: Match Server Integration
- **Server Client**: Build the `engine.py` wrapper using `gomoku_match.PlayerClient` to compete on the server.

## 5. Building and Testing

To build the C++ engine and run its unit tests (which require a system installation of `googletest`), run:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ../engine
make -j$(nproc)
./engine_tests
```
