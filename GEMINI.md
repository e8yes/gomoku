# Gomoku Engine Development Plan (AlphaZero Approach)

This document outlines the development plan for our Gomoku engine with Swap2 support, competing against Claude Code on the `gomoku_match` server. We are utilizing an **AlphaZero-style architecture (MCTS + Neural Network)** trained over 10 days on an RTX 4060 Ti.

## 1. Architecture: C++ Engine & Python Training

- **C++ Search Engine**: The MCTS core, board logic, and batched inference manager will be written purely in C++. During self-play, this C++ engine handles the tree search and queries the GPU.
- **Python Training Pipeline**: Python will be used to manage the cumulative dataset, parse data, and train the PyTorch ResNet model. The trained weights will be exported (e.g., via TorchScript/ONNX) to be loaded by the C++ engine.

## 2. Advanced Training & Data Strategy

### 2.1 Curriculum Learning (Endgame First)
Instead of keeping all moves from early random self-play (which is mostly noise), we will use an **incremental horizon strategy**:
- **Iteration 0**: Only keep the final 1 move of each self-play game and the re-labeled moves. This trains the network on absolute tactical truths (immediate wins/losses).
- **Subsequent Iterations**: Incrementally add ~1 more move from the end of the game and the re-labeled moves per iteration. By Iteration 30, we will be keeping nearly all moves.
- **Benefit**: This acts as curriculum learning. The value network perfectly learns the endgame first, and as the horizon expands, it learns to accurately evaluate mid-game states that lead to those known endgames. The early noisy board states serve as regularizers to prevent overfitting.

### 2.2 Data Augmentation & Re-labeling Perturbation
- **Symmetries**: Every position generated will be augmented using the 8 dihedral symmetries (rotations and flips) of the Gomoku board.
- **Re-labeling Perturbation**: To quickly bootstrap tactical knowledge in early iterations, we will traverse from the endgame of self-play games all the way to just after the swap 2 opening. For each state, we run our pattern detection rules on the state to find relabeling opportunities. For those moves that we can't re-label, we use the original policy/value from the MCTS search.

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
- **ResNet Implementation**: Build the PyTorch ResNet model (6M parameters) with SE-blocks.
- **Cumulative Dataset Manager**: Implement a disk-backed storage system to store and index all self-play games from Iteration 1 to the end, preventing overfitting and ensuring tactical variety.
- **Horizon-Filtered Data Loader**: Implement a PyTorch `Dataset` that samples moves from the cumulative store according to the incremental horizon strategy (Section 2.1).
- **Iterative Training Loop**: Develop the orchestration script to manage training cycles and model exports.

### Phase 4: Data Seeding
Create the `gomoku_game_generator` C++ executable. Upon iteration=0, we perform the data seeding process instead of self play. Read curriculum.py to understand the big picture.
- Randomly play 40,000 games till end.
- Keep the last move of each game. Write the (board, policy, value) training examples according to the format specified by cumulative_dataset.py.
- Run re-labeling for the rest of the board states all the way back to just after the swap 2 opening move. The re-labeling heuristic is described in detail below.

#### Re-labeling
Re-labeling happens when we can prove the tactical truth of the endgame moves by the following simple rules executed in sequence.
1. Depth 1 win:  Find positions (our stone) that will lead to win in 1 move. Policy on those positions are 1/N and value is 1. N is the number of such positions.
2. Depth 2 defense:  Find positions (opponent's stone) that will lead to win in 1 move. Policy on those positions are 1/N. N is the number of such positions. If there are multiple such positions, it is a loss. Value is -1. Don't change the value if there is only one position.
3. Depth 3 win (open 4): Find positions (our stone) that will form an open 4 (any one side form an overline potential doesn't count). Policy on those positions are 1/N. Value is 1. N is the number of such positions.
4. Depth 4 defense (open 4): Find positions (opponent's stone) that will form an open 4 (any one side form an overline potential doesn't count). Policy on those positions are 1/N. N is the number of such positions. If there are multiple such positions, it is a loss. Value is -1. Don't change the value if there is only one position.

### Phase 5: Self-Play Data Generation & Augmentation
- **Multi-Game Orchestration** (`gomoku_game_generator` C++ executable): The program runs 12 game workers in parallel. Each worker is a self-play game (between the challenger and the champion) that uses MCTS with the neural network evaluator. The MCTS collects 16 board states at a time to form a batch to evaluate with the neural network. A total of 2000 simulations per move (roughly 60 ms per move). Having 12 workers in parallel doesn't increase the throughput by 12 but it is meant to saturate the GPU compute. For a 30-ply game, the throughput is expected to be 0.56 games per second or 2000 games per hour. It takes 4.5 hours to complete one iteration of 9000 games. Check curriculum.py for the commandline arguments for this program.
- **Data Emission**: Check cumulative_dataset.py for the format of each (board, policy, value) training example. Per section 2.1 and 2.2, trim the game by the horizon limit and relabel them when possible. Re-labeling will be attempted for the rest of the game except of the swap 2 opening. If relabeling fails, then the game record is just discarded. The horizon limit is game_len - (iteration + 1) for iteration 0..50.
- **Data Gating mechanism**: Implement a gating mechanism where the self-play games are written to a temporary folder first. If the challenger achieves the >55% win rate and is promoted, move the data into the main cumulative data_dir. Otherwise discard the generated data to the diagnostics folder so it doesn't poison the mainline dataset. Manual analysis of the diagnostics folder will be performed.
- **Game Stats**: The game generator also outputs statistics about the match between the champion and the challenger. This information is stored in a JSON file that is used to determine whether the challenger should be promoted to champion (see curriculum.py).
- **10-Day Run**: Start the iterative cycle of self-play and training using the Curriculum Learning (endgame first) strategy.

### Phase 6: Match Server Integration
- **Server Client**: Build the gomoku engine client C++ executable that communicates with the match server and plays games. If we run two of the client program, we should be able to see them play games against each other via the spectator client in the `protocol/examples/spectator.py`.
- **Communication Spec**: Adhere to the match server communication spec in `protocol/spec.md`. It is a JSON-RPC 2.0 protocol.
 

## 5. Building and Testing

To build the C++ engine and run its unit tests (which require a system installation of `googletest`), run:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ../engine
make -j$(nproc)
./gomoku_engine_test
```
