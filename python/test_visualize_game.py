import tempfile
import unittest
from pathlib import Path

import numpy as np

from visualize_game import (
    Move,
    decode_training_record,
    load_move_trace,
    normalize_moves,
    render_board,
)


class VisualizeGameTest(unittest.TestCase):
    def test_normalizes_json_and_alternates_players(self):
        moves = normalize_moves(
            [
                {"x": 7, "y": 7},
                "(8,8)",
                {"x": 6, "y": 7, "color": "black", "number": 9},
                "swap2_choose_white",
            ],
            label_start=0,
        )
        self.assertEqual(
            moves,
            [
                Move(7, 7, "black", 0),
                Move(8, 8, "white", 1),
                Move(6, 7, "black", 9),
            ],
        )

    def test_loads_one_move_per_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "moves.txt"
            path.write_text("(0,0)\n1,1 white\n", encoding="utf-8")
            self.assertEqual(load_move_trace(path), ["(0,0)", "1,1 white"])

    def test_decodes_packed_training_record(self):
        features = np.zeros((9, 15, 15), dtype=np.uint8)
        features[0, 0, 0] = 1  # current player: white
        features[1, 1, 2] = 1  # opponent: black
        features[3, :, :] = 1  # current stone is white
        packed = np.packbits(features.reshape(-1), bitorder="big")
        raw = packed.tobytes() + bytes(254 - len(packed))
        raw += np.zeros(230, dtype="<f2").tobytes()
        raw += np.array([0.5], dtype="<f2").tobytes()

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "one.bin"
            path.write_bytes(raw)
            self.assertEqual(
                decode_training_record(path),
                [Move(0, 0, "white"), Move(2, 1, "black")],
            )

    def test_renders_png(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "board.png"
            output = render_board(
                [Move(7, 7, "black", 0), Move(8, 8, "white", 1)], path
            )
            self.assertEqual(output, path)
            self.assertGreater(path.stat().st_size, 1000)


if __name__ == "__main__":
    unittest.main()
