import os
import struct
import numpy as np
import torch
from cumulative_dataset import GomokuDataset

def pack_game_states(states):
    """Packs [N, 9, 15, 15] 0/1 states into [N, 254] bit-packed uint8."""
    packed = []
    for s in states:
        bits = s.flatten()
        # Pad to 254*8 = 2032 bits
        padded = np.zeros(2032, dtype=np.uint8)
        padded[:2025] = bits
        packed.append(np.packbits(padded))
    return np.array(packed)

def test_binary_roundtrip():
    data_dir = "test_data"
    if not os.path.exists(data_dir):
        os.makedirs(data_dir)
        
    shard_path = os.path.join(data_dir, "test_shard.bin")
    
    # 1. Generate mock samples
    num_samples = 10
    
    # Create random states (9 channels, 15x15)
    states = np.random.randint(0, 2, (num_samples, 9, 15, 15)).astype(np.uint8)
    packed_states = pack_game_states(states) # [num_samples, 254]
    
    # Random probs (float16)
    probs = np.random.rand(num_samples, 230).astype(np.float16)
    
    # Values (float16)
    values = np.random.uniform(-1, 1, num_samples).astype(np.float16)
    
    # Write to file in flat format
    with open(shard_path, "wb") as f:
        for i in range(num_samples):
            f.write(packed_states[i].tobytes())
            f.write(probs[i].tobytes())
            f.write(values[i].tobytes())
            
    print(f"Created flat dummy shard with {num_samples} samples.")

    # 2. Load with GomokuDataset
    dataset = GomokuDataset(data_dir, augment=False)
    
    print(f"Dataset size: {len(dataset)} samples (Expected: 10)")
    assert len(dataset) == 10, f"Expected 10 samples, got {len(dataset)}"
    
    # Verify a specific sample (e.g. index 3, which corresponds to original index 3)
    state_tensor, prob_tensor, val_tensor = dataset[3]
    
    # Compare value (with tolerance for bfloat16 precision loss)
    expected_value = values[3]
    np.testing.assert_allclose(val_tensor.item(), expected_value, atol=1e-2)
    
    # Compare state
    expected_state = states[3].astype(np.float32)
    actual_state = state_tensor.float().numpy()
    np.testing.assert_array_almost_equal(actual_state, expected_state)
    
    # Compare prob (with tolerance for bfloat16 precision loss)
    expected_prob = probs[3].astype(np.float32)
    actual_prob = prob_tensor.float().numpy()
    np.testing.assert_allclose(actual_prob, expected_prob, atol=1e-2)

    print("Binary Roundtrip Test (Flat Format) PASSED!")
    
    # Cleanup
    os.remove(shard_path)
    os.rmdir(data_dir)

if __name__ == "__main__":
    test_binary_roundtrip()
