"""SQLite persistence for match records.

The match server treats this as a write-through journal: every match
creation, submitted move, and final result is written to disk. Crashes
or restarts can replay the journal to reconstruct in-flight games or
inspect finished ones; downstream tooling (Elo curves, post-game
analysis) reads from the same database.

The schema is intentionally narrow — it captures only what the
protocol surfaces, so a different rules engine or a future protocol
extension can still read these rows.
"""

from __future__ import annotations

import datetime as _dt
import os
import sqlite3
import threading
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator


_SCHEMA = """
CREATE TABLE IF NOT EXISTS matches (
    game_id TEXT PRIMARY KEY,
    player_a_name TEXT NOT NULL,
    player_b_name TEXT NOT NULL,
    board_size INTEGER NOT NULL,
    rule_variant INTEGER NOT NULL,
    deadline_ms_per_move INTEGER NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    result TEXT,
    reason TEXT
);
CREATE TABLE IF NOT EXISTS moves (
    game_id TEXT NOT NULL,
    ply INTEGER NOT NULL,
    action_id INTEGER NOT NULL,
    label TEXT NOT NULL,
    by_player_name TEXT NOT NULL,
    applied_at TEXT NOT NULL,
    PRIMARY KEY (game_id, ply),
    FOREIGN KEY (game_id) REFERENCES matches(game_id)
);
CREATE INDEX IF NOT EXISTS idx_matches_finished_at ON matches(finished_at);
CREATE INDEX IF NOT EXISTS idx_matches_player_a ON matches(player_a_name);
CREATE INDEX IF NOT EXISTS idx_matches_player_b ON matches(player_b_name);
"""


def _utc_now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="microseconds")


@dataclass(frozen=True)
class MatchRecord:
    game_id: str
    player_a_name: str
    player_b_name: str
    board_size: int
    deadline_ms_per_move: int
    started_at: str
    finished_at: str | None
    result: str | None
    reason: str | None
    # Legacy: the schema retains a ``rule_variant`` column from when
    # FREESTYLE was supported. New rows always store ``0`` (EXACT_FIVE);
    # old rows surface their stored value here for read-back fidelity.
    rule_variant: int = 0


@dataclass(frozen=True)
class MoveRecord:
    game_id: str
    ply: int
    action_id: int
    label: str
    by_player_name: str
    applied_at: str


class MatchStore:
    """Thread-safe SQLite journal for matches and moves."""

    def __init__(self, db_path: str | os.PathLike[str]) -> None:
        self.path = Path(db_path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # check_same_thread=False so the server's request threads can
        # all share one connection; we serialise writes with the lock.
        self._conn = sqlite3.connect(str(self.path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        # SQLite ignores FOREIGN KEY constraints unless this PRAGMA is set
        # on each connection; the ``moves(game_id) → matches(game_id)``
        # constraint in the schema only takes effect after this is on.
        self._conn.execute("PRAGMA foreign_keys = ON")
        self._lock = threading.Lock()
        with self._tx():
            self._conn.executescript(_SCHEMA)

    def close(self) -> None:
        with self._lock:
            if self._conn is not None:
                self._conn.close()
                self._conn = None  # type: ignore[assignment]

    def __enter__(self) -> "MatchStore":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @contextmanager
    def _tx(self) -> Iterator[sqlite3.Connection]:
        with self._lock:
            try:
                yield self._conn
                self._conn.commit()
            except Exception:
                self._conn.rollback()
                raise

    # ----- Writes ------------------------------------------------------

    def record_match_started(
        self,
        *,
        game_id: str,
        player_a_name: str,
        player_b_name: str,
        board_size: int,
        deadline_ms_per_move: int,
        rule_variant: int = 0,
        started_at: str | None = None,
    ) -> None:
        ts = started_at or _utc_now()
        # ``INSERT OR IGNORE`` so a crash-recovery replay of the journal can
        # call ``record_match_started`` for a game that's already on disk
        # without raising IntegrityError. The recovery path treats the
        # disk version as canonical.
        with self._tx() as conn:
            conn.execute(
                """
                INSERT OR IGNORE INTO matches (
                    game_id, player_a_name, player_b_name,
                    board_size, rule_variant, deadline_ms_per_move,
                    started_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    game_id,
                    player_a_name,
                    player_b_name,
                    int(board_size),
                    int(rule_variant),
                    int(deadline_ms_per_move),
                    ts,
                ),
            )

    def record_move(
        self,
        *,
        game_id: str,
        ply: int,
        action_id: int,
        label: str,
        by_player_name: str,
        applied_at: str | None = None,
    ) -> None:
        ts = applied_at or _utc_now()
        # ``INSERT OR IGNORE``: the (game_id, ply) primary key makes a
        # second insert with the same ply a no-op rather than an
        # IntegrityError. This is the idempotency contract for journal
        # replay after a crash.
        with self._tx() as conn:
            conn.execute(
                """
                INSERT OR IGNORE INTO moves (
                    game_id, ply, action_id, label, by_player_name, applied_at
                ) VALUES (?, ?, ?, ?, ?, ?)
                """,
                (game_id, int(ply), int(action_id), label, by_player_name, ts),
            )

    def record_match_finished(
        self,
        *,
        game_id: str,
        result: str,
        reason: str,
        finished_at: str | None = None,
    ) -> None:
        ts = finished_at or _utc_now()
        with self._tx() as conn:
            conn.execute(
                """
                UPDATE matches
                SET finished_at = ?, result = ?, reason = ?
                WHERE game_id = ?
                """,
                (ts, result, reason, game_id),
            )

    # ----- Reads -------------------------------------------------------

    def get_match(self, game_id: str) -> MatchRecord | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM matches WHERE game_id = ?", (game_id,)
            ).fetchone()
        return self._row_to_match(row) if row is not None else None

    def list_matches(
        self,
        *,
        finished_only: bool = False,
        player_name: str | None = None,
    ) -> list[MatchRecord]:
        clauses: list[str] = []
        params: list[object] = []
        if finished_only:
            clauses.append("finished_at IS NOT NULL")
        if player_name is not None:
            clauses.append("(player_a_name = ? OR player_b_name = ?)")
            params.extend([player_name, player_name])
        sql = "SELECT * FROM matches"
        if clauses:
            sql += " WHERE " + " AND ".join(clauses)
        # Order by finish time so a journal replay applies Elo updates in the
        # same order the live server saw them. ``started_at`` is the fallback
        # for in-flight matches (finished_at IS NULL).
        sql += " ORDER BY COALESCE(finished_at, started_at) ASC, started_at ASC"
        with self._lock:
            rows = self._conn.execute(sql, params).fetchall()
        return [self._row_to_match(row) for row in rows]

    def list_moves(self, game_id: str) -> list[MoveRecord]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM moves WHERE game_id = ? ORDER BY ply ASC",
                (game_id,),
            ).fetchall()
        return [
            MoveRecord(
                game_id=row["game_id"],
                ply=int(row["ply"]),
                action_id=int(row["action_id"]),
                label=row["label"],
                by_player_name=row["by_player_name"],
                applied_at=row["applied_at"],
            )
            for row in rows
        ]

    @staticmethod
    def _row_to_match(row: sqlite3.Row) -> MatchRecord:
        return MatchRecord(
            game_id=row["game_id"],
            player_a_name=row["player_a_name"],
            player_b_name=row["player_b_name"],
            board_size=int(row["board_size"]),
            rule_variant=int(row["rule_variant"]),
            deadline_ms_per_move=int(row["deadline_ms_per_move"]),
            started_at=row["started_at"],
            finished_at=row["finished_at"],
            result=row["result"],
            reason=row["reason"],
        )
