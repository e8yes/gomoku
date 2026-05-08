"""Tests for the Elo engine and its MatchServer integration."""

from __future__ import annotations

import math
import tempfile
import unittest
from pathlib import Path

from gomoku_match import (
    DEFAULT_INITIAL_RATING,
    Action,
    EloEngine,
    InProcessTransport,
    MatchServer,
    MatchSettings,
    MatchStore,
    ObserverClient,
    PlayerClient,
    expected_score,
    k_factor_for,
)


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


class EloMathTests(unittest.TestCase):
    def test_expected_score_symmetric(self) -> None:
        self.assertAlmostEqual(expected_score(1500, 1500), 0.5)
        self.assertAlmostEqual(
            expected_score(1500, 1700) + expected_score(1700, 1500), 1.0
        )

    def test_expected_score_400_gap_is_91pct(self) -> None:
        # Standard Elo property: a 400-point gap → 10:1 expected odds.
        self.assertAlmostEqual(
            expected_score(1900, 1500), 10.0 / 11.0, places=4
        )

    def test_k_factor_schedule(self) -> None:
        self.assertEqual(k_factor_for(0), 40.0)
        self.assertEqual(k_factor_for(29), 40.0)
        self.assertEqual(k_factor_for(30), 20.0)
        self.assertEqual(k_factor_for(99), 20.0)
        self.assertEqual(k_factor_for(100), 10.0)
        self.assertEqual(k_factor_for(10_000), 10.0)


class EloEngineTests(unittest.TestCase):
    def test_default_rating_for_unseen_player(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            try:
                self.assertEqual(elo.get_rating("alice"), DEFAULT_INITIAL_RATING)
                self.assertEqual(elo.get_games_played("alice"), 0)
                self.assertIsNone(elo.get_rating_row("alice"))
            finally:
                store.close()

    def test_first_match_updates_both_sides(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            try:
                store.record_match_started(
                    game_id="g1", player_a_name="alice", player_b_name="bob",
                    board_size=15, deadline_ms_per_move=5000,
                )
                store.record_match_finished(
                    game_id="g1", result="PLAYER_A_WIN", reason="five_in_a_row"
                )
                row = elo.update_after_match(
                    game_id="g1", player_a_name="alice",
                    player_b_name="bob", result="PLAYER_A_WIN",
                )
                # Both started at 1200; expected_a = 0.5; K=40 (new player).
                # post_a = 1200 + 40 * (1 - 0.5) = 1220.
                # post_b = 1200 + 40 * (0 - 0.5) = 1180.
                self.assertAlmostEqual(row.post_rating_a, 1220.0)
                self.assertAlmostEqual(row.post_rating_b, 1180.0)
                self.assertEqual(elo.get_games_played("alice"), 1)
                self.assertEqual(elo.get_games_played("bob"), 1)
                # Audit row reachable by game_id.
                replay = elo.get_match_rating("g1")
                self.assertIsNotNone(replay)
                assert replay is not None
                self.assertAlmostEqual(replay.post_rating_a, 1220.0)
            finally:
                store.close()

    def test_draw_pulls_ratings_toward_each_other(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            try:
                # Seed alice with a higher rating via a loss for bob first.
                for i in range(5):
                    store.record_match_started(
                        game_id=f"seed{i}", player_a_name="alice",
                        player_b_name="bob", board_size=15,
                        deadline_ms_per_move=5000,
                    )
                    store.record_match_finished(
                        game_id=f"seed{i}", result="PLAYER_A_WIN",
                        reason="five_in_a_row",
                    )
                    elo.update_after_match(
                        game_id=f"seed{i}", player_a_name="alice",
                        player_b_name="bob", result="PLAYER_A_WIN",
                    )
                pre_a = elo.get_rating("alice")
                pre_b = elo.get_rating("bob")
                self.assertGreater(pre_a, pre_b)

                store.record_match_started(
                    game_id="draw1", player_a_name="alice",
                    player_b_name="bob", board_size=15,
                    deadline_ms_per_move=5000,
                )
                store.record_match_finished(
                    game_id="draw1", result="DRAW", reason="draw"
                )
                elo.update_after_match(
                    game_id="draw1", player_a_name="alice",
                    player_b_name="bob", result="DRAW",
                )
                # After a draw, the favourite (alice) loses points and
                # the underdog (bob) gains them, net-zero in aggregate.
                post_a = elo.get_rating("alice")
                post_b = elo.get_rating("bob")
                self.assertLess(post_a, pre_a)
                self.assertGreater(post_b, pre_b)
                self.assertAlmostEqual(
                    (pre_a + pre_b) - (post_a + post_b), 0.0, places=6
                )
            finally:
                store.close()

    def test_double_record_for_same_game_id_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            try:
                elo.update_after_match(
                    game_id="g1", player_a_name="alice",
                    player_b_name="bob", result="PLAYER_A_WIN",
                )
                with self.assertRaises(ValueError):
                    elo.update_after_match(
                        game_id="g1", player_a_name="alice",
                        player_b_name="bob", result="PLAYER_A_WIN",
                    )
            finally:
                store.close()

    def test_replay_from_store_backfills_ratings(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "m.sqlite"
            store = MatchStore(path)
            try:
                # Record three finished matches before any Elo engine
                # exists — this is the "attach to existing journal" case.
                for i, result in enumerate(
                    ["PLAYER_A_WIN", "DRAW", "PLAYER_B_WIN"]
                ):
                    gid = f"g{i}"
                    store.record_match_started(
                        game_id=gid, player_a_name="alice",
                        player_b_name="bob", board_size=15,
                        deadline_ms_per_move=5000,
                    )
                    store.record_match_finished(
                        game_id=gid, result=result, reason="ok"
                    )
                elo = EloEngine(store)
                applied = elo.replay_from_store()
                self.assertEqual(applied, 3)
                # Alice 1W/1D/1L vs same opponent at the same start
                # rating → her rating returns close to the start (the
                # rating-conservation property of basic Elo).
                self.assertAlmostEqual(
                    elo.get_rating("alice") + elo.get_rating("bob"),
                    2 * DEFAULT_INITIAL_RATING,
                    places=6,
                )
                # Replay is idempotent.
                self.assertEqual(elo.replay_from_store(), 0)
            finally:
                store.close()

    def test_leaderboard_orders_by_rating(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            try:
                elo.update_after_match(
                    game_id="g1", player_a_name="alice",
                    player_b_name="bob", result="PLAYER_A_WIN",
                )
                elo.update_after_match(
                    game_id="g2", player_a_name="carol",
                    player_b_name="alice", result="PLAYER_B_WIN",
                )
                board = elo.leaderboard()
                self.assertEqual([r.player_name for r in board[:1]], ["alice"])
                self.assertEqual({r.player_name for r in board}, {"alice", "bob", "carol"})
            finally:
                store.close()


class MatchServerEloIntegrationTests(unittest.TestCase):
    def test_finished_game_updates_ratings_and_emits_them(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = MatchStore(Path(td) / "m.sqlite")
            elo = EloEngine(store)
            server = MatchServer(store=store, elo=elo, clock_resolution_s=0.02)
            try:
                # Set up scripted players over in-process transports.
                def attach() -> InProcessTransport:
                    client_side, server_side = InProcessTransport.pair()
                    server.attach(server_side)
                    return client_side

                alice = PlayerClient(attach(), name="alice", on_turn=_swap2_alice_picker)
                bob = PlayerClient(attach(), name="bob", on_turn=_swap2_bob_picker)
                alice.register()
                bob.register()
                observer = ObserverClient(attach())
                observer.handshake()
                observer.subscribe()

                gid = server.create_match(
                    "alice", "bob", MatchSettings(deadline_ms_per_move=2000)
                )
                event = observer.wait_for_event("game_finished", timeout=10.0)
                self.assertEqual(event.params["winner"], "alice")
                # Ratings payload is in the broadcast.
                ratings = event.params["ratings"]
                self.assertGreater(
                    ratings["player_a"]["post"], ratings["player_a"]["pre"]
                )
                self.assertLess(
                    ratings["player_b"]["post"], ratings["player_b"]["pre"]
                )
                # Engine state matches the broadcast payload.
                self.assertAlmostEqual(
                    elo.get_rating("alice"), ratings["player_a"]["post"]
                )
                # Audit row exists.
                audit = elo.get_match_rating(gid)
                self.assertIsNotNone(audit)
                assert audit is not None
                self.assertEqual(audit.result, "PLAYER_A_WIN")
                alice.close()
                bob.close()
                observer.close()
            finally:
                server.shutdown()
                store.close()


if __name__ == "__main__":
    unittest.main()
