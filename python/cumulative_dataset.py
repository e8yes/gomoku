import os
import glob
import struct
import numpy as np
import torch
from torch.utils.data import Dataset
from typing import List, Tuple, Optional

class GomokuDataset(Dataset):
    """
    Cumulative dataset for Gomoku self-play data.
    Loads raw binary shards from disk.
    
    Binary Format:
    - num_moves (int32, 4 bytes)
    - winner    (float32, 4 bytes)
    - states    (num_moves * 254 bytes, bit-packed uint8)
    - probs     (num_moves * 230 * 2 bytes, float16)
    """

    def __init__(
        self,
        data_dir: str,
        horizon: int = 2,
        augment: bool = True,
    ):
        self.data_dir = data_dir
        self.horizon = horizon
        self.augment = augment
        self.samples = []
        
        if not os.path.exists(data_dir):
            os.makedirs(data_dir)
            
        self._refresh_shards()

    def _refresh_shards(self):
        """
        Scans the data directory for .bin shards and parses them.
        """
        shard_paths = sorted(glob.glob(os.path.join(self.data_dir, "*.bin")))
        self.samples = []
        
        for path in shard_paths:
            try:
                with open(path, "rb") as f:
                    file_data = f.read()
                    offset = 0
                    file_size = len(file_data)
                    
                    while offset < file_size:
                        # 1. Read header (num_moves: i32, winner: f32)
                        if offset + 8 > file_size: break
                        num_moves, winner = struct.unpack_from("<if", file_data, offset)
                        offset += 8
                        
                        # 2. State & Policy offsets
                        states_size = num_moves * 254
                        probs_size = num_moves * 230 * 2
                        
                        if offset + states_size + probs_size > file_size:
                            print(f"Warning: Truncated game in {path}")
                            break
                            
                        # Use np.frombuffer for zero-copy views
                        states_view = np.frombuffer(file_data, dtype=np.uint8, count=states_size, offset=offset)
                        offset += states_size
                        
                        probs_view = np.frombuffer(file_data, dtype=np.float16, count=num_moves * 230, offset=offset)
                        offset += probs_size
                        
                        # Reshape
                        states_view = states_view.reshape(num_moves, 254)
                        probs_view = probs_view.reshape(num_moves, 230)
                        
                        # Apply horizon filter
                        start_move = max(0, num_moves - self.horizon)
                        for m in range(start_move, num_moves):
                            self.samples.append({
                                'packed_state': states_view[m].copy(),
                                'prob': probs_view[m].copy(),
                                'value': winner
                            })
            except Exception as e:
                print(f"Error loading {path}: {e}")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        sample = self.samples[idx]
        
        # Unpack state: 254 bytes -> 2032 bits -> 2025 bits -> (9, 15, 15)
        packed_state = sample['packed_state']
        state_bits = np.unpackbits(packed_state)[:2025]
        state = state_bits.reshape(9, 15, 15).astype(np.float32)
        
        prob = sample['prob'].astype(np.float32)
        value = np.array([sample['value']], dtype=np.float32)

        if self.augment:
            state, prob = self._apply_augmentation(state, prob)

        # Convert to bfloat16 for training
        return (
            torch.from_numpy(state).to(torch.bfloat16),
            torch.from_numpy(prob).to(torch.bfloat16),
            torch.from_numpy(value).to(torch.bfloat16)
        )

    def _apply_augmentation(self, state: np.ndarray, prob: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        k = np.random.randint(0, 8)
        if k == 0: return state, prob
        
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
