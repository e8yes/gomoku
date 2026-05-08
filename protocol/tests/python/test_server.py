"""End-to-end tests for the in-process MatchServer."""

from __future__ import annotations

import json
import tempfile
import time
import unittest
from pathlib import Path

from gomoku_match import (
    InProcessTransport,
    MatchServer,
    MatchSettings,
    MatchStore,
    ObserverClient,
    PlayerClient,
)
from gomoku_match.board import Action, GameResult
from gomoku_match.player_client import PlayerClientError
from gomoku_match.protocol import (
    ErrorCode,
    Request,
    decode_message,
    encode_message,
)


def _build_pair(server: MatchServer) -> InProcessTransport:
    client_side, server_side = InProcessTransport.pair()
    server.attach(server_side)
    return client_side


class _ScriptedPlayer:
    """Player whose moves are dictated by a callable + state inspection.

    The callable receives ``state`` and ``deadline_ms`` and returns
    either an ``int`` action id or a ``str`` action label.
    """

    def __init__(self, name: str, server: MatchServer, picker):
        self.name = name
        transport = _build_pair(server)
        self.client = PlayerClient(transport, name=name, on_turn=picker)
        self.client.register()

    def close(self) -> None:
        self.client.close()


def _swap2_alice_picker(state, deadline_ms):
    """Drive a deterministic Swap2 game from Alice's seat (Player.A).

    Alice places her three initial stones as B-W-B at (1,0), (10,10),
    (2,0): that gives her two black stones already on row 0 with the
    white stone parked far away. After Bob takes white in SWAP2, Alice
    completes 5-in-a-row at row 0 in three more moves.
    """

    phase = state["phase"]
    size = state["board_size"]
    if phase == "PLACE_INITIAL_THREE":
        positions = [(1, 0), (10, 10), (2, 0)]
        x, y = positions[state["move_count"]]
        return x + y * size
    if phase == "CHOOSE_COLOR":
        return Action.control("choose_white", size).id
    # STANDARD: complete row 0 at columns 0, 3, 4.
    for col in (0, 3, 4):
        a = col + 0 * size
        if a in state["legal_actions"]:
            return a
    return state["legal_actions"][0]


def _swap2_bob_picker(state, deadline_ms):
    """Bob (Player.B) chooses white at SWAP2 and parks stones in a
    diagonal corner cluster with a gap so he never accidentally forms
    a 5-in-a-row before Alice wins at move 9.
    """

    phase = state["phase"]
    size = state["board_size"]
    if phase == "SWAP2_DECISION":
        return Action.control("swap2_choose_white", size).id
    # Pre-planned corner stones with a gap at (11,11).
    targets = [(14, 14), (13, 13), (12, 12), (9, 9), (8, 8)]
    for x, y in targets:
        a = x + y * size
        if a in state["legal_actions"]:
            return a
    return state["legal_actions"][-1]


class MatchServerEndToEndTests(unittest.TestCase):
    def test_full_swap2_game_completes_with_observer_capturing_every_move(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "matches.sqlite")
            server = MatchServer(store=store)
            try:
                alice = _ScriptedPlayer("alice", server, _swap2_alice_picker)
                bob = _ScriptedPlayer("bob", server, _swap2_bob_picker)
                obs_transport = _build_pair(server)
                observer = ObserverClient(obs_transport)
                observer.handshake()
                observer.subscribe()

                game_id = server.create_match(
                    "alice", "bob", MatchSettings(deadline_ms_per_move=2000)
                )

                event = observer.wait_for_event("game_finished", timeout=10.0)
                params = event.params
                self.assertEqual(params["game_id"], game_id)
                self.assertEqual(params["result"], "PLAYER_A_WIN")
                self.assertEqual(params["reason"], "five_in_a_row")
                self.assertEqual(params["winner"], "alice")

                # Observer should have seen a state_changed for every action.
                state_changes = [
                    e for e in observer.events() if e.method == "state_changed"
                ]
                final_state = params["final_state"]
                expected_actions = len(final_state["moves"])
                self.assertEqual(len(state_changes), expected_actions)

                # Persistence: match marked finished and all actions journalled.
                rec = store.get_match(game_id)
                self.assertIsNotNone(rec)
                assert rec is not None
                self.assertEqual(rec.result, "PLAYER_A_WIN")
                self.assertEqual(rec.reason, "five_in_a_row")
                self.assertIsNotNone(rec.finished_at)
                moves = store.list_moves(game_id)
                self.assertEqual(len(moves), expected_actions)
                # Plies are 1-indexed and contiguous.
                self.assertEqual([m.ply for m in moves], list(range(1, len(moves) + 1)))

                alice.close()
                bob.close()
                observer.close()
            finally:
                server.shutdown()
                store.close()

    def test_illegal_move_is_rejected_without_advancing_state(self) -> None:
        server = MatchServer(clock_resolution_s=0.02)
        try:
            # Alice's picker always submits a control action that is
            # illegal during PLACE_INITIAL_THREE; the server must reject
            # it and refuse to advance state. After her short deadline
            # expires Bob wins on timeout, and the observer can confirm
            # the canonical board never recorded a move.
            def illegal_picker(state, _deadline_ms):
                return Action.control("swap2_choose_white", state["board_size"]).id

            alice_t = _build_pair(server)
            alice = PlayerClient(alice_t, name="alice", on_turn=illegal_picker)
            alice.register()
            bob_t = _build_pair(server)
            bob = PlayerClient(
                bob_t, name="bob", on_turn=lambda s, _: s["legal_actions"][0]
            )
            bob.register()
            obs_t = _build_pair(server)
            observer = ObserverClient(obs_t)
            observer.handshake()
            observer.subscribe()

            game_id = server.create_match(
                "alice", "bob", MatchSettings(deadline_ms_per_move=150)
            )
            event = observer.wait_for_event("game_finished", timeout=5.0)
            self.assertEqual(event.params["reason"], "timeout")
            self.assertEqual(event.params["winner"], "bob")
            self.assertEqual(event.params["final_state"]["move_count"], 0)

            state = observer.query_state(game_id)["state"]
            self.assertEqual(state["move_count"], 0)
            alice.close()
            bob.close()
            observer.close()
        finally:
            server.shutdown()

    def test_deadline_timeout_forfeits_active_player(self) -> None:
        server = MatchServer(clock_resolution_s=0.02)
        try:
            # Alice never plays — her on_turn callback blocks long
            # enough to miss her short deadline.
            def stalling_picker(state, deadline_ms):
                time.sleep(0.5)
                return state["legal_actions"][0]

            alice_t = _build_pair(server)
            alice = PlayerClient(alice_t, name="alice", on_turn=stalling_picker)
            alice.register()
            bob_t = _build_pair(server)
            bob = PlayerClient(bob_t, name="bob", on_turn=lambda s, _: s["legal_actions"][0])
            bob.register()
            obs_t = _build_pair(server)
            observer = ObserverClient(obs_t)
            observer.handshake()
            observer.subscribe()

            game_id = server.create_match(
                "alice", "bob", MatchSettings(deadline_ms_per_move=100)
            )
            event = observer.wait_for_event("game_finished", timeout=5.0)
            self.assertEqual(event.params["reason"], "timeout")
            # Alice was on the clock first → Bob wins on her timeout.
            self.assertEqual(event.params["result"], "PLAYER_B_WIN")
            self.assertEqual(event.params["winner"], "bob")
            alice.close()
            bob.close()
            observer.close()
        finally:
            server.shutdown()


class HandshakeGateTests(unittest.TestCase):
    """Verify §1.1 fix: every method except ``handshake`` requires handshake."""

    def _send_request(self, transport, method: str, params: dict, *, rid: str):
        payload = encode_message(Request(method=method, id=rid, params=params))
        transport.send(payload)
        line = transport.recv(timeout=2.0)
        return decode_message(line)

    def test_query_state_before_handshake_is_rejected(self) -> None:
        # Pre-§1.1 fix, an attacker could read any game's state by
        # opening a fresh socket and sending ``query_state`` directly.
        # The dispatcher now refuses every non-handshake method on a
        # connection that hasn't completed handshake.
        server = MatchServer(clock_resolution_s=0.02)
        try:
            client_side, server_side = InProcessTransport.pair()
            server.attach(server_side)
            response = self._send_request(
                client_side,
                "query_state",
                {"game_id": "g_does_not_exist"},
                rid="r1",
            )
            self.assertIsNone(response.result)
            self.assertIsNotNone(response.error)
            self.assertEqual(
                int(response.error["code"]), int(ErrorCode.BAD_REQUEST)
            )
            client_side.close()
        finally:
            server.shutdown()

    def test_submit_move_before_handshake_is_rejected(self) -> None:
        server = MatchServer(clock_resolution_s=0.02)
        try:
            client_side, server_side = InProcessTransport.pair()
            server.attach(server_side)
            response = self._send_request(
                client_side,
                "submit_move",
                {"game_id": "g_x", "action": 0},
                rid="r2",
            )
            self.assertIsNotNone(response.error)
            self.assertEqual(
                int(response.error["code"]), int(ErrorCode.BAD_REQUEST)
            )
            client_side.close()
        finally:
            server.shutdown()

    def test_query_history_before_handshake_is_rejected(self) -> None:
        server = MatchServer(clock_resolution_s=0.02, admin_token="adm")
        try:
            client_side, server_side = InProcessTransport.pair()
            server.attach(server_side)
            response = self._send_request(
                client_side, "query_history", {}, rid="r3"
            )
            self.assertIsNotNone(response.error)
            # Pre-fix this got past the handshake check straight to the
            # admin gate. After the fix the handshake gate fires first.
            self.assertEqual(
                int(response.error["code"]), int(ErrorCode.BAD_REQUEST)
            )
            client_side.close()
        finally:
            server.shutdown()


class JournalIdempotencyTests(unittest.TestCase):
    """Verify §1.3 fix: record_move is idempotent under replay."""

    def test_record_move_is_idempotent(self) -> None:
        # Crash-replay invokes record_move twice for the same (game_id,
        # ply); the ``INSERT OR IGNORE`` change makes the second call a
        # no-op rather than raising IntegrityError.
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "matches.sqlite")
            try:
                store.record_match_started(
                    game_id="g1",
                    player_a_name="alice",
                    player_b_name="bob",
                    board_size=15,
                    deadline_ms_per_move=1000,
                )
                store.record_move(
                    game_id="g1",
                    ply=1,
                    action_id=10,
                    label="(10,0)",
                    by_player_name="alice",
                )
                # Replay the same move — must not raise.
                store.record_move(
                    game_id="g1",
                    ply=1,
                    action_id=10,
                    label="(10,0)",
                    by_player_name="alice",
                )
                moves = store.list_moves("g1")
                self.assertEqual(len(moves), 1)
                self.assertEqual(moves[0].action_id, 10)
            finally:
                store.close()

    def test_record_match_started_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "matches.sqlite")
            try:
                store.record_match_started(
                    game_id="g1",
                    player_a_name="alice",
                    player_b_name="bob",
                    board_size=15,
                    deadline_ms_per_move=1000,
                )
                store.record_match_started(
                    game_id="g1",
                    player_a_name="alice",
                    player_b_name="bob",
                    board_size=15,
                    deadline_ms_per_move=1000,
                )
                self.assertEqual(len(store.list_matches()), 1)
            finally:
                store.close()


class PersistenceTests(unittest.TestCase):
    def test_store_round_trip_independently_of_server(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "matches.sqlite"
            store = MatchStore(path)
            store.record_match_started(
                game_id="g1",
                player_a_name="alice",
                player_b_name="bob",
                board_size=15,
                deadline_ms_per_move=3000,
            )
            store.record_move(
                game_id="g1", ply=1, action_id=0, label="(0,0)", by_player_name="alice"
            )
            store.record_match_finished(
                game_id="g1", result="PLAYER_A_WIN", reason="five_in_a_row"
            )
            store.close()

            # Reopen and confirm everything is there.
            store2 = MatchStore(path)
            try:
                rec = store2.get_match("g1")
                self.assertIsNotNone(rec)
                assert rec is not None
                self.assertEqual(rec.player_a_name, "alice")
                self.assertEqual(rec.result, "PLAYER_A_WIN")
                self.assertEqual(len(store2.list_matches()), 1)
                self.assertEqual(len(store2.list_matches(finished_only=True)), 1)
                self.assertEqual(
                    len(store2.list_matches(player_name="alice")), 1
                )
                moves = store2.list_moves("g1")
                self.assertEqual(len(moves), 1)
                self.assertEqual(moves[0].label, "(0,0)")
            finally:
                store2.close()


if __name__ == "__main__":
    unittest.main()
