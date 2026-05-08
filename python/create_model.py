"""
create_model.py — Generate a skeleton Gomoku ResNet as TorchScript.

Run this once before smoke-testing NeuralNetEvaluator:

    python python/create_model.py
    # → writes model.pt in the current directory

The generated model has random weights but the correct I/O contract:
    Input:  float32 [batch, 4, 15, 15]
    Output: tuple(
        policy_logits  float32 [batch, 230],   # 225 board + 5 Swap2 actions
        value          float32 [batch, 1],      # tanh-bounded scalar
    )

This skeleton (1 conv layer + policy/value heads) will be replaced by the
fully-trained ResNet produced in Phase 3.
"""

import torch
import torch.nn as nn
from torchsummary import summary

NUM_INPUT_CHANNELS = 4   # must match NeuralNetEvaluator::kNumInputChannels
BOARD_SIZE = 15
NUM_ACTIONS = 230        # must match Board::kNumActions


class GomokuNet(nn.Module):
    """Minimal single-block ResNet-style network for skeleton testing."""

    def __init__(self, num_filters: int = 64):
        super().__init__()
        # Shared body
        self.conv = nn.Conv2d(NUM_INPUT_CHANNELS, num_filters, 3, padding=1)
        self.bn = nn.BatchNorm2d(num_filters)

        # Policy head
        self.policy_conv = nn.Conv2d(num_filters, 2, 1)
        self.policy_bn = nn.BatchNorm2d(2)
        self.policy_fc = nn.Linear(2 * BOARD_SIZE * BOARD_SIZE, NUM_ACTIONS)

        # Value head
        self.value_conv = nn.Conv2d(num_filters, 1, 1)
        self.value_bn = nn.BatchNorm2d(1)
        self.value_fc1 = nn.Linear(BOARD_SIZE * BOARD_SIZE, 64)
        self.value_fc2 = nn.Linear(64, 1)

    def forward(self, x: torch.Tensor):
        # Shared body
        x = torch.relu(self.bn(self.conv(x)))

        # Policy head → logits (no softmax; the C++ evaluator masks + softmaxes)
        p = torch.relu(self.policy_bn(self.policy_conv(x)))
        p = p.view(p.size(0), -1)
        policy_logits = self.policy_fc(p)

        # Value head → tanh scalar
        v = torch.relu(self.value_bn(self.value_conv(x)))
        v = v.view(v.size(0), -1)
        v = torch.relu(self.value_fc1(v))
        value = torch.tanh(self.value_fc2(v))

        return policy_logits, value


def main():
    assert torch.cuda.is_available(), "CUDA is not available"
    device = torch.device("cuda")

    model = GomokuNet(num_filters=64).to(device)
    model.eval()

    summary(model, (NUM_INPUT_CHANNELS, BOARD_SIZE, BOARD_SIZE))

    # Trace with a dummy batch-1 input (avoids control-flow TorchScript issues
    # with BatchNorm; tracing is sufficient for an inference-only model).
    dummy = torch.zeros(1, NUM_INPUT_CHANNELS, BOARD_SIZE, BOARD_SIZE).to(device)
    traced = torch.jit.trace(model, dummy)

    # Quick sanity check
    with torch.no_grad():
        policy, value = traced(dummy)
    assert policy.shape == (1, NUM_ACTIONS), f"Unexpected policy shape: {policy.shape}"
    assert value.shape == (1, 1), f"Unexpected value shape: {value.shape}"

    out_path = "model.pt"
    traced.save(out_path)
    print(f"Saved TorchScript model to {out_path}")
    print(f"  Policy logits shape: {list(policy.shape)}")
    print(f"  Value shape:         {list(value.shape)}")


if __name__ == "__main__":
    main()
