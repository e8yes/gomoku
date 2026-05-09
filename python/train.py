import os
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader
from tqdm import tqdm
import argparse

# Architecture source
from create_model import GomokuNet, NUM_INPUT_CHANNELS, BOARD_SIZE, NUM_ACTIONS

# Dataset
from cumulative_dataset import GomokuDataset

def _train_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: optim.Optimizer,
    scheduler: optim.lr_scheduler.OneCycleLR,
    device: torch.device,
    epoch: int,
    total_epochs: int
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

        pbar.set_postfix({
            "loss": f"{total_loss.item():.4f}",
            "pi": f"{policy_loss.item():.4f}",
            "v": f"{value_loss.item():.4f}"
        })

def train(
    data_dir: str,
    model_path: str,
    load_path: str = None,
    batch_size: int = 256,
    epochs: int = 5,
    lr: float = 1e-3,
):
    """
    Trains the Gomoku neural network using a ResNet architecture and the 
    cumulative dataset generated from self-play.

    Args:
        data_dir (str): Path to the directory containing sharded binary data.
        model_path (str): Path to save/load the model weights (.pth).
        batch_size (int): Number of samples per training batch.
        epochs (int): Number of full passes over the dataset.
        lr (float): Maximum learning rate for the OneCycleLR scheduler.
    """

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device} in pure bfloat16...")

    # 1. Load Dataset
    dataset = GomokuDataset(data_dir, augment=True)
    if len(dataset) == 0:
        print("Error: No data found in", data_dir)
        return

    loader = DataLoader(
        dataset, 
        batch_size=batch_size, 
        shuffle=True, 
        num_workers=4, 
        pin_memory=True
    )

    # 2. Initialize Model
    model = GomokuNet().to(device).to(torch.bfloat16)
    
    # Load existing weights if available
    effective_load_path = load_path if load_path else model_path
    if effective_load_path and os.path.exists(effective_load_path):
        print(f"Loading weights from {effective_load_path}")
        # Note: If weights were saved as float32, we may need to cast
        model.load_state_dict(torch.load(effective_load_path, map_location=device))
    elif load_path:
        print(f"Warning: load_path {load_path} specified but not found.")

    # 3. Optimizer & Scheduler
    optimizer = optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    # OneCycleLR is great for AlphaZero-style training
    scheduler = optim.lr_scheduler.OneCycleLR(
        optimizer, 
        max_lr=lr, 
        steps_per_epoch=len(loader), 
        epochs=epochs
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
            total_epochs=epochs
        )

    # 5. Save Weights
    torch.save(model.state_dict(), model_path)
    print(f"Model saved to {model_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_dir", type=str, default="data")
    parser.add_argument("--model_path", type=str, default="weights.pth")
    parser.add_argument("--load_path", type=str, default=None)
    parser.add_argument("--batch_size", type=int, default=256)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--lr", type=float, default=1e-3)
    
    args = parser.parse_args()
    train(
        data_dir=args.data_dir,
        model_path=args.model_path,
        load_path=args.load_path,
        batch_size=args.batch_size,
        epochs=args.epochs,
        lr=args.lr
    )
