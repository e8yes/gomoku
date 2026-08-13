import tempfile
import unittest
from pathlib import Path

import numpy as np

from cumulative_dataset import GomokuDataset, RECORD_DTYPE


class IterationWindowTest(unittest.TestCase):
    def test_only_newest_shards_are_indexed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            data_dir = Path(temp_dir)
            counts = [1, 2, 3, 4, 5, 6]
            for iteration, count in enumerate(counts):
                shard = np.zeros(count, dtype=RECORD_DTYPE)
                shard.tofile(data_dir / f"iteration_{iteration:03d}.bin")

            dataset = GomokuDataset(
                data_dir, augment=False, iteration_window=4
            )

            self.assertEqual(len(dataset), sum(counts[-4:]))
            self.assertEqual(
                [Path(path).name for path in dataset.shard_paths],
                [f"iteration_{iteration:03d}.bin" for iteration in range(2, 6)],
            )

    def test_window_larger_than_history_uses_all_available_shards(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            data_dir = Path(temp_dir)
            np.zeros(4, dtype=RECORD_DTYPE).tofile(
                data_dir / "iteration_000.bin"
            )

            dataset = GomokuDataset(
                data_dir, augment=False, iteration_window=4
            )
            self.assertEqual(len(dataset), 4)

    def test_window_must_be_positive(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaisesRegex(ValueError, "iteration_window"):
                GomokuDataset(temp_dir, iteration_window=0)


if __name__ == "__main__":
    unittest.main()
