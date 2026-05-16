import datetime
import json
import logging
import os
import shutil
import glob
import subprocess
from dataclasses import dataclass, field
from typing import Any, Dict, List

import torch
from create_model import BOARD_SIZE, NUM_INPUT_CHANNELS, GomokuNet
from train import train


@dataclass
class CurriculumConfig:
    # Data directory
    data_dir: str = "data"

    # PyTorch model path (.pth)
    weights_dir: str = "weights"

    # Torchscript location (.pt)
    model_export_path: str = "exported_models"

    # Game generator binary
    game_generator_bin: str = "./gomoku_game_generator"

    # Model evaluator binary
    model_evaluator_bin: str = "./gomoku_model_evaluator"

    # Number of iterations
    num_iterations: int = 50

    # Games per iteration
    games_per_iteration: int = 9000

    # Games per evaluation
    games_per_evaluation: int = 100

    # Minimum promotion win rate
    min_promotion_win_rate: float = 0.55

    # Training parameters
    train_params: Dict[str, Any] = field(
        default_factory=lambda: {"batch_size": 512, "epochs": 1, "lr_seed": 0.01}
    )

    @classmethod
    def load(cls, path: str):
        if not os.path.exists(path):
            return cls()
        with open(path, "r") as f:
            data = json.load(f)
        return cls(**data)

    def save(self, path: str):
        with open(path, "w") as f:
            json.dump(self.__dict__, f, indent=2)


@dataclass
class IterationSummary:
    iteration: int = 0
    policy_loss: float = 0.0
    value_loss: float = 0.0
    challenger_win_rate: float = 0.0
    promoted: bool = False


def setup_logging():
    """Sets up logging to both console and a timestamped file."""
    if not os.path.exists("logs"):
        os.makedirs("logs")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join("logs", f"training_{timestamp}.log")

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[logging.FileHandler(log_file), logging.StreamHandler()],
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
    Uses FP16 precision for maximum inference throughput on GPU.
    TODO: Support other GPU devices (ROCm, MPS, XPU, Vulkan, etc.). Do not support CPU.
    """
    assert torch.cuda.is_available(), "CUDA is not available!"
    device = torch.device("cuda")
    model = GomokuNet().to(device).half()

    # Load weights
    checkpoint = torch.load(weights_path, map_location=device)
    model.load_state_dict(checkpoint)
    model.eval()

    # Use FP16 for export as expected by the C++ engine
    dummy = torch.zeros(
        1,
        NUM_INPUT_CHANNELS,
        BOARD_SIZE,
        BOARD_SIZE,
        device=device,
        dtype=torch.float16,
    )
    traced = torch.jit.trace(model, dummy)
    traced.save(export_path)
    logging.info(f"Exported {weights_path} to {export_path}")


def find_last_champion(export_path: str) -> str:
    """Finds the last champion model."""
    assert os.path.exists(export_path), f"Export path {export_path} does not exist!"
    champion_pts = glob.glob(os.path.join(export_path, "champion*.pt"))
    if not champion_pts:
        return str()
    return max(champion_pts)


def run_iteration(iteration: int, config: CurriculumConfig) -> IterationSummary:
    """
    Executes a single iteration of the AlphaZero training loop.
    Returns: (new_champion_path, promoted, win_rate, policy_loss, value_loss)
    """
    logging.info(f"\n{'=' * 60}")
    logging.info(f" ITERATION {iteration:02d} / {config.num_iterations - 1}")
    logging.info(f"{'=' * 60}")

    # 1. Self-Play (Data Generation)
    # Iteration 0: Bootstrapping phase. No models exist yet. The generator will
    # produce a seed dataset using internal heuristics.
    # Iteration k > 0: High-quality data generation by matching the current
    # champion against itself.
    logging.info(f"[*] Starting C++ Self-Play (Iteration: {iteration})...")

    if not os.path.exists(config.data_dir):
        os.makedirs(config.data_dir)
    assert os.path.exists(config.data_dir), (
        f"Data directory {config.data_dir} does not exist!"
    )

    assert os.path.exists(config.game_generator_bin), (
        f"Game generator {config.game_generator_bin} does not exist!"
    )
    cmd = [
        config.game_generator_bin,
        "--games",
        str(config.games_per_iteration),
        "--iteration",
        str(iteration),
        "--out_dir",
        config.data_dir,
    ]

    # Only pass models if they actually exist (Iteration 0 will pass none)
    champion_pt = find_last_champion(config.model_export_path)
    if os.path.exists(champion_pt):
        cmd.extend(["--champion_model_path", champion_pt])

    subprocess.run(cmd, check=True)

    # 2. Train Model (Challenger)
    prev_challenger_pth = (
        os.path.join(config.model_export_path, f"challenger{iteration - 1:02d}.pth")
        if iteration > 0
        else None
    )
    current_challenger_pth = os.path.join(
        config.model_export_path, f"challenger{iteration:02d}.pth"
    )
    learning_rate = config.train_params["lr_seed"] * pow(0.9, iteration)
    logging.info(
        f"[*] Training challenger model from {prev_challenger_pth} to {current_challenger_pth} with learning rate {learning_rate}"
    )
    pi_loss, v_loss = train(
        data_dir=config.data_dir,
        load_path=prev_challenger_pth,
        save_path=current_challenger_pth,
        batch_size=config.train_params["batch_size"],
        epochs=config.train_params["epochs"],
        lr=learning_rate,
    )

    # Export the challenger for evaluation/production
    challenger_pt = os.path.join(
        config.model_export_path, f"challenger{iteration:02d}.pt"
    )
    export_to_torchscript(current_challenger_pth, challenger_pt)

    # 3. Evaluation (Champion vs Challenger)
    # We run evaluation games between champion and challenger.
    # For iteration 0, we auto-promote to establish the initial champion.
    logging.info("[*] Evaluating challenger promotion...")

    if iteration == 0:
        logging.info("[+] Iteration 0: Auto-promoting initial champion.")
        promoted = True
        win_rate = 1.0
    else:
        assert os.path.exists(config.model_evaluator_bin), (
            f"Model evaluator {config.model_evaluator_bin} not found!"
        )
        stats_file = os.path.join(
            config.model_export_path, f"model_stats_{iteration:02d}.json"
        )
        cmd = [
            config.model_evaluator_bin,
            "--games",
            str(config.games_per_evaluation),
            "--champion_model_path",
            champion_pt,
            "--challenger_model_path",
            challenger_pt,
            "--out_file",
            stats_file,
        ]
        subprocess.run(cmd, check=True)

        assert os.path.exists(stats_file), (
            f"Stats file {stats_file} not found! Data generation must produce this file."
        )

        with open(stats_file, "r") as f:
            stats = json.load(f)

        win_rate = float(stats["challenger_win_rate"])
        promoted = win_rate >= config.min_promotion_win_rate
        logging.info(
            f"[*] Challenger Win Rate: {win_rate:.2%} (Threshold: {config.min_promotion_win_rate:.2%})"
        )

    if promoted:
        new_champion_pt = os.path.join(
            config.model_export_path, f"champion{iteration:02d}.pt"
        )
        logging.info(f"[+] Challenger promoted! {new_champion_pt}")
        shutil.copy(challenger_pt, new_champion_pt)
    else:
        logging.info("[-] Challenger not promoted.")

    return IterationSummary(
        iteration=iteration,
        policy_loss=pi_loss,
        value_loss=v_loss,
        win_rate=win_rate,
        promoted=promoted,
    )


def log_training_summary(history: List[IterationSummary]):
    """Logs a formatted table of training progress."""
    header = f"{'iteration':<12} {'promoted':<10} {'win rate':<12} {'policy loss':<15} {'value loss':<12}"
    logging.info("\n" + "=" * 70)
    logging.info(" TRAINING PROGRESS SUMMARY")
    logging.info("-" * 70)
    logging.info(header)

    for entry in history:
        it = entry.iteration
        p = "Y" if entry.promoted else "N"
        wr = f"{entry.win_rate:.2%}" if it > 0 else "N/A"
        pi = f"{entry.policy_loss:.4f}"
        v = f"{entry.value_loss:.4f}"
        logging.info(f"{it:<12} {p:<10} {wr:<12} {pi:<15} {v:<12}")

    logging.info("=" * 70 + "\n")


def main():
    config = CurriculumConfig.load("curriculum_schedule.json")

    if not os.path.exists(config.weights_dir):
        os.makedirs(config.weights_dir)
    if not os.path.exists(config.data_dir):
        os.makedirs(config.data_dir)
    if not os.path.exists(config.diagnostics_dir):
        os.makedirs(config.diagnostics_dir)
    if not os.path.exists(config.model_export_path):
        os.makedirs(config.model_export_path)

    setup_logging()

    start_time = datetime.datetime.now()
    end_time = start_time + datetime.timedelta(days=15)

    logging.info("Gomoku AlphaZero Training Curriculum Started")
    logging.info(f"Target End Date: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")

    # Initial champion is None for the bootstrapping phase
    history = []

    for i in range(0, config.num_iterations):
        summary = run_iteration(i, config)
        history.append(summary)

        # Log the full table after each iteration
        log_training_summary(history)

        if datetime.datetime.now() > end_time:
            logging.info("Time limit reached. Stopping training.")
            break


if __name__ == "__main__":
    main()
