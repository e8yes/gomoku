# Gomoku Engine Development Plan (AlphaZero Approach)

This document outlines the development plan for our Gomoku engine with Swap2 support, competing against other agents on the `gomoku_match` server. We are utilizing an **AlphaZero-style architecture (MCTS + Neural Network)** trained over 10 days on an RTX 4060 Ti.

## 1. Architecture: C++ Engine & Python Training

- **C++ Search Engine**: The MCTS core, board logic, and batched inference manager will be written purely in C++. During self-play, this C++ engine handles the tree search and queries the GPU.
- **Python Training Pipeline**: Python will be used to manage the cumulative dataset, parse data, and train the PyTorch ResNet model. The trained weights will be exported as an AOTInductor `.pt2` package, generating optimized CUDA/Triton kernels for the C++ engine.

## 2. Advanced Training & Data Strategy

### 2.1 Adaptive Recent-Window Dataset
The incremental horizon strategy is intentionally dropped. It introduced an undesirable position-distribution bias by changing which part of each game was retained from one iteration to the next.
- **Data retention**: Keep every non-terminal position from every self-play game. There is no iteration-dependent horizon filter.
- **Post-promotion window**: After a successful promotion, train and validate uniformly on records from only the newest four iteration shards. Do not mix in the older 30% tail; it adds stale-target noise.
- **Adaptive failure window**: Expand the lookup window by one shard for every consecutive failed promotion: four after a promotion, five after one failure, six after two, and so on. A successful promotion resets it to four. If less history exists, use every available shard.
- **Champion reset**: Every challenger starts from the current champion's FP32 checkpoint. A failed challenger is never used as the initialization for the next challenger.
- **Opponent diversity**: Once two promoted champions exist, generate 80% of games as current-champion self-play and 20% against the immediately previous champion, alternating seats. In mixed games, retain only positions whose decision was made by the current champion so stale policy targets from the weaker model never enter training.
- **Validation**: Use the same adaptive shard window for validation measurements, but disable spatial augmentation.
- **Precision**: Keep model parameters, optimizer state, inputs, losses, and updates in FP32. Export the production `.pt2` package and run C++ inference in FP16.

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
- **Cumulative Dataset Manager**: Implement a disk-backed storage system that retains all self-play shards while allowing training to index only the selected adaptive recent window.
- **Adaptive Recent-Window Data Loader**: Train uniformly from the newest four shards after promotion and expand that window by one for each consecutive promotion failure, as specified in Section 2.1.
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

Upon iteration=0, we perform the data seeding process by search upon the `RandomEvaluator` with the endgame evaluator.
- Keep every non-terminal position of each game. Write the (board, policy, value) training examples according to the format specified by `cumulative_dataset.py`.
- Test run the curriculum.py to see if the entire pipeline runs properly.

### Phase 6: Self-Play Data Generation & Augmentation (`gomoku_game_generator` C++ executable)
This binary takes in 3 required and 1 optional arguments (see `curriculum.py`):
- --games: Number of self-play games to generate.
- --iteration: Iteration number.
- --out_dir: Directory to output the game data.
- --champion_model_path: Path to the champion model (optional). If omitted, the evaluator will be the random evaluator.

- **State Space Exploration**: During self-play, enable Dirichlet root noise and visit-proportional action sampling for the first six decision plies. After that opening window, disable root noise and select the highest-visit action. When exploration mode is configured, randomize exact PUCT ties so disabling root noise cannot reintroduce row-major edge bias.
- **Champion-led generation**: After bootstrap, 80% of games use current-champion self-play. The remaining 30% use the immediately previous promoted champion as an opponent with seats alternated, while emitting only the current champion's decision positions. Unpromoted challengers never generate data.
- **Endgame solver warm-up**: Disable both the attacker-side VCF solver and defensive VCF callback for iterations 0 through 19 while the bootstrap model learns the general policy. Enable both callbacks from iteration 20 onward; this threshold is controlled by `endgame_solver_start_iteration` in the curriculum schedule.
- **Multi-Game Orchestration**: The program runs 12 game workers in parallel. Each worker runs a self-play game that uses MCTS (800 simulations per move) with the neural network evaluator combined with the VCF endgame solver.
- **Data Emission**: Check cumulative_dataset.py for the format of each (board, policy, value) training example. Emit every non-terminal position; the Python loader selects the adaptive recent iteration window.
- **Learning-rate schedule**: Start at the configured FP32 learning rate and decay it by a factor of 0.93 per curriculum iteration.
- **Contest Run**: Run 50 iterations using the adaptive recent-window strategy.

### Phase 7: Evaluation (`gomoku_model_evaluator` C++ executable)
This binary takes in 4 arguments (see `curriculum.py`): 
* --games: Number of matches to evaluate upon.
* --champion_model_path: Path to the champion model.
* --challenger_model_path: Path to the challenger model.
* --out_dir: Directory to output the evaluation results.

- **Model Evaluator**: Alternate champion and challenger seats exactly evenly across 200 games. Use the same 800 simulations per move as self-play generation so the gate measures which model will generate stronger data under the production generation budget. Disable prior noise after the first 4 plies.
- **Gather Statistics**: challenger win rate (with 90% confidence interval), game length (mean/median/std/min/max).
- **Parallelism**: Like the game generator, this program runs 12 game workers in parallel to saturate the GPU.

### Phase 8: Match Server Integration
- **Server Client**: Build the gomoku engine client C++ executable that communicates with the match server and plays games. If we run two of the client program, we should be able to see them play games against each other via the spectator client in the `protocol/examples/spectator.py`.
- **Communication Spec**: Adhere to the match server communication spec in `protocol/spec.md`. It is a JSON-RPC 2.0 protocol.
- **Native implementation**: `gomoku_match_client` now provides a portable
  JSON-RPC 2.0 JSON-Lines client. It supports handshake/register, optional
  admin-created pairing with explicit seat selection, complete Swap2 action handling, server-state replay,
  persistent, deadline-bounded MCTS, optional root noise, AOTInductor model
  inference, and the existing VCF attacker/defender callbacks. The
  `--deadline-ms` option controls the admin-created match clock; search uses
  that duration by default, stopping between simulation batches and reserving
  time for move submission. `--simulations N` explicitly opts into a fixed
  simulation-count search. Omitting `--model` selects the `RandomEvaluator`
  for protocol smoke tests.
- **Validation**: The JSON codec has gtests, the engine test suite passes, and
  two native clients have been verified against the Python TCP server through
  a complete Swap2 match ending in `game_finished`. The Python spectator and
  observer path remain protocol-compatible and are covered by the existing
  protocol test suite.
 

## 5. Building, Testing & Running the Curriculum

### 5.1 Building the C++ Engine

Building the engine automatically places the required binaries (`gomoku_game_generator` and `gomoku_model_evaluator`) into the `python/` directory via CMake `POST_BUILD` steps:

```bash
mkdir -p build && cd build
# If using a virtual environment with PyTorch:
cmake -DCMAKE_PREFIX_PATH="../venv/lib/python3.10/site-packages/torch" -DCMAKE_BUILD_TYPE=Release ../engine
# Or with standard system PyTorch:
# cmake -DCMAKE_BUILD_TYPE=Release ../engine
make -j$(nproc)
```

### 5.2 Running C++ & Python Unit Tests

Run the native C++ unit tests:

```bash
./build/gomoku_engine_test
```

Run Python test suites (using virtualenv):

```bash
PYTHONPATH=protocol/python:python ./venv/bin/python3 -m unittest discover -s python
PYTHONPATH=protocol/python:python ./venv/bin/python3 -m unittest discover -s protocol/tests/python
```

### 5.3 Launching the Learning Curriculum

The Python training curriculum orchestrates self-play data generation (via `gomoku_game_generator`), ResNet model training, FP32 weight checkpoints, AOTInductor `.pt2` compilation, and champion-challenger promotion gating (via `gomoku_model_evaluator`).

Ensure the binaries have been compiled and copied into `python/` (or run `make -C build`), then launch the curriculum:

```bash
cd python
../venv/bin/python3 curriculum.py --schedule curriculum_schedule.json
```

Key curriculum flags and settings in `curriculum_schedule.json`:
- `game_generator_bin`: `./gomoku_game_generator` (auto-copied post-build)
- `model_evaluator_bin`: `./gomoku_model_evaluator` (auto-copied post-build)
- `games_per_iteration`: Number of self-play games per iteration (default: 10,000)
- `simulations_per_move`: MCTS search budget (default: 800)
- `games_per_evaluation`: Balanced champion-vs-challenger promotion evaluation matches (default: 200)
- `endgame_solver_start_iteration`: Iteration threshold to activate VCF solver callbacks (default: 20)

### 5.4 Running the Native Match Client

To connect to a running `gomoku_match` referee server:

```bash
# Terminal 1: Start TCP Match Server
PYTHONPATH=protocol/python ./venv/bin/python3 -m gomoku_match --listen tcp://127.0.0.1:7901 --admin-token phase8

# Terminal 2: Register Player 1 (Bob)
./build/gomoku_match_client --name bob --model exported_models/champion36.pt2

# Terminal 3: Register Player 2 (Alice) & Request Match against Bob
./build/gomoku_match_client --name alice --opponent bob --admin-token phase8 \
  --model exported_models/champion36.pt2
```

