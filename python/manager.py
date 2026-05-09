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
SELF_PLAY_BIN = "../build/self_play"

def get_current_day():
    return datetime.datetime.now().strftime("%Y-%m-%d")

def run_iteration(iteration):
    horizon = 2 * iteration
    print(f"\n{'='*60}")
    print(f" ITERATION {iteration:02d} / {NUM_ITERATIONS} | Horizon: {horizon} moves")
    print(f"{'='*60}")

    # 1. Self-Play (Phase 4)
    if os.path.exists(SELF_PLAY_BIN):
        print(f"[*] Starting C++ Self-Play data generation...")
        # Example command: ./self_play --games 10000 --output data/shard_iter_01.bin
        # subprocess.run([SELF_PLAY_BIN, "--games", str(GAMES_PER_ITERATION), "--out_dir", DATA_DIR])
    else:
        print(f"[!] # TODO: SELF_PLAY_BIN ({SELF_PLAY_BIN}) not found. Skipping data generation.")

    # 2. Training (Phase 3)
    print(f"[*] Starting Python training for iteration {iteration}...")
    weights_path = os.path.join(WEIGHTS_DIR, f"model_iter_{iteration:02d}.pth")
    prev_weights = os.path.join(WEIGHTS_DIR, f"model_iter_{iteration-1:02d}.pth") if iteration > 1 else "none"
    
    cmd = [
        "python", "train.py",
        "--data_dir", DATA_DIR,
        "--model_path", weights_path,
        "--horizon", str(horizon),
        "--batch_size", "512",
        "--epochs", "10"
    ]
    
    if os.path.exists(prev_weights):
        # In a real setup, train.py would load these weights automatically if passed
        pass 

    subprocess.run(cmd)

    # 3. Export to TorchScript (Phase 2/3)
    print(f"[*] Exporting model for C++ inference...")
    # This would involve loading weights into GomokuNet and calling torch.jit.trace
    # subprocess.run(["python", "create_model.py", "--weights", weights_path, "--out", MODEL_EXPORT_PATH])

    print(f"[*] Iteration {iteration} complete.\n")

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
