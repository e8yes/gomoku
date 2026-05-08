"""Gomocup-protocol engine adapter.

The Gomocup AI Gomoku tournament protocol predates ``gomoku_match`` by
~20 years and is the de-facto standard for legacy Gomoku engines. This
adapter spawns such an engine as a subprocess and bridges its
line-oriented stdio protocol to a :class:`gomoku_match.PlayerClient`,
so legacy engines can register against the standard match server and
accumulate Elo through the same pipeline as native engines.

Scope and Swap2 handling
------------------------

The Gomocup protocol natively covers only standard Gomoku — alternating
placements starting with black. It has no notion of Swap2's three
opening phases (``PLACE_INITIAL_THREE``, ``SWAP2_DECISION``,
``SWAP2_PLACE_TWO``, ``CHOOSE_COLOR``). The adapter handles those
phases locally with a configurable :class:`GomocupSwap2Strategy` (the
default is the "decline-swap" strategy commonly used in human play:
center cluster, take white, choose white again if asked). When the
match enters STANDARD phase, the adapter sends a single ``BOARD ...
DONE`` block to bring the engine up to speed with all the placements
that already happened, then translates each subsequent opponent move
to a ``TURN x,y`` and reads the engine's reply.

Wire-level subprocess protocol summary
--------------------------------------

Server → Engine (we send):
    INFO timeout_match <ms>
    INFO timeout_turn <ms>
    START <size>             — new game, board NxN
    BOARD                    — replay full state
    <x>,<y>,<who>            — one line per placed stone (who: 1=us, 2=opponent)
    DONE                     — end of board replay; engine responds with its move
    TURN <x>,<y>             — opponent played at (x,y); engine responds with its move
    END                      — game over; engine should exit cleanly

Engine → Server (we read):
    OK                       — ack of START
    <x>,<y>                  — engine's move
    DEBUG <free text> | MESSAGE <free text> | ERROR <free text>  — informational
"""

from __future__ import annotations

import subprocess
import threading
from dataclasses import dataclass, field
from typing import Any, Callable, Mapping, Sequence

from ..board import Action, GamePhase

OnTurnCallback = Callable[[Mapping[str, Any], int], int | str]


class GomocupEngineError(RuntimeError):
    """Raised when the subprocess engine misbehaves or exits unexpectedly."""


@dataclass(frozen=True)
class GomocupSwap2Strategy:
    """Plays the Swap2 opening phases without consulting the engine.

    A "decline-swap" strategy mimicking what most casual play uses:
    drop three center-cluster stones, accept white when offered, and
    if the responder picks ``swap2_place_two`` instead, take white in
    ``CHOOSE_COLOR``.
    """

    initial_three: Sequence[tuple[int, int]] = field(
        default=((7, 7), (8, 7), (7, 8))
    )
    swap2_decision: str = "swap2_choose_white"  # or swap2_choose_black / swap2_place_two
    place_two: Sequence[tuple[int, int]] = field(default=((8, 8), (6, 6)))
    choose_color: str = "choose_white"  # or choose_black

    def action_for_phase(
        self, state: Mapping[str, Any]
    ) -> int:
        size = int(state["board_size"])
        phase = state["phase"]
        if phase == GamePhase.PLACE_INITIAL_THREE.name:
            x, y = self.initial_three[int(state["move_count"])]
            return x + y * size
        if phase == GamePhase.SWAP2_DECISION.name:
            return Action.control(self.swap2_decision, size).id
        if phase == GamePhase.SWAP2_PLACE_TWO.name:
            # Two stones placed during this phase, in order: white then black.
            placed_in_phase = sum(
                1 for a in state["moves"] if int(a) < size * size
            ) - 3  # subtract the PLACE_INITIAL_THREE placements
            x, y = self.place_two[placed_in_phase]
            return x + y * size
        if phase == GamePhase.CHOOSE_COLOR.name:
            return Action.control(self.choose_color, size).id
        raise ValueError(f"action_for_phase called with unexpected phase {phase}")


class GomocupEngine:
    """Subprocess wrapper for a Gomocup-protocol engine."""

    def __init__(
        self,
        cmd: Sequence[str],
        *,
        board_size: int,
        timeout_turn_ms: int = 5000,
        timeout_match_ms: int = 1_000_000,
        startup_timeout_s: float = 5.0,
    ) -> None:
        self._cmd = list(cmd)
        self._board_size = board_size
        self._timeout_turn_ms = int(timeout_turn_ms)
        self._timeout_match_ms = int(timeout_match_ms)
        self._proc = subprocess.Popen(
            self._cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,  # line-buffered
        )
        self._send_lock = threading.Lock()
        try:
            self._send(f"INFO timeout_match {self._timeout_match_ms}")
            self._send(f"INFO timeout_turn {self._timeout_turn_ms}")
            self._send(f"START {board_size}")
            self._await_ok(timeout_s=startup_timeout_s)
        except Exception:
            self.close(grace_s=0.1)
            raise

    @property
    def board_size(self) -> int:
        return self._board_size

    # ----- Public API -------------------------------------------------

    def play_from_board(
        self, history: Sequence[tuple[int, int, int]]
    ) -> tuple[int, int]:
        """Send ``BOARD`` + history + ``DONE`` and read the engine's move.

        Each history entry is ``(x, y, who)`` where ``who`` is ``1``
        for the engine's own stones and ``2`` for the opponent's.
        """
        self._send("BOARD")
        for x, y, who in history:
            self._send(f"{int(x)},{int(y)},{int(who)}")
        self._send("DONE")
        return self._read_move()

    def play_after_opponent(self, x: int, y: int) -> tuple[int, int]:
        self._send(f"TURN {int(x)},{int(y)}")
        return self._read_move()

    def close(self, *, grace_s: float = 1.0) -> None:
        if self._proc.poll() is None:
            try:
                self._send("END")
            except Exception:
                pass
            try:
                self._proc.wait(timeout=grace_s)
            except subprocess.TimeoutExpired:
                self._proc.terminate()
                try:
                    self._proc.wait(timeout=grace_s)
                except subprocess.TimeoutExpired:
                    self._proc.kill()
        # Drain pipes so file descriptors are closed.
        for stream in (self._proc.stdin, self._proc.stdout, self._proc.stderr):
            try:
                if stream is not None:
                    stream.close()
            except Exception:  # pragma: no cover
                pass

    # ----- Wire helpers -----------------------------------------------

    def _send(self, line: str) -> None:
        if self._proc.stdin is None or self._proc.poll() is not None:
            raise GomocupEngineError("engine subprocess is not running")
        with self._send_lock:
            self._proc.stdin.write(line + "\n")
            self._proc.stdin.flush()

    def _readline(self, *, timeout_s: float | None = None) -> str:
        # subprocess.Popen.stdout doesn't natively support timeouts on
        # POSIX; we leave timeout enforcement to the caller's harness
        # (the match server's deadline machinery). If the engine never
        # responds the surrounding deadline expires and the player
        # forfeits as expected.
        if self._proc.stdout is None:
            raise GomocupEngineError("engine has no stdout")
        line = self._proc.stdout.readline()
        if line == "":
            raise GomocupEngineError("engine closed stdout before responding")
        return line.rstrip("\r\n")

    def _await_ok(self, *, timeout_s: float) -> None:
        # Some engines respond `OK`, others say nothing; we accept either.
        # We give up looking after seeing one chatty info line or one
        # blank line, whichever comes first.
        for _ in range(8):
            line = self._readline(timeout_s=timeout_s)
            up = line.upper().strip()
            if not up:
                return
            if up == "OK":
                return
            if up.startswith(("DEBUG", "MESSAGE", "INFO")):
                continue
            if up.startswith("ERROR"):
                raise GomocupEngineError(f"engine errored at startup: {line}")
            # Any other line is treated as a successful start.
            return
        raise GomocupEngineError("engine did not acknowledge START")

    def _read_move(self) -> tuple[int, int]:
        # Skip DEBUG/MESSAGE lines until we get the move.
        for _ in range(64):
            line = self._readline()
            up = line.upper()
            if up.startswith(("DEBUG", "MESSAGE", "INFO")):
                continue
            if up.startswith("ERROR"):
                raise GomocupEngineError(f"engine reported error: {line}")
            try:
                xs, ys = line.split(",", 1)
                return int(xs.strip()), int(ys.strip())
            except ValueError:
                # Unparseable; keep reading in case it's chatter.
                continue
        raise GomocupEngineError("engine did not return a parseable move")


def _decode_placement(action_id: int, size: int) -> tuple[int, int]:
    return action_id % size, action_id // size


def _stone_color_for_placement_index(
    placement_index: int, state: Mapping[str, Any]
) -> int:
    """Return ``1`` (BLACK) or ``2`` (WHITE) for the n-th placement.

    Placement order in Swap2 is fully determined by the rules engine:

    - ``0`` BLACK (player A's first opener)
    - ``1`` WHITE (player A's second opener)
    - ``2`` BLACK (player A's third opener)
    - ``3``... depends on responder branch.

    For the responder branch ``swap2_place_two`` (B places W then B):
    - ``3`` WHITE
    - ``4`` BLACK

    Everything from there on alternates by color. Rather than re-deriving
    the phase machine, we read the deterministic answer off the
    cells/move log: cells[y*size+x] is 1 (BLACK) or 2 (WHITE), and
    the placement order in ``moves`` matches the placement order on
    the board.
    """
    size = int(state["board_size"])
    cells = state["cells"]
    moves = state["moves"]
    placements = [a for a in moves if int(a) < size * size]
    if placement_index >= len(placements):
        raise IndexError(f"placement index {placement_index} out of range")
    aid = int(placements[placement_index])
    x, y = _decode_placement(aid, size)
    return int(cells[y * size + x])


def _build_board_history(
    state: Mapping[str, Any], my_stone_code: int
) -> list[tuple[int, int, int]]:
    """Flatten the state's placement log into ``(x, y, who)`` rows.

    ``my_stone_code`` is ``1`` if I'm playing black, ``2`` if white.
    Other-coloured placements become ``who=2``; my colour becomes
    ``who=1``.
    """
    size = int(state["board_size"])
    moves = state["moves"]
    placements = [a for a in moves if int(a) < size * size]
    history: list[tuple[int, int, int]] = []
    for i, aid in enumerate(placements):
        x, y = _decode_placement(int(aid), size)
        stone = _stone_color_for_placement_index(i, state)
        who = 1 if stone == my_stone_code else 2
        history.append((x, y, who))
    return history


def _player_label_to_role(state: Mapping[str, Any], my_label: str) -> int:
    """Return the BLACK/WHITE code (1/2) my player is using right now.

    ``my_label`` is ``"A"`` or ``"B"``. Looks up ``player_stones`` in
    the state payload and decodes ``"BLACK"``/``"WHITE"`` to ``1``/``2``.
    """
    stone_name = state["player_stones"][my_label]
    return 1 if stone_name == "BLACK" else 2 if stone_name == "WHITE" else 0


def make_gomocup_callback(
    engine_cmd: Sequence[str],
    *,
    swap2: GomocupSwap2Strategy | None = None,
    timeout_turn_ms: int = 5000,
    timeout_match_ms: int = 1_000_000,
    on_engine_started: Callable[[GomocupEngine], None] | None = None,
) -> tuple[OnTurnCallback, Callable[[], None]]:
    """Build a ``(on_turn, close)`` pair backed by a Gomocup subprocess.

    Pass ``on_turn`` to :class:`PlayerClient`. Call ``close`` to tear
    down the subprocess after the game; it is safe to call multiple
    times.
    """
    swap2_strategy = swap2 or GomocupSwap2Strategy()
    state_box: dict[str, Any] = {
        "engine": None,
        "synced": False,
    }

    def _ensure_engine(board_size: int) -> GomocupEngine:
        engine = state_box["engine"]
        if engine is None:
            engine = GomocupEngine(
                engine_cmd,
                board_size=board_size,
                timeout_turn_ms=timeout_turn_ms,
                timeout_match_ms=timeout_match_ms,
            )
            state_box["engine"] = engine
            if on_engine_started is not None:
                on_engine_started(engine)
        return engine

    def on_turn(state: Mapping[str, Any], _deadline_ms: int) -> int:
        size = int(state["board_size"])
        phase = state["phase"]
        if phase != GamePhase.STANDARD.name:
            # Bridge nothing to the engine; play Swap2 ourselves.
            return swap2_strategy.action_for_phase(state)

        my_label = state["current_player"]  # 'A' or 'B'
        my_stone_code = _player_label_to_role(state, my_label)
        engine = _ensure_engine(size)
        if not state_box["synced"]:
            history = _build_board_history(state, my_stone_code)
            x, y = engine.play_from_board(history)
            state_box["synced"] = True
            return x + y * size

        # Mid-game: send the opponent's most recent placement.
        placements = [
            int(a) for a in state["moves"] if int(a) < size * size
        ]
        last_opp_action = placements[-1]
        ox, oy = _decode_placement(last_opp_action, size)
        x, y = engine.play_after_opponent(ox, oy)
        return x + y * size

    def close() -> None:
        engine = state_box["engine"]
        if engine is not None:
            engine.close()
            state_box["engine"] = None

    return on_turn, close
