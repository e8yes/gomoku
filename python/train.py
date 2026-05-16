import os
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader
from tqdm import tqdm
import argparse
import logging
import datetime
import json
from typing import Tuple

# Architecture source
from create_model import GomokuNet

# Dataset
from cumulative_dataset import GomokuDataset


def setup_logging():
    """Sets up logging to both console and a timestamped file."""
    if not os.path.exists("logs"):
        os.makedirs("logs")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join("logs", f"train_{timestamp}.log")

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[logging.FileHandler(log_file), logging.StreamHandler()],
    )
    logging.info(f"Logging initialized. File: {log_file}")


def _train_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: optim.Optimizer,
    scheduler: optim.lr_scheduler.OneCycleLR,
    device: torch.device,
    epoch: int,
    total_epochs: int,
):
    """
    Runs a single training epoch.
    """
    model.train()
    pbar = tqdm(loader, desc=f"Epoch {epoch}/{total_epochs}")
    for states, target_probs, target_values in pbar:
        states = states.to(device)
        target_probs = target_probs.to(device)
        target_values = target_values.to(device)

        optimizer.zero_grad()

        # Forward pass
        out_probs, out_values = model(states)

        # Losses
        # Policy: CrossEntropy (out_probs are logits)
        policy_loss = nn.functional.cross_entropy(out_probs, target_probs)
        # Value: MSE
        value_loss = nn.functional.mse_loss(out_values, target_values)

        total_loss = policy_loss + value_loss

        # Backward pass
        total_loss.backward()
        optimizer.step()
        scheduler.step()

        pbar.set_postfix(
            {
                "loss": f"{total_loss.item():.4f}",
                "pi": f"{policy_loss.item():.4f}",
                "v": f"{value_loss.item():.4f}",
            }
        )


def _evaluate_model(
    model: nn.Module, loader: DataLoader, device: torch.device, num_batches: int = 100
) -> Tuple[float, float]:
    """
    Computes average policy and value loss over a sample of batches.
    """
    model.eval()
    total_pi_loss = 0.0
    total_v_loss = 0.0
    num_eval_batches = min(num_batches, len(loader))

    if num_eval_batches == 0:
        return 0.0, 0.0

    logging.info(f"Evaluating on {num_eval_batches} batches...")
    with torch.no_grad():
        eval_iter = iter(loader)
        for _ in range(num_eval_batches):
            try:
                states, target_probs, target_values = next(eval_iter)
            except StopIteration:
                break

            states = states.to(device)
            target_probs = target_probs.to(device)
            target_values = target_values.to(device)

            out_probs, out_values = model(states)

            pi_loss = nn.functional.cross_entropy(out_probs, target_probs)
            v_loss = nn.functional.mse_loss(out_values, target_values)

            total_pi_loss += pi_loss.item()
            total_v_loss += v_loss.item()

    avg_pi_loss = total_pi_loss / num_eval_batches
    avg_v_loss = total_v_loss / num_eval_batches
    return avg_pi_loss, avg_v_loss


def _save_training_stats(model_path: str, policy_loss: float, value_loss: float):
    """
    Saves the evaluation losses to a JSON file alongside the model weights.
    """
    stats_path = os.path.splitext(model_path)[0] + ".json"
    with open(stats_path, "w") as f:
        json.dump({"policy_loss": policy_loss, "value_loss": value_loss}, f, indent=2)
    logging.info(f"Training stats saved to {stats_path}")


def train(
    data_dir: str,
    load_path: str = None,
    save_path: str = None,
    batch_size: int = 256,
    epochs: int = 5,
    lr: float = 1e-3,
) -> Tuple[float, float]:
    """
    Trains the Gomoku neural network using a ResNet architecture and the
    cumulative dataset generated from self-play.

    Args:
        data_dir (str): Path to the directory containing sharded binary data.
        load_path (str): Path to load model weights from (.pth).
        save_path (str): Path to save model weights to (.pth).
        batch_size (int): Number of samples per training batch.
        epochs (int): Number of full passes over the dataset.
        lr (float): Maximum learning rate for the OneCycleLR scheduler.

    Returns:
        Tuple[float, float]: Average (policy_loss, value_loss) over 100 sampled batches.
    """

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    logging.info(f"Training on {device} in pure bfloat16...")

    # 1. Load Dataset
    dataset = GomokuDataset(data_dir, augment=True)
    if len(dataset) == 0:
        logging.error(f"No data found in {data_dir}")
        return 0.0, 0.0

    loader = DataLoader(
        dataset, batch_size=batch_size, shuffle=True, num_workers=4, pin_memory=True
    )

    # 2. Initialize Model
    model = GomokuNet().to(device)

    # Load existing weights if available
    if load_path:
        if os.path.exists(load_path):
            logging.info(f"Loading weights from {load_path}")
            # Note: If weights were saved as float32, we may need to cast
            model.load_state_dict(torch.load(load_path, map_location=device))
        else:
            logging.warning(
                f"load_path {load_path} specified but not found. Starting from scratch."
            )

    # 3. Optimizer & Scheduler
    optimizer = optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    # OneCycleLR is great for AlphaZero-style training
    scheduler = optim.lr_scheduler.OneCycleLR(
        optimizer, max_lr=lr, steps_per_epoch=len(loader), epochs=epochs
    )

    # 4. Training Loop
    for epoch in range(1, epochs + 1):
        _train_epoch(
            model=model,
            loader=loader,
            optimizer=optimizer,
            scheduler=scheduler,
            device=device,
            epoch=epoch,
            total_epochs=epochs,
        )

    # 5. Save Weights
    torch.save(model.state_dict(), save_path)
    logging.info(f"Model saved to {save_path}")

    # 6. Evaluation (Sample 100 batches)
    avg_pi_loss, avg_v_loss = _evaluate_model(model, loader, device, num_batches=100)
    logging.info(
        f"Validation Loss - Policy: {avg_pi_loss:.4f}, Value: {avg_v_loss:.4f}"
    )

    # 7. Save Stats
    _save_training_stats(save_path, avg_pi_loss, avg_v_loss)

    return avg_pi_loss, avg_v_loss


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_dir", type=str, default="data")
    parser.add_argument("--model_path", type=str, default="weights.pth")
    parser.add_argument("--load_path", type=str, default=None)
    parser.add_argument("--batch_size", type=int, default=256)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--lr", type=float, default=1e-3)

    args = parser.parse_args()
    setup_logging()
    train(
        data_dir=args.data_dir,
        model_path=args.model_path,
        load_path=args.load_path,
        batch_size=args.batch_size,
        epochs=args.epochs,
        lr=args.lr,
    )
