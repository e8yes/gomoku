"""Tests for the Gomocup engine adapter."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

from gomoku_match import (
    Action,
    InProcessTransport,
    MatchServer,
    MatchSettings,
    ObserverClient,
    PlayerClient,
)
from gomoku_match.adapters import (
    GomocupEngine,
    GomocupSwap2Strategy,
    make_gomocup_callback,
)

FAKE_ENGINE_PATH = Path(__file__).parent / "fake_gomocup_engine.py"
FAKE_ENGINE_CMD = [sys.executable, str(FAKE_ENGINE_PATH)]


def _bob_picker(state, _deadline_ms):
    """Bob plays a deterministic non-blocking sequence opposite Alice."""
    size = state["board_size"]
    phase = state["phase"]
    if phase == "SWAP2_DECISION":
        return Action.control("swap2_choose_white", size).id
    targets = [(14, 14), (13, 13), (12, 12), (11, 14), (10, 14), (9, 14),
               (8, 14), (7, 14), (6, 14), (5, 14), (4, 14)]
    for x, y in targets:
        a = x + y * size
        if a in state["legal_actions"]:
            return a
    return state["legal_actions"][-1]


class GomocupEngineTests(unittest.TestCase):
    def test_subprocess_lifecycle(self) -> None:
        engine = GomocupEngine(FAKE_ENGINE_CMD, board_size=15, startup_timeout_s=3.0)
        try:
            x, y = engine.play_from_board([(0, 0, 1), (1, 1, 2)])
            # Fake engine returns the first empty cell in row-major
            # order; with (0,0) and (1,1) occupied, that's (1,0).
            self.assertEqual((x, y), (1, 0))
            x2, y2 = engine.play_after_opponent(0, 1)
            # Now occupied = {(0,0),(1,1),(1,0),(0,1)} → next is (2,0).
            self.assertEqual((x2, y2), (2, 0))
        finally:
            engine.close()

    def test_swap2_strategy_supplies_phase_actions(self) -> None:
        strat = GomocupSwap2Strategy()
        size = 15

        # PLACE_INITIAL_THREE move 0 → (7,7).
        s0 = {
            "phase": "PLACE_INITIAL_THREE",
            "board_size": size,
            "move_count": 0,
            "moves": [],
        }
        self.assertEqual(strat.action_for_phase(s0), 7 + 7 * size)

        # SWAP2_DECISION → swap2_choose_white control id.
        s1 = {"phase": "SWAP2_DECISION", "board_size": size, "moves": []}
        self.assertEqual(
            strat.action_for_phase(s1),
            Action.control("swap2_choose_white", size).id,
        )

        # CHOOSE_COLOR → choose_white control id.
        s2 = {"phase": "CHOOSE_COLOR", "board_size": size, "moves": []}
        self.assertEqual(
            strat.action_for_phase(s2),
            Action.control("choose_white", size).id,
        )


class GomocupAdapterIntegrationTests(unittest.TestCase):
    def test_full_swap2_game_via_subprocess_engine(self) -> None:
        server = MatchServer(clock_resolution_s=0.05)
        try:
            client_t, server_t = InProcessTransport.pair()
            server.attach(server_t)
            on_turn, close_engine = make_gomocup_callback(
                FAKE_ENGINE_CMD, timeout_turn_ms=5000
            )
            try:
                alice = PlayerClient(client_t, name="alice", on_turn=on_turn)
                alice.register()

                bob_t, bob_server_t = InProcessTransport.pair()
                server.attach(bob_server_t)
                bob = PlayerClient(bob_t, name="bob", on_turn=_bob_picker)
                bob.register()

                obs_t, obs_server_t = InProcessTransport.pair()
                server.attach(obs_server_t)
                observer = ObserverClient(obs_t)
                observer.handshake()
                observer.subscribe()

                gid = server.create_match(
                    "alice", "bob",
                    MatchSettings(deadline_ms_per_move=10000),
                )
                event = observer.wait_for_event(
                    "game_finished", timeout=30.0
                )
                # Some result occurred — the headline assertion is that
                # the game terminates cleanly under the bridge, not that
                # the fake engine wins. We tolerate any of the legitimate
                # finish reasons.
                self.assertIn(
                    event.params["reason"],
                    ("five_in_a_row", "draw", "timeout", "resignation"),
                )
                self.assertEqual(event.params["game_id"], gid)
                # The Swap2 phases were handled by the strategy and did
                # not need the engine — but the STANDARD phase did, so
                # the move log must contain placements past the Swap2
                # opening (PLACE_INITIAL_THREE = 3, plus a SWAP2 control
                # action, plus at least one STANDARD placement).
                final_state = event.params["final_state"]
                placements = [
                    a for a in final_state["moves"]
                    if int(a) < final_state["board_size"] ** 2
                ]
                self.assertGreaterEqual(len(placements), 4)

                alice.close()
                bob.close()
                observer.close()
            finally:
                close_engine()
        finally:
            server.shutdown()


if __name__ == "__main__":
    unittest.main()
