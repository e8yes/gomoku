"""
create_model.py — Export the Gomoku ResNet as a TorchScript model.

Run from the python/ directory:

    python create_model.py
    # → writes model.pt (copy to build/ before running neural_net_evaluator_tests)

Model I/O contract (must match NeuralNetEvaluator / BatchInferenceExecutor):
    Input:  float32 [batch, 9, 15, 15]
    Output: tuple(
        policy_logits  float32 [batch, 230],   # raw logits; C++ applies masking+softmax
        value          float32 [batch, 1],      # tanh-bounded scalar in [-1, 1]
    )
"""

import torch
import torch.nn as nn
from torchsummary import summary
import time

# Input channel layout (must match NeuralNetEvaluator::kNumInputChannels and
# the encoding in NeuralNetEvaluator::BoardToTensorImpl):
#
#   Ch 0 — Current player's stones (1.0 where present, else 0.0)
#   Ch 1 — Opponent's stones       (1.0 where present, else 0.0)
#   Ch 2 — Constant 1.0 if the current player is Black, else 0.0
#   Ch 3 — Constant 1.0 if the current player is White, else 0.0
#   Ch 4 — Constant 1.0 if Phase == kPlaceInitialThree, else 0.0
#   Ch 5 — Constant 1.0 if Phase == kSwap2Decision, else 0.0
#   Ch 6 — Constant 1.0 if Phase == kSwap2PlaceTwo, else 0.0
#   Ch 7 — Constant 1.0 if Phase == kChooseColor, else 0.0
#   Ch 8 — Constant 1.0 if Phase == kStandard, else 0.0
#
# Rationale:
#   Channels 0 & 1 give the network a consistent first-person view of the
#   board regardless of stone colour, so the same weights handle both sides.
#
#   Channels 2 & 3 are the "colour-to-move" indicator planes. They are
#   necessary because Gomoku with Swap2 assigns colours dynamically: a player
#   who places the initial three stones may end up as either Black or White
#   depending on the opponent's Swap2 decision. Without an explicit colour
#   signal the network cannot distinguish:
#     - whose long-term positional advantage is larger (Black typically has a
#       stronger first-move advantage in Gomoku);
#     - which side is subject to the exact-five rule (overlines don't win).
#
#   Channels 4-8 are "phase" indicator planes. In Swap2, the game progresses
#   through several distinct phases (placing initial stones, deciding to swap,
#   placing two more stones, etc.). The legal actions and strategy depend heavily
#   on the current phase. By using explicit scalar planes, the network can condition
#   its predictions on the current Swap2 state.

NUM_INPUT_CHANNELS = 9  # total input channels; see layout above
BOARD_SIZE = 15
NUM_ACTIONS = 230  # must match Board::kNumActions (225 cells + 5 Swap2)

NUM_FILTERS = 128
NUM_BLOCKS = 5
SE_RATIO = 4  # SE bottleneck: filters // SE_RATIO channels


# ---------------------------------------------------------------------------
# Building blocks
# ---------------------------------------------------------------------------


class SELayer(nn.Module):
    """Squeeze-Excitation channel attention.

    GlobalAvgPool → FC(C→C/r) → ReLU → FC(C/r→C) → Sigmoid → scale.
    Adds ~0.5% parameters but measurably improves tactical feature selection.
    """

    def __init__(self, channels: int, ratio: int = SE_RATIO):
        super().__init__()
        squeezed = max(channels // ratio, 1)
        self.pool = nn.AdaptiveAvgPool2d(1)
        self.fc1 = nn.Linear(channels, squeezed)
        self.fc2 = nn.Linear(squeezed, channels)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        b, c, _, _ = x.shape
        s = self.pool(x).view(b, c)
        s = torch.relu(self.fc1(s))
        s = torch.sigmoid(self.fc2(s))
        return x * s.view(b, c, 1, 1)


class ResBlock(nn.Module):
    """Standard pre-activation residual block with SE attention.

    Conv-BN-ReLU → Conv-BN → SE → + skip → ReLU
    """

    def __init__(self, filters: int):
        super().__init__()
        self.conv1 = nn.Conv2d(filters, filters, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(filters)
        self.conv2 = nn.Conv2d(filters, filters, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(filters)
        self.se = SELayer(filters)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        residual = x
        out = torch.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        out = self.se(out)
        return torch.relu(out + residual)


# ---------------------------------------------------------------------------
# Full network
# ---------------------------------------------------------------------------


class GomokuNet(nn.Module):
    """10-block SE-ResNet for 15×15 Gomoku (Swap2 variant).

    Input:  float32 [batch, 4, 15, 15]  — see NUM_INPUT_CHANNELS for layout.
    Output: (policy_logits [batch, 230], value [batch, 1])

    Designed to fit comfortably on the RTX 4060 Ti at batch size 256–512.
    """

    def __init__(
        self,
        num_filters: int = NUM_FILTERS,
        num_blocks: int = NUM_BLOCKS,
    ):
        super().__init__()

        # Stem: project input channels to filter space.
        self.stem = nn.Sequential(
            nn.Conv2d(NUM_INPUT_CHANNELS, num_filters, 3, padding=1, bias=False),
            nn.BatchNorm2d(num_filters),
            nn.ReLU(inplace=True),
        )

        # Residual tower.
        self.tower = nn.Sequential(*[ResBlock(num_filters) for _ in range(num_blocks)])

        # Policy head: 2 conv filters → flatten → 230 logits.
        self.policy_head = nn.Sequential(
            nn.Conv2d(num_filters, 2, 1, bias=False),
            nn.BatchNorm2d(2),
            nn.ReLU(inplace=True),
            nn.Flatten(),
            nn.Linear(2 * BOARD_SIZE * BOARD_SIZE, NUM_ACTIONS),
        )

        # Value head: 1 conv filter → flatten → FC(256) → FC(1) → tanh.
        self.value_head = nn.Sequential(
            nn.Conv2d(num_filters, 1, 1, bias=False),
            nn.BatchNorm2d(1),
            nn.ReLU(inplace=True),
            nn.Flatten(),
            nn.Linear(BOARD_SIZE * BOARD_SIZE, 256),
            nn.ReLU(inplace=True),
            nn.Linear(256, 1),
            nn.Tanh(),
        )

    def forward(self, x: torch.Tensor):
        x = self.tower(self.stem(x))
        return self.policy_head(x), self.value_head(x)


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------


def main():
    assert torch.cuda.is_available(), "CUDA is not available"
    device = torch.device("cuda")

    # torchsummary feeds float32 inputs internally, so run it before .half().
    model = GomokuNet().to(device).eval()
    summary(model, (NUM_INPUT_CHANNELS, BOARD_SIZE, BOARD_SIZE))

    # Trace the model. Master model is always in FP32.
    dummy = torch.zeros(
        1,
        NUM_INPUT_CHANNELS,
        BOARD_SIZE,
        BOARD_SIZE,
        device=device,
        dtype=torch.float32,
    )
    traced = torch.jit.trace(model, dummy)

    # Sanity check shapes.
    with torch.no_grad():
        policy, value = traced(dummy)
    assert policy.shape == (1, NUM_ACTIONS), f"Bad policy shape: {policy.shape}"
    assert value.shape == (1, 1), f"Bad value shape:  {value.shape}"

    out_path = "model.pt"
    traced.save(out_path)
    print(f"\nSaved TorchScript model → {out_path}")
    print(f"  Filters:       {NUM_FILTERS}")
    print(f"  Blocks:        {NUM_BLOCKS}")
    print(f"  Policy shape:  {list(policy.shape)}")
    print(f"  Value shape:   {list(value.shape)}")

    # Throughput test:
    test_model = model.half()
    BATCH_SIZE = 192

    time_begin = time.time()
    NUM_INFERENCE = 1000
    for _ in range(NUM_INFERENCE):
        batch = (
            torch.ones(
                BATCH_SIZE,
                NUM_INPUT_CHANNELS,
                BOARD_SIZE,
                BOARD_SIZE,
                dtype=torch.float16,
            )
            * 0.2345
        )
        test_model(batch.to(device))
    time_end = time.time()
    print(
        f"Average inference time: {(time_end - time_begin) * 1000 / (NUM_INFERENCE * BATCH_SIZE)} ms"
    )


if __name__ == "__main__":
    main()
