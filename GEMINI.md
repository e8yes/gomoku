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

### 2.2 Data Augmentation
- **Symmetries**: Every position generated will be augmented using the 8 dihedral symmetries (rotations and flips) of the Gomoku board.

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

### Phase 4: VCF solver and MCTS enhancements
- **VCF solver**: Implement on a playground/toy folder a VCF solver. Correctness is the priority. Test against known VCF win/loss positions. It returns the first found winning sequence for the current player if one exists, otherwise it returns an empty vector.
- **VCT Solver**: TBD.
- **MCTS Noise**: Optionally add Dirichlet noise to the legal moves' prior probabilities at the root node to encourage exploration during self-play and evaluation.
- **MCTS Caching**: Caching of the MCTS tree throughout the game. Discard nodes after a move is made and the child becomes the new root.
- **Non-parallel MCTS**: The current MCTS implementation is parallel with virtual loss to prevent thread collisions. Context switching overhead is substantial. Retain the current virtual loss strategy but gather to-be-expanded nodes into a small-batch (32 nodes) for evaluation. The batch inference executor should gather small-batches and accumulate them into a larger batch of 192 inputs. Notify the `std::future<Response[]>` per small-batch but not per input. Fit the `Evaluator` interface to accept small-batch as input. Backpropagate results after the small-batch evaluation completes.
- **Evaluation Cache**: Hash the board state using Zobrist hashing to avoid redundant computations. Identical board states encountered during search should return identical policy/value vectors. Re-use evaluated nodes throughout the game.
- **MCTS with Endgame Solver**: Derive an endgame solver interface. Let the MCTS class optionally accept the solver interface from the Search() function. It should return a winning sequence for the current player if one exists, otherwise it returns an empty vector. We run the solver in a separate thread after issuing the evaluator call for the small-batch. This allows us to hide the CPU cost of solving the small-batch while the GPU is evaluating on it. If solved, we override the policy and value returned from the evaluator and update the evaluation cache. Based on the playground/toy VCF solver, implement a high-performance endgame solver on the defined interface.


### Phase 5: Data Seeding (`gomoku_game_generator` C++ executable)
This binary takes in 3 required and 1 optional arguments (see `curriculum.py`):
- --games: Number of self-play games to generate.
- --iteration: Iteration number.
- --out_dir: Directory to output the game data.

Upon iteration=0, we perform the data seeding process by search upon the `RandomEvaluator`.
- Keep the last 3 moves of each game. Write the (board, policy, value) training examples according to the format specified by `cumulative_dataset.py`.
- Test run the curriculum.py to see if the entire pipeline runs properly.

### Phase 6: Self-Play Data Generation & Augmentation (`gomoku_game_generator` C++ executable)
This binary takes in 3 required and 1 optional arguments (see `curriculum.py`):
- --games: Number of self-play games to generate.
- --iteration: Iteration number.
- --out_dir: Directory to output the game data.
- --champion_model_path: Path to the champion model (optional). If omitted, the evaluator will be the random evaluator.

- **State Space Exploration**: Enable Dirichlet noise at the root node to encourage exploration for each move in the game.
- **Multi-Game Orchestration**: The program runs 12 game workers in parallel. Each worker runs a self-play game that uses MCTS (400 simulations per move) with the neural network evaluator combined with the VCF endgame solver. Having 12 workers in parallel is meant to saturate the GPU compute. For a 30-ply game, the throughput is expected to be 0.56 games per second or 2000 games per hour. It takes 4.5 hours to complete one iteration of 9000 games.
- **Data Emission**: Check cumulative_dataset.py for the format of each (board, policy, value) training example. Per section 2.1 and 2.2, trim the game by the horizon limit. The horizon limit is game_len - (iteration + 3) for iteration 0..50.
- **10-Day Run**: Start the iterative cycle of self-play and training using the Curriculum Learning (endgame first) strategy.

### Phase 7: Evaluation (`gomoku_model_evaluator` C++ executable)
This binary takes in 4 arguments (see `curriculum.py`): 
* --games: Number of matches to evaluate upon.
* --champion_model_path: Path to the champion model.
* --challenger_model_path: Path to the challenger model.
* --out_dir: Directory to output the evaluation results.

- **Model Evaluator**: Disable prior noise after the first 4 plies. Search for 1000 simulations per move.
- **Gather Statistics**: challenger win rate (with 90% confidence interval), game length (mean/median/std/min/max).
- **Parallelism**: Like the game generator, this program runs 12 game workers in parallel to saturate the GPU.

### Phase 8: Match Server Integration
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
