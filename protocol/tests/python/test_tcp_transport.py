"""End-to-end tests for the TCP transport + auth + disconnect grace."""

from __future__ import annotations

import time
import unittest

from gomoku_match import (
    Action,
    MatchServer,
    MatchSettings,
    ObserverClient,
    PlayerClient,
    TcpListener,
    connect_tcp,
    parse_listen_url,
)
from gomoku_match.player_client import PlayerClientError


def _swap2_alice_picker(state, _deadline_ms):
    phase = state["phase"]
    size = state["board_size"]
    if phase == "PLACE_INITIAL_THREE":
        positions = [(1, 0), (10, 10), (2, 0)]
        x, y = positions[state["move_count"]]
        return x + y * size
    if phase == "CHOOSE_COLOR":
        return Action.control("choose_white", size).id
    for col in (0, 3, 4):
        a = col + 0 * size
        if a in state["legal_actions"]:
            return a
    return state["legal_actions"][0]


def _swap2_bob_picker(state, _deadline_ms):
    phase = state["phase"]
    size = state["board_size"]
    if phase == "SWAP2_DECISION":
        return Action.control("swap2_choose_white", size).id
    targets = [(14, 14), (13, 13), (12, 12), (9, 9), (8, 8)]
    for x, y in targets:
        a = x + y * size
        if a in state["legal_actions"]:
            return a
    return state["legal_actions"][-1]


class _ListenerHarness:
    """Spin up a MatchServer on a loopback TCP port for the test body."""

    def __init__(self, *, auth_token: str | None = None) -> None:
        self.server = MatchServer(clock_resolution_s=0.02, auth_token=auth_token)
        self.listener = TcpListener(
            "127.0.0.1", 0, lambda t: self.server.attach(t)
        )
        self.listener.start()
        self.port = self.listener.port

    def shutdown(self) -> None:
        self.listener.stop()
        self.server.shutdown()


class TcpTransportTests(unittest.TestCase):
    def test_parse_listen_url_accepts_bare_host_port(self) -> None:
        self.assertEqual(parse_listen_url("tcp://0.0.0.0:7901"), ("0.0.0.0", 7901))
        self.assertEqual(parse_listen_url("127.0.0.1:1234"), ("127.0.0.1", 1234))
        with self.assertRaises(ValueError):
            parse_listen_url("ws://localhost:80")

    def test_full_swap2_game_over_loopback_tcp(self) -> None:
        h = _ListenerHarness()
        try:
            alice = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=_swap2_alice_picker,
            )
            alice.register()
            bob = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="bob",
                on_turn=_swap2_bob_picker,
            )
            bob.register()
            observer = ObserverClient(connect_tcp("127.0.0.1", h.port))
            observer.handshake()
            observer.subscribe()

            game_id = h.server.create_match(
                "alice", "bob", MatchSettings(deadline_ms_per_move=2000)
            )
            event = observer.wait_for_event("game_finished", timeout=10.0)
            self.assertEqual(event.params["game_id"], game_id)
            self.assertEqual(event.params["result"], "PLAYER_A_WIN")
            self.assertEqual(event.params["reason"], "five_in_a_row")
            alice.close()
            bob.close()
            observer.close()
        finally:
            h.shutdown()

    def test_auth_token_required_rejects_missing_token(self) -> None:
        h = _ListenerHarness(auth_token="s3cret")
        try:
            client = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=_swap2_alice_picker,
            )
            with self.assertRaises(PlayerClientError) as ctx:
                client.register()
            self.assertIn("auth_failed", str(ctx.exception))
            client.close()
        finally:
            h.shutdown()

    def test_auth_token_accepted_when_matching(self) -> None:
        h = _ListenerHarness(auth_token="s3cret")
        try:
            client = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=_swap2_alice_picker,
                auth_token="s3cret",
            )
            pid = client.register()
            self.assertTrue(pid.startswith("p"))
            client.close()
        finally:
            h.shutdown()

    def test_disconnect_grace_cancelled_on_reconnect(self) -> None:
        h = _ListenerHarness()
        try:
            # Long deadline so the move clock cannot interfere.
            settings = MatchSettings(
                deadline_ms_per_move=60_000, disconnect_grace_ms=2_000
            )

            alice_first = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=lambda *_: None,  # don't actually move
            )
            alice_first.register()
            bob = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="bob",
                on_turn=_swap2_bob_picker,
            )
            bob.register()
            observer = ObserverClient(connect_tcp("127.0.0.1", h.port))
            observer.handshake()
            observer.subscribe()

            game_id = h.server.create_match("alice", "bob", settings)
            time.sleep(0.2)  # let your_turn arrive

            # Drop Alice — server should schedule (not apply) forfeit.
            alice_first.close()
            time.sleep(0.3)
            with h.server._lock:  # noqa: SLF001 — internal probe
                game = h.server._games[game_id]
                self.assertIsNotNone(game.disconnect_forfeit_at)
                self.assertFalse(game.finished)

            # Reconnect within grace; same name → cancels forfeit and
            # the scripted picker drives the rest of the game to a
            # legitimate (non-disconnect) result.
            alice_second = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=_swap2_alice_picker,
            )
            alice_second.register()
            event = observer.wait_for_event("game_finished", timeout=10.0)
            self.assertEqual(event.params["winner"], "alice")
            self.assertEqual(event.params["reason"], "five_in_a_row")
            alice_second.close()
            bob.close()
            observer.close()
        finally:
            h.shutdown()


    def test_reconnect_with_auth_token_resumes_game(self) -> None:
        # §5.4: reconnect-by-name with an auth token still cancels the
        # disconnect-grace forfeit and resumes play.
        h = _ListenerHarness(auth_token="s3cret")
        try:
            settings = MatchSettings(
                deadline_ms_per_move=60_000, disconnect_grace_ms=2_000
            )
            alice_first = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=lambda *_: None,
                auth_token="s3cret",
            )
            alice_first.register()
            bob = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="bob",
                on_turn=_swap2_bob_picker,
                auth_token="s3cret",
            )
            bob.register()
            observer = ObserverClient(connect_tcp("127.0.0.1", h.port))
            observer.handshake(auth_token="s3cret")
            observer.subscribe()

            game_id = h.server.create_match("alice", "bob", settings)
            time.sleep(0.2)
            alice_first.close()
            time.sleep(0.3)

            alice_second = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=_swap2_alice_picker,
                auth_token="s3cret",
            )
            alice_second.register()
            event = observer.wait_for_event("game_finished", timeout=10.0)
            self.assertEqual(event.params["winner"], "alice")
            self.assertEqual(event.params["reason"], "five_in_a_row")
            self.assertEqual(event.params["game_id"], game_id)
            alice_second.close()
            bob.close()
            observer.close()
        finally:
            h.shutdown()

    def test_reconnect_after_grace_expiry_returns_finished_game(self) -> None:
        # §5.4: after the grace window elapses the disconnect forfeit
        # fires; a same-name reconnect arrives at a finished game.
        h = _ListenerHarness()
        try:
            settings = MatchSettings(
                deadline_ms_per_move=60_000,
                disconnect_grace_ms=300,
            )
            alice_first = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=lambda *_: None,
            )
            alice_first.register()
            bob = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="bob",
                on_turn=lambda s, _: s["legal_actions"][0],
            )
            bob.register()
            observer = ObserverClient(connect_tcp("127.0.0.1", h.port))
            observer.handshake()
            observer.subscribe()

            game_id = h.server.create_match("alice", "bob", settings)
            time.sleep(0.2)
            alice_first.close()
            # Wait past the 300 ms grace; the clock loop forfeits the
            # game with reason="disconnect" before reconnect arrives.
            event = observer.wait_for_event("game_finished", timeout=5.0)
            self.assertEqual(event.params["reason"], "disconnect")
            self.assertEqual(event.params["winner"], "bob")
            self.assertEqual(event.params["game_id"], game_id)

            # A late reconnect under the same name still succeeds at the
            # protocol level, but query_state shows the finished game.
            alice_second = PlayerClient(
                connect_tcp("127.0.0.1", h.port),
                name="alice",
                on_turn=lambda *_: None,
            )
            alice_second.register()
            state = observer.query_state(game_id)
            self.assertTrue(state["finished"])
            self.assertEqual(state["result"], "PLAYER_B_WIN")
            alice_second.close()
            bob.close()
            observer.close()
        finally:
            h.shutdown()


if __name__ == "__main__":
    unittest.main()
