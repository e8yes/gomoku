import os
import subprocess
import datetime
import json
import torch
import shutil
import logging
from dataclasses import dataclass, field
from typing import Dict, Any, List, Optional

# Architecture source
from create_model import GomokuNet, NUM_INPUT_CHANNELS, BOARD_SIZE
from train import train

@dataclass
class CurriculumConfig:
    data_dir: str = "data"
    weights_dir: str = "weights"
    model_export_path: str = "exported_models"
    game_generator_bin: str = "./gomoku_game_generator"
    num_iterations: int = 50
    games_per_iteration: int = 9000
    min_promotion_win_rate: float = 0.55
    diagnostics_dir: str = "diagnostics"
    train_params: Dict[str, Any] = field(default_factory=lambda: {
        "batch_size": 512,
        "epochs": 1,
        "lr": 0.002
    })

    @classmethod
    def load(cls, path: str):
        if not os.path.exists(path):
            return cls()
        with open(path, 'r') as f:
            data = json.load(f)
        return cls(**data)

    def save(self, path: str):
        with open(path, 'w') as f:
            json.dump(self.__dict__, f, indent=2)

def setup_logging():
    """Sets up logging to both console and a timestamped file."""
    if not os.path.exists("logs"):
        os.makedirs("logs")
    
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join("logs", f"training_{timestamp}.log")
    
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler()
        ]
    )
    logging.info(f"Logging initialized. File: {log_file}")

def get_python_bin():
    """Detects the virtual environment python binary."""
    for venv_path in [".venv", "venv"]:
        path = os.path.join("..", venv_path, "bin", "python")
        if os.path.exists(path):
            return path
    return "python3"

def export_to_torchscript(weights_path: str, export_path: str):
    """
    Converts a .pth weight file to a TorchScript .pt file for C++ inference.
    Uses FP16 precision for maximum inference throughput on RTX 4060 Ti.
    """
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = GomokuNet().to(device)
    
    # Load weights
    checkpoint = torch.load(weights_path, map_location=device)
    model.load_state_dict(checkpoint)
    model.eval()

    # Use FP16 for export as expected by the C++ engine
    if torch.cuda.is_available():
        model = model.half()
        dummy = torch.zeros(
            1, NUM_INPUT_CHANNELS, BOARD_SIZE, BOARD_SIZE,
            device=device, dtype=torch.float16
        )
    else:
        dummy = torch.zeros(
            1, NUM_INPUT_CHANNELS, BOARD_SIZE, BOARD_SIZE,
            device=device, dtype=torch.float32
        )

    traced = torch.jit.trace(model, dummy)
    traced.save(export_path)
    logging.info(f"Exported {weights_path} to {export_path}")

def run_iteration(iteration: int, config: CurriculumConfig, champion_path: str):
    """
    Executes a single iteration of the AlphaZero training loop.
    Returns: (new_champion_path, promoted, win_rate, policy_loss, value_loss)
    """
    logging.info(f"\n{'='*60}")
    logging.info(f" ITERATION {iteration:02d} / {config.num_iterations - 1}")
    logging.info(f"{'='*60}")

    challenger_path = os.path.join(config.weights_dir, f"model_iter_{iteration:02d}.pth")

    # 1. Self-Play (Data Generation)
    # Iteration 0: Bootstrapping phase. No models exist yet. The generator will
    # produce a seed dataset using internal heuristics or random play.
    # Iteration k > 0: High-quality data generation by matching the current 
    # champion against the challenger from iteration k-1.
    logging.info(f"[*] Starting C++ Self-Play (Iteration: {iteration})...")
    
    champion_pt = os.path.join(config.model_export_path, "champion.pt")
    prev_challenger_pt = os.path.join(config.model_export_path, f"challenger{iteration-1:02d}.pt") if iteration > 0 else None

    temp_data_dir = os.path.join(config.data_dir, f"temp_iter_{iteration:02d}")
    if not os.path.exists(temp_data_dir):
        os.makedirs(temp_data_dir)

    if os.path.exists(config.game_generator_bin):
        cmd = [
            config.game_generator_bin,
            "--games", str(config.games_per_iteration),
            "--iteration", str(iteration),
            "--out_dir", temp_data_dir
        ]
        
        # Only pass models if they actually exist (Iteration 0 will pass neither)
        if os.path.exists(champion_pt):
            cmd.extend(["--champion_model_path", champion_pt])
        if prev_challenger_pt and os.path.exists(prev_challenger_pt):
            cmd.extend(["--challenger_model_path", prev_challenger_pt])
            
        subprocess.run(cmd, check=True)
    else:
        logging.warning(f"{config.game_generator_bin} not found. Skipping data generation.")

    # Special case for Iteration 0: The seed data must be available for training immediately.
    if iteration == 0:
        logging.info("[*] Iteration 0: Moving seed data to cumulative data_dir.")
        for f in os.listdir(temp_data_dir):
            shutil.move(os.path.join(temp_data_dir, f), os.path.join(config.data_dir, f))

    # 2. Train Model (Challenger)
    logging.info(f"[*] Training challenger model: {challenger_path}")
    
    # Call the training function directly instead of launching a new process
    pi_loss, v_loss = train(
        data_dir=config.data_dir,
        model_path=challenger_path,
        load_path=champion_path if (champion_path and os.path.exists(champion_path)) else None,
        batch_size=config.train_params["batch_size"],
        epochs=config.train_params["epochs"],
        lr=config.train_params["lr"]
    )

    # Export the challenger for evaluation/production
    challenger_export = os.path.join(config.model_export_path, f"challenger{iteration:02d}.pt")
    export_to_torchscript(challenger_path, challenger_export)

    # 3. Evaluation (Champion vs Challenger)
    # The game generator outputs statistics about the match between champion and challenger.
    # For iteration 0, we auto-promote to establish the initial champion.
    logging.info(f"[*] Evaluating challenger promotion...")
    
    if iteration == 0:
        logging.info(f"[+] Iteration 0: Auto-promoting initial champion.")
        promoted = True
        win_rate = 1.0
    else:
        stats_file = os.path.join(temp_data_dir, f"game_stats_{iteration:02d}.json")
        if not os.path.exists(stats_file):
            # If not in temp, maybe it was iteration 0 and moved?
            stats_file = os.path.join(config.data_dir, f"game_stats_{iteration:02d}.json")
            
        assert os.path.exists(stats_file), f"Stats file {stats_file} not found! Data generation must produce this file."
        
        with open(stats_file, 'r') as f:
            stats = json.load(f)
        
        win_rate = float(stats["challenger_win_rate"])
        promoted = win_rate >= config.min_promotion_win_rate
        logging.info(f"[*] Challenger Win Rate: {win_rate:.2%} (Threshold: {config.min_promotion_win_rate:.2%})")

    # Data Gating: Move data based on promotion result
    if iteration > 0:
        target_dir = config.data_dir if promoted else config.diagnostics_dir
        if not os.path.exists(target_dir):
            os.makedirs(target_dir)
        
        logging.info(f"[*] Gating data for iteration {iteration} to {target_dir}")
        for f in os.listdir(temp_data_dir):
            shutil.move(os.path.join(temp_data_dir, f), os.path.join(target_dir, f))
    
    # Clean up temp dir if it's empty
    if os.path.exists(temp_data_dir) and not os.listdir(temp_data_dir):
        os.rmdir(temp_data_dir)

    if promoted:
        logging.info(f"[+] Challenger PROMOTED to Champion!")
        return challenger_path, True, win_rate, pi_loss, v_loss
    else:
        logging.info(f"[-] Challenger failed to beat threshold. Retaining current Champion.")
        return champion_path, False, win_rate, pi_loss, v_loss

def log_training_summary(history: List[Dict[str, Any]]):
    """Logs a formatted table of training progress."""
    header = f"{'iteration':<12} {'win rate':<12} {'policy loss':<15} {'value loss':<12}"
    logging.info("\n" + "="*60)
    logging.info(" TRAINING PROGRESS SUMMARY")
    logging.info("-" * 60)
    logging.info(header)
    
    for entry in history:
        it = entry["iteration"]
        wr = f"{entry['win_rate']:.2f}" if entry["win_rate"] is not None and it > 0 else "N/A"
        pi = f"{entry['policy_loss']:.4f}"
        v = f"{entry['value_loss']:.4f}"
        logging.info(f"{it:<12} {wr:<12} {pi:<15} {v:<12}")
    
    logging.info("="*60 + "\n")

def main():
    config = CurriculumConfig.load("curriculum_schedule.json")
    
    if not os.path.exists(config.weights_dir): os.makedirs(config.weights_dir)
    if not os.path.exists(config.data_dir): os.makedirs(config.data_dir)
    if not os.path.exists(config.diagnostics_dir): os.makedirs(config.diagnostics_dir)
    if not os.path.exists(config.model_export_path): os.makedirs(config.model_export_path)

    setup_logging()

    start_time = datetime.datetime.now()
    end_time = start_time + datetime.timedelta(days=15)
    
    logging.info(f"Gomoku AlphaZero Training Manager Started")
    logging.info(f"Target End Date: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")

    # Initial champion is None for the bootstrapping phase
    champion_path = None 
    history = []

    for i in range(0, config.num_iterations):
        champion_path, promoted, win_rate, pi_loss, v_loss = run_iteration(i, config, champion_path)
        
        history.append({
            "iteration": i,
            "win_rate": win_rate,
            "policy_loss": pi_loss,
            "value_loss": v_loss
        })

        # Periodically update the production champion
        if promoted:
            champion_export = os.path.join(config.model_export_path, "champion.pt")
            challenger_export = os.path.join(config.model_export_path, f"challenger{i:02d}.pt")
            shutil.copy2(challenger_export, champion_export)
            logging.info(f"[*] Updated production champion: {champion_export}")

        # Log the full table after each iteration
        log_training_summary(history)

        if datetime.datetime.now() > end_time:
            logging.info("Time limit reached. Stopping training.")
            break

if __name__ == "__main__":
    main()
