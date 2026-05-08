"""Pure-Python Swap2 board tests."""

from __future__ import annotations

import unittest

from gomoku_match.board import (
    Action,
    Board,
    BoardConfig,
    GamePhase,
    GameResult,
    IllegalActionError,
    Player,
    Stone,
    decode_action_label,
    encode_action_label,
)


def placement(x: int, y: int, size: int = 15) -> int:
    return x + y * size


def control(name: str, size: int = 15) -> int:
    return Action.control(name, size).id


class BoardPhaseTests(unittest.TestCase):
    def test_initial_state_expects_three_placements(self) -> None:
        board = Board()
        self.assertEqual(board.phase, GamePhase.PLACE_INITIAL_THREE)
        self.assertEqual(board.current_player, Player.A)
        self.assertEqual(board.stone_to_place, Stone.BLACK)
        self.assertEqual(len(board.legal_actions()), 225)

    def test_swap2_choose_white_path(self) -> None:
        board = Board()
        for x in range(3):
            board.apply(placement(x, 0))
        self.assertEqual(board.phase, GamePhase.SWAP2_DECISION)
        self.assertEqual(board.current_player, Player.B)
        self.assertEqual(set(board.legal_actions()), {225, 226, 227})

        board.apply(control("swap2_choose_white"))
        self.assertEqual(board.phase, GamePhase.STANDARD)
        self.assertEqual(board.player_stones[Player.A], Stone.BLACK)
        self.assertEqual(board.player_stones[Player.B], Stone.WHITE)
        # Move 4 is white, so player B (white) is on the clock.
        self.assertEqual(board.current_player, Player.B)
        self.assertEqual(board.stone_to_place, Stone.WHITE)

    def test_swap2_place_two_then_choose_color(self) -> None:
        board = Board()
        board.apply(placement(7, 7))
        board.apply(placement(8, 7))
        board.apply(placement(7, 8))
        board.apply(control("swap2_place_two"))
        self.assertEqual(board.phase, GamePhase.SWAP2_PLACE_TWO)
        # B places W then B (4 total stones placed by both sides; 2 by B).
        board.apply(placement(8, 8))  # white
        self.assertEqual(board.stone_to_place, Stone.BLACK)
        board.apply(placement(6, 6))  # black
        self.assertEqual(board.phase, GamePhase.CHOOSE_COLOR)
        self.assertEqual(board.current_player, Player.A)

        board.apply(control("choose_white"))
        self.assertEqual(board.phase, GamePhase.STANDARD)
        self.assertEqual(board.player_stones[Player.A], Stone.WHITE)
        self.assertEqual(board.player_stones[Player.B], Stone.BLACK)
        # Move 6 is white, A (white) plays.
        self.assertEqual(board.current_player, Player.A)
        self.assertEqual(board.stone_to_place, Stone.WHITE)

    def test_illegal_action_raises(self) -> None:
        board = Board()
        with self.assertRaises(IllegalActionError):
            board.apply(control("swap2_choose_white"))  # not allowed yet
        # And re-using the same cell:
        board.apply(placement(7, 7))
        with self.assertRaises(IllegalActionError):
            board.apply(placement(7, 7))


class BoardTerminalTests(unittest.TestCase):
    def _reach_standard_a_black(self) -> Board:
        board = Board()
        board.apply(placement(0, 0))
        board.apply(placement(14, 14))
        board.apply(placement(1, 0))
        board.apply(control("swap2_choose_white"))  # B picks white, A black.
        # After swap2_choose_white, A is black, B is white, B is on clock.
        return board

    def test_exact_five_wins_for_player_a(self) -> None:
        board = self._reach_standard_a_black()
        # Sequence: B at (10,14) (white), A (4,0) black, B ..., A (3,0), etc.
        # Build A's row: 0,1,2,3,4 at row 0. (0) and (1) already placed.
        board.apply(placement(10, 14))  # B white
        board.apply(placement(2, 0))    # A black
        board.apply(placement(11, 14))  # B white
        board.apply(placement(3, 0))    # A black
        board.apply(placement(12, 14))  # B white
        board.apply(placement(4, 0))    # A black wins
        self.assertEqual(board.result, GameResult.PLAYER_A_WIN)

    def test_overline_does_not_win(self) -> None:
        # Distinguishing exact-five rule: completing a run of *six*
        # in a row by filling a gap is **not** a win. The opener
        # arranges columns 0..2 and 4..5 of row 0 first (no contiguous
        # run of five), then fills column 3 last to form 0..5 — six
        # stones in a row. Under exact-five this leaves the game
        # undetermined; under the legacy freestyle rule it would win.
        board = self._reach_standard_a_black()
        # A is black at (0,0), (1,0); B is white. Build the gapped
        # configuration before the 5-in-a-row trigger fires.
        # Sequence: alternate B (parked on row 14) and A (filling row 0).
        moves = [
            ((14, 13), (2, 0)),  # A row0: 0,1,2 (3-run, safe)
            ((13, 14), (4, 0)),  # A row0: gap at 3, isolated 4
            ((12, 14), (5, 0)),  # A row0: 4,5 (2-run); 0..2 still 3-run
            ((11, 14), (3, 0)),  # A fills gap → 0..5 contiguous (6-run)
        ]
        for (bx, by), (ax, ay) in moves:
            board.apply(placement(bx, by))  # B white
            board.apply(placement(ax, ay))  # A black
        # Six in a row should NOT win under exact-five.
        self.assertEqual(board.result, GameResult.UNDETERMINED)
        # Sanity: A really does have 6 black stones across (0..5, 0).
        for col in range(6):
            self.assertEqual(board.at(col, 0), Stone.BLACK)


class ActionLabelTests(unittest.TestCase):
    def test_round_trip(self) -> None:
        for action_id in [0, 113, 224, 225, 226, 227, 228, 229]:
            label = encode_action_label(action_id, 15)
            self.assertEqual(decode_action_label(label, 15), action_id)

    def test_decode_invalid(self) -> None:
        for bad in ["", "foo", "(99,99)", "(7,)"]:
            with self.assertRaises(ValueError):
                decode_action_label(bad, 15)


if __name__ == "__main__":
    unittest.main()
