import datetime
import argparse
import json
import logging
import math
import os
import shutil
import glob
import subprocess
from dataclasses import dataclass, field
from typing import Any, Dict, List

import torch
from create_model import BOARD_SIZE, NUM_INPUT_CHANNELS, GomokuNet, compile_aoti_model
from train import train


@dataclass
class CurriculumConfig:
    # Data directory
    data_dir: str = "data"

    # Diagnostic output directory
    diagnostics_dir: str = "diagnostics"

    # PyTorch model path (.pth)
    weights_dir: str = "weights"

    # AOTInductor/Triton model package location (.pt2)
    model_export_path: str = "exported_models"

    # Game generator binary
    game_generator_bin: str = "./gomoku_game_generator"

    # Model evaluator binary
    model_evaluator_bin: str = "./gomoku_model_evaluator"

    # Number of iterations
    start_iteration: int = 0
    num_iterations: int = 50

    # Games per iteration
    games_per_iteration: int = 10000

    # Fraction of games played against the previous promoted champion. Only
    # the current champion's decision positions are retained from these games.
    previous_champion_mix_fraction: float = 0.20

    # MCTS simulations per move during self-play and promotion evaluation.
    simulations_per_move: int = 800

    # Games per evaluation
    games_per_evaluation: int = 200

    # Keep the VCF attacker/defender callbacks out of the early bootstrap
    # curriculum. This threshold is controlled by the schedule rather than
    # hard-coded in the C++ executables.
    endgame_solver_start_iteration: int = 20

    # Use a compact recent window after promotion, expanding it by one shard
    # for every consecutive failed challenger.
    base_training_window_iterations: int = 4

    # Minimum promotion win rate
    min_promotion_win_rate: float = 0.55

    # Training parameters
    train_params: Dict[str, Any] = field(
        default_factory=lambda: {
            "batch_size": 512,
            "epochs": 1,
            "lr_seed": 0.01,
            "lr_decay": 0.93,
        }
    )

    @classmethod
    def load(cls, path: str):
        if not os.path.exists(path):
            return cls()
        with open(path, "r") as f:
            data = json.load(f)
        config = cls(**data)
        if config.games_per_evaluation <= 0 or config.games_per_evaluation % 2:
            raise ValueError(
                "games_per_evaluation must be a positive even number so seats "
                "are balanced"
            )
        if config.base_training_window_iterations <= 0:
            raise ValueError("base_training_window_iterations must be positive")
        if not 0.0 <= config.previous_champion_mix_fraction <= 1.0:
            raise ValueError(
                "previous_champion_mix_fraction must be between zero and one"
            )
        return config

    def save(self, path: str):
        with open(path, "w") as f:
            json.dump(self.__dict__, f, indent=2)


@dataclass
class IterationSummary:
    iteration: int = 0
    policy_loss: float = 0.0
    value_loss: float = 0.0
    win_rate: float = 0.0
    promoted: bool = False
    training_window: int = 0


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


def export_to_aoti(weights_path: str, export_path: str):
    """
    Converts a .pth weight file to an AOTInductor .pt2 package for C++
    inference. AOTInductor generates optimized CUDA/Triton kernels while
    retaining a C++ package-loading interface.

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

    # Use an example batch larger than one so Dynamo does not specialize the
    # exported batch dimension to the constant 1.
    export_dummy = torch.zeros(
        2,
        NUM_INPUT_CHANNELS,
        BOARD_SIZE,
        BOARD_SIZE,
        device=device,
        dtype=torch.float16,
    )
    compile_aoti_model(model, export_dummy, export_path)
    logging.info(f"Exported {weights_path} to AOTInductor package {export_path}")


def find_last_champion(export_path: str) -> str:
    """Finds the last champion model."""
    assert os.path.exists(export_path), f"Export path {export_path} does not exist!"
    champion_pts = glob.glob(os.path.join(export_path, "champion*.pt2"))
    if not champion_pts:
        return str()

    # Find the champion with the highest iteration number.
    return max(
        champion_pts,
        key=model_iteration,
    )


def find_previous_champion(export_path: str) -> str:
    """Find the promoted champion immediately preceding the current one."""
    champion_pts = sorted(
        glob.glob(os.path.join(export_path, "champion*.pt2")),
        key=model_iteration,
    )
    return champion_pts[-2] if len(champion_pts) >= 2 else str()


def model_iteration(path: str) -> int:
    """Extract the numeric curriculum iteration from a model filename."""
    stem = os.path.splitext(os.path.basename(path))[0]
    digits = "".join(filter(str.isdigit, stem))
    if not digits:
        raise ValueError(f"Model path has no iteration number: {path}")
    return int(digits)


def find_champion_training_weights(export_path: str, champion_pt: str) -> str:
    """Find the FP32 checkpoint corresponding to the deployed champion."""
    if not champion_pt:
        return str()
    iteration = model_iteration(champion_pt)
    champion_pth = os.path.join(export_path, f"champion{iteration:02d}.pth")
    if os.path.exists(champion_pth):
        return champion_pth

    # Compatibility with runs created before champion FP32 checkpoints were
    # copied explicitly: the promoted challenger has identical weights.
    challenger_pth = os.path.join(export_path, f"challenger{iteration:02d}.pth")
    if os.path.exists(challenger_pth):
        return challenger_pth
    raise FileNotFoundError(
        f"No FP32 checkpoint found for champion iteration {iteration}"
    )


def adaptive_training_window(
    iteration: int, champion_pt: str, base_window: int
) -> int:
    """Expand the recent-shard window once per prior promotion failure."""
    if base_window <= 0:
        raise ValueError("base_window must be positive")
    if not champion_pt:
        return base_window
    champion_iteration = model_iteration(champion_pt)
    failures_since_promotion = max(0, iteration - champion_iteration - 1)
    return base_window + failures_since_promotion


def discounted_learning_rate(
    scheduled_learning_rate: float, base_window: int, selected_shards: int
) -> float:
    """Discount LR when failure recovery expands the sampled shard window."""
    if scheduled_learning_rate <= 0.0:
        raise ValueError("scheduled_learning_rate must be positive")
    if base_window <= 0:
        raise ValueError("base_window must be positive")
    if selected_shards < 0:
        raise ValueError("selected_shards must not be negative")
    if selected_shards <= base_window:
        return scheduled_learning_rate
    return scheduled_learning_rate * math.sqrt(base_window / selected_shards)


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
        "--simulations",
        str(config.simulations_per_move),
        "--out_dir",
        config.data_dir,
    ]

    use_endgame_solver = iteration >= config.endgame_solver_start_iteration
    logging.info(
        "[*] Endgame solver: %s (enabled from iteration %d)",
        "enabled" if use_endgame_solver else "disabled",
        config.endgame_solver_start_iteration,
    )
    if not use_endgame_solver:
        cmd.append("--disable_endgame_solver")

    # Only pass models if they actually exist (Iteration 0 will pass none)
    champion_pt = find_last_champion(config.model_export_path)
    if os.path.exists(champion_pt):
        logging.info("[*] Self-play model: current champion %s", champion_pt)
        cmd.extend(["--champion_model_path", champion_pt])
        previous_champion_pt = find_previous_champion(config.model_export_path)
        if previous_champion_pt and config.previous_champion_mix_fraction > 0.0:
            logging.info(
                "[*] Previous-champion mix: %.1f%% against %s; retaining only "
                "current-champion positions",
                config.previous_champion_mix_fraction * 100.0,
                previous_champion_pt,
            )
            cmd.extend(
                [
                    "--previous_champion_model_path",
                    previous_champion_pt,
                    "--previous_champion_mix_fraction",
                    str(config.previous_champion_mix_fraction),
                ]
            )
        else:
            logging.info("[*] Previous-champion mix unavailable")
    else:
        logging.info("[*] Self-play model: RandomEvaluator bootstrap")

    subprocess.run(cmd, check=True)

    # 2. Train Model (Challenger)
    champion_pth = find_champion_training_weights(
        config.model_export_path, champion_pt
    )
    training_window = adaptive_training_window(
        iteration, champion_pt, config.base_training_window_iterations
    )
    current_challenger_pth = os.path.join(
        config.model_export_path, f"challenger{iteration:02d}.pth"
    )
    scheduled_learning_rate = config.train_params["lr_seed"] * pow(
        config.train_params["lr_decay"], iteration
    )
    learning_rate = discounted_learning_rate(
        scheduled_learning_rate,
        config.base_training_window_iterations,
        training_window,
    )
    logging.info(
        "[*] Training challenger from current champion %s to %s with learning "
        "rate %s (scheduled: %s) using the newest %d iteration shards",
        champion_pth or "<fresh initialization>",
        current_challenger_pth,
        learning_rate,
        scheduled_learning_rate,
        training_window,
    )
    pi_loss, v_loss = train(
        data_dir=config.data_dir,
        load_path=champion_pth or None,
        save_path=current_challenger_pth,
        batch_size=config.train_params["batch_size"],
        epochs=config.train_params["epochs"],
        lr=learning_rate,
        iteration_window=training_window,
    )

    # Export the challenger for evaluation/production
    challenger_pt = os.path.join(
        config.model_export_path, f"challenger{iteration:02d}.pt2"
    )
    export_to_aoti(current_challenger_pth, challenger_pt)

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
        evaluation_dir = os.path.join(
            config.diagnostics_dir, f"evaluation_{iteration:02d}"
        )
        stats_file = os.path.join(evaluation_dir, "evaluation.json")
        cmd = [
            config.model_evaluator_bin,
            "--games",
            str(config.games_per_evaluation),
            "--simulations",
            str(config.simulations_per_move),
            "--champion_model_path",
            champion_pt,
            "--challenger_model_path",
            challenger_pt,
            "--out_dir",
            evaluation_dir,
        ]
        if not use_endgame_solver:
            cmd.append("--disable_endgame_solver")
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
            config.model_export_path, f"champion{iteration:02d}.pt2"
        )
        new_champion_pth = os.path.join(
            config.model_export_path, f"champion{iteration:02d}.pth"
        )
        logging.info(f"[+] Challenger promoted! {new_champion_pt}")
        shutil.copy(challenger_pt, new_champion_pt)
        shutil.copy(current_challenger_pth, new_champion_pth)
    else:
        logging.info("[-] Challenger not promoted.")

    return IterationSummary(
        iteration=iteration,
        policy_loss=pi_loss,
        value_loss=v_loss,
        win_rate=win_rate,
        promoted=promoted,
        training_window=training_window,
    )


def log_training_summary(history: List[IterationSummary]):
    """Logs a formatted table of training progress."""
    header = f"{'iteration':<12} {'promoted':<10} {'win rate':<12} {'window':<9} {'policy loss':<15} {'value loss':<12}"
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
        logging.info(
            f"{it:<12} {p:<10} {wr:<12} {entry.training_window:<9} "
            f"{pi:<15} {v:<12}"
        )

    logging.info("=" * 70 + "\n")


def load_prior_history(
    config: CurriculumConfig, start_iteration: int
) -> List[IterationSummary]:
    """Reconstructs IterationSummary records for already completed iterations."""
    history = []
    for it in range(start_iteration):
        challenger_json = os.path.join(
            config.model_export_path, f"challenger{it:02d}.json"
        )
        if not os.path.exists(challenger_json):
            continue

        try:
            with open(challenger_json, "r") as f:
                stats = json.load(f)
            pi_loss = float(stats.get("policy_loss", 0.0))
            v_loss = float(stats.get("value_loss", 0.0))
        except Exception:
            pi_loss, v_loss = 0.0, 0.0

        champion_pt = os.path.join(
            config.model_export_path, f"champion{it:02d}.pt2"
        )
        promoted = os.path.exists(champion_pt)

        if it == 0:
            win_rate = 1.0
        else:
            eval_json = os.path.join(
                config.diagnostics_dir, f"evaluation_{it:02d}", "evaluation.json"
            )
            win_rate = 0.0
            if os.path.exists(eval_json):
                try:
                    with open(eval_json, "r") as f:
                        eval_stats = json.load(f)
                    win_rate = float(eval_stats.get("challenger_win_rate", 0.0))
                except Exception:
                    win_rate = 0.0

        prior_champions = [
            p
            for p in glob.glob(os.path.join(config.model_export_path, "champion*.pt2"))
            if model_iteration(p) < it
        ]
        last_champ = (
            max(prior_champions, key=model_iteration)
            if prior_champions
            else ""
        )
        training_window = adaptive_training_window(
            it, last_champ, config.base_training_window_iterations
        )

        history.append(
            IterationSummary(
                iteration=it,
                policy_loss=pi_loss,
                value_loss=v_loss,
                win_rate=win_rate,
                promoted=promoted,
                training_window=training_window,
            )
        )
    return history


def main():
    parser = argparse.ArgumentParser(description="Run the Gomoku curriculum.")
    parser.add_argument(
        "--schedule",
        default="curriculum_schedule.json",
        help="Path to the curriculum JSON schedule (outputs remain relative to the current working directory).",
    )
    parser.add_argument(
        "--start_iteration",
        "--start-iteration",
        dest="start_iteration",
        type=int,
        default=None,
        help="Override the start iteration from the schedule.",
    )
    args = parser.parse_args()
    config = CurriculumConfig.load(args.schedule)
    if args.start_iteration is not None:
        config.start_iteration = args.start_iteration

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
    logging.info(f"Starting Iteration: {config.start_iteration}")
    logging.info(f"Ending Iteration: {config.num_iterations - 1}")

    # Load prior history for resumed runs
    history = load_prior_history(config, config.start_iteration)
    if history:
        logging.info(f"Loaded history for {len(history)} prior iteration(s):")
        log_training_summary(history)

    for i in range(config.start_iteration, config.num_iterations):
        summary = run_iteration(i, config)
        history.append(summary)

        # Log the full table after each iteration
        log_training_summary(history)

        if datetime.datetime.now() > end_time:
            logging.info("Time limit reached. Stopping training.")
            break


if __name__ == "__main__":
    main()

