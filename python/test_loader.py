import os
import struct
import numpy as np
import torch
from cumulative_dataset import GomokuDataset

def test_binary_roundtrip():
    data_dir = "test_data"
    if not os.path.exists(data_dir):
        os.makedirs(data_dir)
        
    shard_path = os.path.join(data_dir, "test_shard.bin")
    
    # 1. Generate a mock game
    num_moves = 30
    winner = 1.0 # Black wins
    
    # Create random states (9 channels, 15x15)
    states = np.random.randint(0, 2, (num_moves, 9, 15, 15)).astype(np.uint8)
    packed_states = np.packbits(states, axis=None).reshape(num_moves, 254) # Wait, 254 is correct for 2025 bits
    
    # Actually, let's be precise: 15*15*9 = 2025 bits. 2025 / 8 = 253.125 -> 254 bytes.
    # To pack 2025 bits correctly into 254 bytes:
    def pack_game_states(states):
        # states: [N, 9, 15, 15]
        packed = []
        for s in states:
            bits = s.flatten()
            # Pad to 254*8 = 2032 bits
            padded = np.zeros(2032, dtype=np.uint8)
            padded[:2025] = bits
            packed.append(np.packbits(padded))
        return np.array(packed)

    packed_states = pack_game_states(states)
    
    # Random probs (float16)
    probs = np.random.rand(num_moves, 230).astype(np.float16)
    
    # Write to file
    with open(shard_path, "wb") as f:
        # Header: i32, f32
        f.write(struct.pack("<if", num_moves, winner))
        # States: num_moves * 254
        f.write(packed_states.tobytes())
        # Probs: num_moves * 230 * 2
        f.write(probs.tobytes())
        
    print(f"Created dummy shard with 1 game, {num_moves} moves.")

    # 2. Load with GomokuDataset
    # Horizon = 5: should only get last 5 moves
    dataset = GomokuDataset(data_dir, horizon=5, augment=False)
    
    print(f"Dataset size: {len(dataset)} samples (Expected: 5)")
    assert len(dataset) == 5, f"Expected 5 samples, got {len(dataset)}"
    
    # Verify last move
    state_tensor, prob_tensor, val_tensor = dataset[4]
    
    # Compare value
    assert val_tensor.item() == winner, f"Expected value {winner}, got {val_tensor.item()}"
    
    # Compare state (unpacking logic)
    expected_state = states[-1].astype(np.float32)
    actual_state = state_tensor.float().numpy()
    np.testing.assert_array_almost_equal(actual_state, expected_state)
    
    # Compare prob
    expected_prob = probs[-1].astype(np.float32)
    actual_prob = prob_tensor.float().numpy()
    np.testing.assert_array_almost_equal(actual_prob, expected_prob, decimal=3)

    print("Binary Roundtrip Test PASSED!")
    
    # Cleanup
    os.remove(shard_path)
    os.rmdir(data_dir)

if __name__ == "__main__":
    test_binary_roundtrip()
