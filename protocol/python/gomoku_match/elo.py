"""Centralised Elo rating computation.

Reads finished matches from a :class:`MatchStore` and maintains two
auxiliary tables in the same SQLite database:

- ``ratings`` — one row per player (``player_name`` → ``rating``,
  ``games_played``, ``last_match_id``, ``last_updated``).
- ``match_ratings`` — one row per finished game keyed by ``game_id``
  recording each side's pre/post rating and the K-factor used. Lets
  downstream tooling reconstruct rating curves and audit deltas.

Rating math is standard Elo:

    expected_a = 1 / (1 + 10 ** ((rating_b - rating_a) / 400))
    new_rating_a = rating_a + K * (score_a - expected_a)

The K-factor schedule reduces volatility as a player accumulates games
(40 → 20 → 10 at 30 / 100 game thresholds). Both sides update with K
based on their *own* games-played count, so a veteran playing a newcomer
moves the newcomer's rating much faster than the veteran's. Initial
rating defaults to 1200, matching the de-facto open-source convention.

Usage::

    store = MatchStore(path)
    elo = EloEngine(store)
    # When MatchServer finishes a game, it calls:
    delta = elo.update_after_match(
        game_id="g1",
        player_a_name="alice",
        player_b_name="bob",
        result="PLAYER_A_WIN",  # or PLAYER_B_WIN / DRAW
    )
    print(elo.get_rating("alice"), elo.leaderboard()[:5])
"""

from __future__ import annotations

import datetime as _dt
import sqlite3
import threading
from dataclasses import dataclass
from typing import Iterable

from .persistence import MatchStore

DEFAULT_INITIAL_RATING = 1200.0


def _utc_now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="microseconds")


_SCHEMA = """
CREATE TABLE IF NOT EXISTS ratings (
    player_name TEXT PRIMARY KEY,
    rating REAL NOT NULL,
    games_played INTEGER NOT NULL,
    last_match_id TEXT,
    last_updated TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS match_ratings (
    game_id TEXT PRIMARY KEY,
    player_a_name TEXT NOT NULL,
    player_b_name TEXT NOT NULL,
    pre_rating_a REAL NOT NULL,
    post_rating_a REAL NOT NULL,
    pre_rating_b REAL NOT NULL,
    post_rating_b REAL NOT NULL,
    k_factor_a REAL NOT NULL,
    k_factor_b REAL NOT NULL,
    score_a REAL NOT NULL,
    result TEXT NOT NULL,
    recorded_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_ratings_rating ON ratings(rating DESC);
"""


@dataclass(frozen=True)
class RatingRow:
    player_name: str
    rating: float
    games_played: int
    last_match_id: str | None
    last_updated: str


@dataclass(frozen=True)
class MatchRatingRow:
    game_id: str
    player_a_name: str
    player_b_name: str
    pre_rating_a: float
    post_rating_a: float
    pre_rating_b: float
    post_rating_b: float
    k_factor_a: float
    k_factor_b: float
    score_a: float
    result: str
    recorded_at: str


def expected_score(rating_a: float, rating_b: float) -> float:
    """Probability that A beats B under standard Elo (no draw model)."""
    return 1.0 / (1.0 + 10.0 ** ((rating_b - rating_a) / 400.0))


def k_factor_for(games_played: int) -> float:
    """Default K-factor schedule.

    The early K=40 is large enough for a newcomer's rating to converge
    after ~30 games; we then drop to 20 for the bulk of the playing
    career and 10 once we trust the rating to be settled.
    """
    if games_played < 30:
        return 40.0
    if games_played < 100:
        return 20.0
    return 10.0


def _score_a_for_result(result: str) -> float:
    if result == "PLAYER_A_WIN":
        return 1.0
    if result == "PLAYER_B_WIN":
        return 0.0
    if result == "DRAW":
        return 0.5
    raise ValueError(f"unsupported result {result!r} (expected PLAYER_*_WIN or DRAW)")


class EloEngine:
    """Maintains an Elo leaderboard alongside a :class:`MatchStore`."""

    def __init__(
        self,
        store: MatchStore,
        *,
        initial_rating: float = DEFAULT_INITIAL_RATING,
    ) -> None:
        self._store = store
        self._initial_rating = float(initial_rating)
        self._lock = threading.Lock()
        # Reuse the MatchStore's connection so ratings live in the
        # same SQLite file. ``update_after_match`` acquires both locks
        # (our own, then the store's) — order is fixed there so we
        # never deadlock against another thread doing the same.
        with self._store._lock:  # noqa: SLF001 — intentional close coupling.
            self._store._conn.executescript(_SCHEMA)
            self._store._conn.commit()

    @property
    def initial_rating(self) -> float:
        return self._initial_rating

    # ----- Reads -----------------------------------------------------

    def get_rating(self, player_name: str) -> float:
        row = self._fetch_rating(player_name)
        return float(row["rating"]) if row is not None else self._initial_rating

    def get_games_played(self, player_name: str) -> int:
        row = self._fetch_rating(player_name)
        return int(row["games_played"]) if row is not None else 0

    def get_rating_row(self, player_name: str) -> RatingRow | None:
        row = self._fetch_rating(player_name)
        if row is None:
            return None
        return RatingRow(
            player_name=row["player_name"],
            rating=float(row["rating"]),
            games_played=int(row["games_played"]),
            last_match_id=row["last_match_id"],
            last_updated=row["last_updated"],
        )

    def leaderboard(self, *, limit: int | None = None) -> list[RatingRow]:
        sql = "SELECT * FROM ratings ORDER BY rating DESC, games_played DESC"
        if limit is not None:
            sql += f" LIMIT {int(limit)}"
        with self._store._lock:  # noqa: SLF001
            rows = self._store._conn.execute(sql).fetchall()
        return [
            RatingRow(
                player_name=r["player_name"],
                rating=float(r["rating"]),
                games_played=int(r["games_played"]),
                last_match_id=r["last_match_id"],
                last_updated=r["last_updated"],
            )
            for r in rows
        ]

    def get_match_rating(self, game_id: str) -> MatchRatingRow | None:
        with self._store._lock:  # noqa: SLF001
            row = self._store._conn.execute(
                "SELECT * FROM match_ratings WHERE game_id = ?", (game_id,)
            ).fetchone()
        if row is None:
            return None
        return MatchRatingRow(
            game_id=row["game_id"],
            player_a_name=row["player_a_name"],
            player_b_name=row["player_b_name"],
            pre_rating_a=float(row["pre_rating_a"]),
            post_rating_a=float(row["post_rating_a"]),
            pre_rating_b=float(row["pre_rating_b"]),
            post_rating_b=float(row["post_rating_b"]),
            k_factor_a=float(row["k_factor_a"]),
            k_factor_b=float(row["k_factor_b"]),
            score_a=float(row["score_a"]),
            result=row["result"],
            recorded_at=row["recorded_at"],
        )

    # ----- Updates ---------------------------------------------------

    def update_after_match(
        self,
        *,
        game_id: str,
        player_a_name: str,
        player_b_name: str,
        result: str,
    ) -> MatchRatingRow:
        """Update both players' ratings and return the audit row.

        Idempotent on ``game_id``: a second call with the same
        ``game_id`` raises so that re-running the journal cannot
        double-apply rating deltas.

        ``player_a_name == player_b_name`` is rejected. A self-match
        has no Elo signal — both sides start with the same pre-rating
        and the standard formula yields ``post = pre`` regardless of
        result, but the two ``INSERT OR REPLACE`` statements clobber
        each other on the same primary key and only credit a single
        ``games_played`` increment. Failing fast keeps callers from
        silently corrupting the leaderboard.
        """
        if player_a_name == player_b_name:
            raise ValueError(
                "Elo update requires distinct players; got"
                f" player_a_name == player_b_name == {player_a_name!r}"
            )
        score_a = _score_a_for_result(result)
        with self._lock, self._store._lock:  # noqa: SLF001
            conn = self._store._conn  # noqa: SLF001
            existing = conn.execute(
                "SELECT 1 FROM match_ratings WHERE game_id = ?", (game_id,)
            ).fetchone()
            if existing is not None:
                raise ValueError(
                    f"match_ratings already has an entry for game_id={game_id!r}"
                )
            pre_a, games_a = self._read_or_default(conn, player_a_name)
            pre_b, games_b = self._read_or_default(conn, player_b_name)
            expected_a = expected_score(pre_a, pre_b)
            k_a = k_factor_for(games_a)
            k_b = k_factor_for(games_b)
            post_a = pre_a + k_a * (score_a - expected_a)
            post_b = pre_b + k_b * ((1.0 - score_a) - (1.0 - expected_a))
            now = _utc_now()
            conn.execute(
                """
                INSERT OR REPLACE INTO ratings (
                    player_name, rating, games_played,
                    last_match_id, last_updated
                ) VALUES (?, ?, ?, ?, ?)
                """,
                (player_a_name, post_a, games_a + 1, game_id, now),
            )
            conn.execute(
                """
                INSERT OR REPLACE INTO ratings (
                    player_name, rating, games_played,
                    last_match_id, last_updated
                ) VALUES (?, ?, ?, ?, ?)
                """,
                (player_b_name, post_b, games_b + 1, game_id, now),
            )
            conn.execute(
                """
                INSERT INTO match_ratings (
                    game_id, player_a_name, player_b_name,
                    pre_rating_a, post_rating_a,
                    pre_rating_b, post_rating_b,
                    k_factor_a, k_factor_b, score_a, result, recorded_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    game_id, player_a_name, player_b_name,
                    pre_a, post_a, pre_b, post_b,
                    k_a, k_b, score_a, result, now,
                ),
            )
            conn.commit()
        return MatchRatingRow(
            game_id=game_id,
            player_a_name=player_a_name,
            player_b_name=player_b_name,
            pre_rating_a=pre_a,
            post_rating_a=post_a,
            pre_rating_b=pre_b,
            post_rating_b=post_b,
            k_factor_a=k_a,
            k_factor_b=k_b,
            score_a=score_a,
            result=result,
            recorded_at=now,
        )

    def replay_from_store(self, *, game_ids: Iterable[str] | None = None) -> int:
        """Backfill ratings by walking finished matches in ``MatchStore`` order.

        Useful when an ``EloEngine`` is attached to an existing journal
        for the first time. Returns the number of matches applied.
        Skips matches that already have a row in ``match_ratings``.
        """
        if game_ids is None:
            matches = self._store.list_matches(finished_only=True)
        else:
            matches = []
            for gid in game_ids:
                rec = self._store.get_match(gid)
                if rec is not None and rec.finished_at is not None:
                    matches.append(rec)
        applied = 0
        for rec in matches:
            if rec.result not in ("PLAYER_A_WIN", "PLAYER_B_WIN", "DRAW"):
                continue
            with self._store._lock:  # noqa: SLF001
                already = self._store._conn.execute(  # noqa: SLF001
                    "SELECT 1 FROM match_ratings WHERE game_id = ?",
                    (rec.game_id,),
                ).fetchone()
            if already is not None:
                continue
            self.update_after_match(
                game_id=rec.game_id,
                player_a_name=rec.player_a_name,
                player_b_name=rec.player_b_name,
                result=rec.result,
            )
            applied += 1
        return applied

    # ----- Internals -------------------------------------------------

    def _fetch_rating(self, player_name: str) -> sqlite3.Row | None:
        with self._store._lock:  # noqa: SLF001
            return self._store._conn.execute(  # noqa: SLF001
                "SELECT * FROM ratings WHERE player_name = ?", (player_name,)
            ).fetchone()

    def _read_or_default(
        self, conn: sqlite3.Connection, player_name: str
    ) -> tuple[float, int]:
        row = conn.execute(
            "SELECT rating, games_played FROM ratings WHERE player_name = ?",
            (player_name,),
        ).fetchone()
        if row is None:
            return self._initial_rating, 0
        return float(row["rating"]), int(row["games_played"])
