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

def train(args):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device} in pure bfloat16...")

    # 1. Load Dataset
    dataset = GomokuDataset(args.data_dir, horizon=args.horizon, augment=True)
    if len(dataset) == 0:
        print("Error: No data found in", args.data_dir)
        return

    loader = DataLoader(
        dataset, 
        batch_size=args.batch_size, 
        shuffle=True, 
        num_workers=4, 
        pin_memory=True
    )

    # 2. Initialize Model
    model = GomokuNet().to(device).to(torch.bfloat16)
    
    # Load existing weights if available
    if os.path.exists(args.model_path):
        print(f"Loading weights from {args.model_path}")
        # Note: If weights were saved as float32, we may need to cast
        model.load_state_dict(torch.load(args.model_path, map_location=device))

    # 3. Optimizer & Scheduler
    optimizer = optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    # OneCycleLR is great for AlphaZero-style training
    scheduler = optim.lr_scheduler.OneCycleLR(
        optimizer, 
        max_lr=args.lr, 
        steps_per_epoch=len(loader), 
        epochs=args.epochs
    )

    # 4. Training Loop
    model.train()
    for epoch in range(args.epochs):
        pbar = tqdm(loader, desc=f"Epoch {epoch+1}/{args.epochs}")
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

    # 5. Save Weights
    torch.save(model.state_dict(), args.model_path)
    print(f"Model saved to {args.model_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_dir", type=str, default="data")
    parser.add_argument("--model_path", type=str, default="weights.pth")
    parser.add_argument("--horizon", type=int, default=2)
    parser.add_argument("--batch_size", type=int, default=256)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--lr", type=float, default=1e-3)
    
    args = parser.parse_args()
    train(args)
