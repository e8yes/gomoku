import glob
import os
from typing import Tuple

import numpy as np
import torch
from torch.utils.data import Dataset


def _load_all(data_dir: str) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Loads all training examples from .bin shards into consolidated numpy arrays.

    Flat Binary Format (716 bytes per sample):
    - packed_state: 254 bytes (bit-packed 9x15x15 = 2025 bits, padded to 2032)
    - probs:        460 bytes (230 float16 probabilities)
    - value:        2 bytes   (float16 winner/evaluation)
    """
    shard_paths = sorted(glob.glob(os.path.join(data_dir, "*.bin")))

    states_list = []
    probs_list = []
    values_list = []

    # Sample size: 254 + 460 + 2 = 716 bytes
    SAMPLE_SIZE = 716

    for path in shard_paths:
        try:
            with open(path, "rb") as f:
                data = f.read()
                if len(data) == 0:
                    continue

                num_samples = len(data) // SAMPLE_SIZE
                if num_samples == 0:
                    continue

                # Use structured dtype for zero-copy parsing
                dtype = np.dtype(
                    [
                        ("packed_state", "u1", (254,)),
                        ("probs", "f2", (230,)),
                        ("value", "f2"),
                    ]
                )
                samples = np.frombuffer(data, dtype=dtype, count=num_samples)

                # We copy to avoid keeping the entire file buffer in memory via views
                states_list.append(samples["packed_state"].copy())
                probs_list.append(samples["probs"].copy())
                values_list.append(samples["value"].copy())

        except Exception as e:
            print(f"Error loading {path}: {e}")

    if not states_list:
        return (
            np.empty((0, 254), dtype=np.uint8),
            np.empty((0, 230), dtype=np.float16),
            np.empty((0,), dtype=np.float16),
        )

    return (
        np.concatenate(states_list, axis=0),
        np.concatenate(probs_list, axis=0),
        np.concatenate(values_list, axis=0),
    )


class GomokuDataset(Dataset):
    """
    Cumulative dataset for Gomoku training.
    Uses consolidated numpy arrays for high performance and low memory overhead.
    """

    def __init__(
        self,
        data_dir: str,
        augment: bool = True,
    ):
        self.data_dir = data_dir
        self.augment = augment

        if not os.path.exists(data_dir):
            os.makedirs(data_dir)

        # Load all shards into flat numpy arrays
        self.packed_states, self.probs, self.values = _load_all(data_dir)

    def __len__(self):
        return len(self.values)

    def __getitem__(self, idx):
        # 1. Unpack state: 254 bytes -> 2032 bits -> 2025 bits -> (9, 15, 15)
        packed_state = self.packed_states[idx]
        state_bits = np.unpackbits(packed_state)[:2025]
        state = state_bits.reshape(9, 15, 15).astype(np.float32)

        # 2. Extract policy and value
        prob = self.probs[idx].astype(np.float32)
        value = np.array([self.values[idx]], dtype=np.float32)

        # 3. Apply augmentation
        if self.augment:
            state, prob = self._apply_augmentation(state, prob)

        # 4. Convert to float32 for training
        return (
            torch.from_numpy(state).to(torch.float32),
            torch.from_numpy(prob).to(torch.float32),
            torch.from_numpy(value).to(torch.float32),
        )

    def _apply_augmentation(
        self, state: np.ndarray, prob: np.ndarray
    ) -> Tuple[np.ndarray, np.ndarray]:
        k = np.random.randint(0, 8)
        if k == 0:
            return state, prob

        board_prob = prob[:225].reshape(15, 15)
        special_prob = prob[225:]

        if k >= 4:
            state = np.flip(state, axis=2)
            board_prob = np.flip(board_prob, axis=1)

        rot = k % 4
        if rot > 0:
            state = np.rot90(state, k=rot, axes=(1, 2))
            board_prob = np.rot90(board_prob, k=rot)

        return state.copy(), np.concatenate([board_prob.flatten(), special_prob]).copy()
