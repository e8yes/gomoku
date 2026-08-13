import glob
import mmap
import os
from typing import Dict, List, Tuple

import numpy as np
import torch
from torch.utils.data import Dataset


# Flat Binary Format (716 bytes per sample):
# - packed_state: 254 bytes (bit-packed 9x15x15 = 2025 bits, padded to 2032)
# - probs:        460 bytes (230 float16 probabilities)
# - value:        2 bytes   (float16 winner/evaluation)
SAMPLE_SIZE = 716
DEFAULT_ITERATION_WINDOW = 4
RECORD_DTYPE = np.dtype(
    [
        ("packed_state", "u1", (254,)),
        ("probs", "f2", (230,)),
        ("value", "f2"),
    ]
)


class GomokuDataset(Dataset):
    """
    Cumulative dataset for Gomoku training.
    Indexes shards without loading the cumulative dataset into RAM. Each worker
    lazily memory-maps the shards it reads, keeping memory bounded even after
    many curriculum iterations.
    """

    def __init__(
        self,
        data_dir: str,
        augment: bool = True,
        iteration_window: int | None = None,
    ):
        self.data_dir = data_dir
        self.augment = augment

        if iteration_window is not None and iteration_window <= 0:
            raise ValueError("iteration_window must be positive")

        if not os.path.exists(data_dir):
            os.makedirs(data_dir)

        shard_paths = sorted(glob.glob(os.path.join(data_dir, "*.bin")))
        if iteration_window is not None:
            shard_paths = shard_paths[-iteration_window:]

        self.shard_paths: List[str] = []
        shard_counts: List[int] = []
        for path in shard_paths:
            size = os.path.getsize(path)
            count, remainder = divmod(size, SAMPLE_SIZE)
            if remainder:
                raise ValueError(
                    f"Shard {path} has {remainder} trailing bytes; "
                    f"expected records of {SAMPLE_SIZE} bytes"
                )
            if count:
                self.shard_paths.append(path)
                shard_counts.append(count)

        self.shard_counts = shard_counts
        self.shard_ends = np.cumsum(np.asarray(shard_counts, dtype=np.int64))
        self._mapped_files: Dict[int, object] = {}
        self._mapped_shards: Dict[int, mmap.mmap] = {}

    def __len__(self):
        if len(self.shard_ends) == 0:
            return 0
        return int(self.shard_ends[-1])

    def _get_shard(self, shard_index: int) -> mmap.mmap:
        mapped = self._mapped_shards.get(shard_index)
        if mapped is None:
            file_handle = open(self.shard_paths[shard_index], "rb")
            mapped = mmap.mmap(file_handle.fileno(), 0, access=mmap.ACCESS_READ)
            self._mapped_files[shard_index] = file_handle
            self._mapped_shards[shard_index] = mapped
        return mapped

    def __getstate__(self):
        # mmap/file handles cannot be serialized by spawn-based DataLoaders.
        state = self.__dict__.copy()
        state["_mapped_files"] = {}
        state["_mapped_shards"] = {}
        return state

    def __del__(self):
        for mapped in getattr(self, "_mapped_shards", {}).values():
            mapped.close()
        for file_handle in getattr(self, "_mapped_files", {}).values():
            file_handle.close()

    def __getitem__(self, idx):
        if idx < 0:
            idx += len(self)
        if idx < 0 or idx >= len(self):
            raise IndexError(idx)

        shard_index = int(np.searchsorted(self.shard_ends, idx, side="right"))
        shard_start = 0 if shard_index == 0 else int(self.shard_ends[shard_index - 1])
        record_offset = (idx - shard_start) * SAMPLE_SIZE
        mapped = self._get_shard(shard_index)
        record = np.frombuffer(
            mapped, dtype=RECORD_DTYPE, count=1, offset=record_offset
        )[0]

        # 1. Unpack state: 254 bytes -> 2032 bits -> 2025 bits -> (9, 15, 15)
        packed_state = record["packed_state"].copy()
        state_bits = np.unpackbits(packed_state)[:2025]
        state = state_bits.reshape(9, 15, 15).astype(np.float32)

        # 2. Extract policy and value
        prob = record["probs"].astype(np.float32)
        value = np.array([record["value"]], dtype=np.float32)

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
