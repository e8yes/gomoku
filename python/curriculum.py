import os
import subprocess
import datetime
import time

# Configuration
DATA_DIR = "data"
WEIGHTS_DIR = "weights"
MODEL_EXPORT_PATH = "../model.pt"
NUM_ITERATIONS = 15
GAMES_PER_ITERATION = 10000

# # TODO: Point this to the C++ self-play executable once Phase 4 is done.
GAME_GENERATOR_BIN = "./gomoku_game_generator"

def get_current_day():
    return datetime.datetime.now().strftime("%Y-%m-%d")

def run_iteration(iteration: int):
    """
    Executes a single iteration of the AlphaZero training loop, including
    self-play data generation, model training, and TorchScript export.
    """
    print(f"\n{'='*60}")
    print(f" ITERATION {iteration:02d} / {NUM_ITERATIONS}")
    print(f"{'='*60}")

    # TODO: 1. Self-Play (Phase 4)
    assert os.path.exists(GAME_GENERATOR_BIN), f"[!] {GAME_GENERATOR_BIN} not found. Skipping data generation."
    print(f"[*] Starting C++ Self-Play data generation...")
    # Example command: ./gomoku_game_generator      \
    #   --champion_model=...champion.pt             \
    #   --challenger_model=...iteration_01.pt       \
    #   --iteration_number 2                        \
    #   --time_limit_seconds 17280                  \
    #   --output data/shard_iter_02.bin             \
    #   --game_stats data/game_stats_iter_02.json
    # subprocess.run([GAME_GENERATOR_BIN, "--games", str(GAMES_PER_ITERATION), "--out_dir", DATA_DIR])

    # Detect virtual environment
    python_bin = "python3"
    for venv_path in [".venv", "venv"]:
        path = os.path.join("..", venv_path, "bin", "python")
        if os.path.exists(path):
            python_bin = path
            break

    # 2. Train Model
    print(f"[*] Starting Python training for iteration {iteration} using {python_bin}...")
    weights_path = os.path.join(WEIGHTS_DIR, f"model_iter_{iteration:02d}.pth")
    prev_weights = os.path.join(WEIGHTS_DIR, f"model_iter_{iteration-1:02d}.pth") if iteration > 1 else None

    cmd = [
        python_bin, "train.py",
        "--data_dir", DATA_DIR,
        "--model_path", weights_path,
        "--batch_size", "512",
        "--epochs", "10"
    ]
    
    if prev_weights and os.path.exists(prev_weights):
        cmd.extend(["--load_path", prev_weights])

    subprocess.run(cmd, check=True)
    print(f"[*] Iteration {iteration} complete.\n")

    # 3. Load game_stats_iter_02.json and decide whether to promote the
    # challenger to champion.

def main():
    if not os.path.exists(WEIGHTS_DIR): os.makedirs(WEIGHTS_DIR)
    if not os.path.exists(DATA_DIR): os.makedirs(DATA_DIR)

    start_time = datetime.datetime.now()
    end_time = start_time + datetime.timedelta(days=15)
    
    print(f"Gomoku AlphaZero Training Manager Started")
    print(f"Target End Date: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")

    for i in range(1, NUM_ITERATIONS + 1):
        run_iteration(i)
        
        # In a 15-day run, we might want to check the clock here
        if datetime.datetime.now() > end_time:
            print("Time limit reached. Stopping training.")
            break

if __name__ == "__main__":
    main()
