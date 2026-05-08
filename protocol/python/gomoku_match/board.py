"""Pure-Python Gomoku Swap2 rules engine.

The match server uses this as its canonical truth: every submitted move
is validated against ``Board.legal_actions()``. The engine covers all
five Swap2 phases, exact-five terminal detection (a run of exactly five
same-colour stones — six-or-more does **not** win), and a
JSON-serialisable state snapshot suitable for ``your_turn`` and
``state_changed`` event payloads.

This module has **no dependency on gomoku_az**. The action ID layout is
the dense scheme described in ``docs/protocol_v2.md``:

* ``0 .. board_size**2 - 1`` — board placements, ``action_id = x + y * board_size``.
* ``board_size**2 + 0`` — ``swap2_choose_white``: responder takes white;
  opener keeps black; standard play resumes with white to move.
* ``board_size**2 + 1`` — ``swap2_choose_black``: responder takes black;
  opener becomes white; standard play resumes with white to move.
* ``board_size**2 + 2`` — ``swap2_place_two``: responder declines color
  and places two more stones (white, then black) before passing the
  color choice back to the opener.
* ``board_size**2 + 3`` — ``choose_white``: opener takes white after
  the responder placed two more stones.
* ``board_size**2 + 4`` — ``choose_black``: opener takes black.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Iterable


class GamePhase(IntEnum):
    PLACE_INITIAL_THREE = 0
    SWAP2_DECISION = 1
    SWAP2_PLACE_TWO = 2
    CHOOSE_COLOR = 3
    STANDARD = 4


class GameResult(IntEnum):
    UNDETERMINED = 0
    PLAYER_A_WIN = 1
    PLAYER_B_WIN = 2
    DRAW = 3


class Player(IntEnum):
    A = 0
    B = 1


class Stone(IntEnum):
    EMPTY = 0
    BLACK = 1
    WHITE = 2


class IllegalActionError(ValueError):
    """Raised when an action is illegal in the current board state."""


_CONTROL_NAMES = (
    "swap2_choose_white",  # offset 0
    "swap2_choose_black",  # offset 1
    "swap2_place_two",     # offset 2
    "choose_white",        # offset 3
    "choose_black",        # offset 4
)
CONTROL_ACTION_COUNT = len(_CONTROL_NAMES)


@dataclass(frozen=True)
class BoardConfig:
    size: int = 15


@dataclass(frozen=True)
class Action:
    """Decoded representation of an action id.

    ``label`` is human-readable; ``id`` is the canonical wire form.
    """

    id: int
    label: str

    @classmethod
    def placement(cls, x: int, y: int, board_size: int) -> "Action":
        if board_size <= 0:
            raise ValueError(f"board_size must be positive, got {board_size}")
        if not (0 <= x < board_size and 0 <= y < board_size):
            raise ValueError(
                f"placement ({x}, {y}) out of bounds for board_size={board_size}"
            )
        return cls(id=x + y * board_size, label=f"({x},{y})")

    @classmethod
    def control(cls, name: str, board_size: int) -> "Action":
        if name not in _CONTROL_NAMES:
            raise ValueError(f"unknown control action '{name}'")
        offset = _CONTROL_NAMES.index(name)
        return cls(id=board_size * board_size + offset, label=name)


def decode_action_label(label: str, board_size: int) -> int:
    """Translate a label such as ``"(8,7)"`` or ``"swap2_place_two"`` to an id."""

    if label in _CONTROL_NAMES:
        return board_size * board_size + _CONTROL_NAMES.index(label)
    if label.startswith("(") and label.endswith(")"):
        body = label[1:-1].split(",")
        if len(body) == 2:
            try:
                x = int(body[0])
                y = int(body[1])
            except ValueError as exc:
                raise ValueError(f"bad placement label {label!r}") from exc
            if not (0 <= x < board_size and 0 <= y < board_size):
                raise ValueError(f"label {label!r} out of bounds")
            return x + y * board_size
    raise ValueError(f"unrecognised action label {label!r}")


def encode_action_label(action_id: int, board_size: int) -> str:
    if action_id < 0:
        raise ValueError(f"negative action_id {action_id}")
    if action_id < board_size * board_size:
        x = action_id % board_size
        y = action_id // board_size
        return f"({x},{y})"
    offset = action_id - board_size * board_size
    if 0 <= offset < CONTROL_ACTION_COUNT:
        return _CONTROL_NAMES[offset]
    raise ValueError(f"action_id {action_id} out of range")


_DIRECTIONS: tuple[tuple[int, int], ...] = ((1, 0), (0, 1), (1, 1), (1, -1))


@dataclass
class Board:
    """Mutable Swap2 Gomoku board.

    Keeps full move history so observers can reconstruct the game from
    scratch and so the server can replay state for late-joining clients.
    """

    config: BoardConfig = field(default_factory=BoardConfig)
    cells: list[int] = field(init=False)
    move_history: list[int] = field(default_factory=list, init=False)
    phase: GamePhase = field(default=GamePhase.PLACE_INITIAL_THREE, init=False)
    current_player: Player = field(default=Player.A, init=False)
    stone_to_place: Stone = field(default=Stone.BLACK, init=False)
    player_stones: dict[Player, Stone] = field(init=False)
    move_count: int = field(default=0, init=False)
    result: GameResult = field(default=GameResult.UNDETERMINED, init=False)

    def __post_init__(self) -> None:
        if self.config.size < 5:
            raise ValueError("board_size must be at least 5")
        self.cells = [int(Stone.EMPTY)] * (self.config.size * self.config.size)
        # Player-stone assignment is decided during/after Swap2.
        self.player_stones = {Player.A: Stone.EMPTY, Player.B: Stone.EMPTY}

    # ----- Read-only views -------------------------------------------

    @property
    def size(self) -> int:
        return self.config.size

    @property
    def board_action_count(self) -> int:
        return self.config.size * self.config.size

    @property
    def action_count(self) -> int:
        return self.board_action_count + CONTROL_ACTION_COUNT

    def at(self, x: int, y: int) -> Stone:
        return Stone(self.cells[x + y * self.config.size])

    def cells_2d(self) -> list[list[int]]:
        n = self.config.size
        return [self.cells[r * n : (r + 1) * n] for r in range(n)]

    def control_action_id(self, name: str) -> int:
        return Action.control(name, self.config.size).id

    def is_legal(self, action_id: int) -> bool:
        return action_id in self._legal_set()

    def legal_actions(self) -> list[int]:
        return sorted(self._legal_set())

    def legal_actions_mask(self) -> list[int]:
        # Returns ``list[int]`` (0/1) to mirror the ``vector<uint8_t>``
        # shape of the C++ ``Board::legal_actions_mask`` binding without
        # introducing a numpy dependency on this module. Callers that
        # iterate the result (the adapter layer in ``gomoku_az``) treat it
        # as a sequence of ints and coerce element-by-element.
        mask = [0] * self.action_count
        for a in self._legal_set():
            mask[a] = 1
        return mask

    def to_state_dict(self) -> dict:
        return {
            "board_size": self.config.size,
            "phase": self.phase.name,
            "phase_id": int(self.phase),
            "current_player": "A" if self.current_player == Player.A else "B",
            "stone_to_place": self.stone_to_place.name,
            "move_count": self.move_count,
            "player_stones": {
                "A": self.player_stones[Player.A].name,
                "B": self.player_stones[Player.B].name,
            },
            "result": self.result.name,
            "result_id": int(self.result),
            "moves": list(self.move_history),
            "cells": list(self.cells),
            "legal_actions": self.legal_actions(),
        }

    # ----- Mutators ---------------------------------------------------

    def apply(self, action_id: int) -> None:
        if self.result != GameResult.UNDETERMINED:
            raise IllegalActionError(
                f"game already finished with {self.result.name}"
            )
        if action_id not in self._legal_set():
            raise IllegalActionError(
                f"action {action_id} ({self._safe_label(action_id)}) "
                f"not legal in {self.phase.name}"
            )

        if action_id < self.board_action_count:
            self._apply_placement(action_id)
        else:
            self._apply_control(action_id - self.board_action_count)

        self.move_history.append(int(action_id))

    # ----- Internal: phase machine -----------------------------------

    def _apply_placement(self, action_id: int) -> None:
        x = action_id % self.config.size
        y = action_id // self.config.size
        self.cells[action_id] = int(self.stone_to_place)
        self.move_count += 1

        if self.phase == GamePhase.PLACE_INITIAL_THREE:
            # Opener (Player.A) places B-W-B. Stones alternate; current
            # player stays A throughout the three placements.
            if self.move_count == 3:
                self.phase = GamePhase.SWAP2_DECISION
                self.current_player = Player.B
                self.stone_to_place = Stone.EMPTY
            else:
                self.stone_to_place = (
                    Stone.WHITE if self.move_count == 1 else Stone.BLACK
                )
            return

        if self.phase == GamePhase.SWAP2_PLACE_TWO:
            # Responder (Player.B) places W then B (4th and 5th stones).
            if self.move_count == 4:
                # Just placed the white; next is black.
                self.stone_to_place = Stone.BLACK
            elif self.move_count == 5:
                self.phase = GamePhase.CHOOSE_COLOR
                self.current_player = Player.A
                self.stone_to_place = Stone.EMPTY
            return

        # STANDARD play.
        self._check_terminal_after_placement(x, y)
        if self.result != GameResult.UNDETERMINED:
            return
        # Alternate stones and players.
        self.current_player = Player.B if self.current_player == Player.A else Player.A
        self.stone_to_place = (
            Stone.WHITE if self.stone_to_place == Stone.BLACK else Stone.BLACK
        )

    def _apply_control(self, offset: int) -> None:
        name = _CONTROL_NAMES[offset]
        if name == "swap2_choose_white":
            # Responder takes white. Opener stays black. Standard play
            # begins with white to move (move 4).
            self.player_stones[Player.A] = Stone.BLACK
            self.player_stones[Player.B] = Stone.WHITE
            self.phase = GamePhase.STANDARD
            self.current_player = Player.B
            self.stone_to_place = Stone.WHITE
            return
        if name == "swap2_choose_black":
            # Responder takes black. Opener becomes white. Standard play
            # begins with white to move.
            self.player_stones[Player.A] = Stone.WHITE
            self.player_stones[Player.B] = Stone.BLACK
            self.phase = GamePhase.STANDARD
            self.current_player = Player.A
            self.stone_to_place = Stone.WHITE
            return
        if name == "swap2_place_two":
            self.phase = GamePhase.SWAP2_PLACE_TWO
            self.current_player = Player.B
            self.stone_to_place = Stone.WHITE
            return
        if name == "choose_white":
            # Opener (A) takes white after responder placed two more.
            # Five stones already placed (B, W, B, W, B).
            # Move 6 is white -> A plays.
            self.player_stones[Player.A] = Stone.WHITE
            self.player_stones[Player.B] = Stone.BLACK
            self.phase = GamePhase.STANDARD
            self.current_player = Player.A
            self.stone_to_place = Stone.WHITE
            return
        if name == "choose_black":
            # Opener (A) takes black. Move 6 is white -> B plays.
            self.player_stones[Player.A] = Stone.BLACK
            self.player_stones[Player.B] = Stone.WHITE
            self.phase = GamePhase.STANDARD
            self.current_player = Player.B
            self.stone_to_place = Stone.WHITE
            return
        raise IllegalActionError(f"unhandled control offset {offset}")

    # ----- Internal: legality ----------------------------------------

    def _legal_set(self) -> set[int]:
        if self.result != GameResult.UNDETERMINED:
            return set()
        n = self.board_action_count
        if self.phase in (
            GamePhase.PLACE_INITIAL_THREE,
            GamePhase.SWAP2_PLACE_TWO,
            GamePhase.STANDARD,
        ):
            return {a for a in range(n) if self.cells[a] == int(Stone.EMPTY)}
        if self.phase == GamePhase.SWAP2_DECISION:
            return {
                n + 0,  # swap2_choose_white
                n + 1,  # swap2_choose_black
                n + 2,  # swap2_place_two
            }
        if self.phase == GamePhase.CHOOSE_COLOR:
            return {
                n + 3,  # choose_white
                n + 4,  # choose_black
            }
        return set()

    def _safe_label(self, action_id: int) -> str:
        try:
            return encode_action_label(action_id, self.config.size)
        except ValueError:
            return f"<invalid id {action_id}>"

    # ----- Internal: terminal detection ------------------------------

    def _check_terminal_after_placement(self, x: int, y: int) -> None:
        stone = Stone(self.cells[x + y * self.config.size])
        if stone == Stone.EMPTY:
            return
        n = self.config.size
        for dx, dy in _DIRECTIONS:
            run = 1
            for sign in (-1, 1):
                cx, cy = x + sign * dx, y + sign * dy
                while 0 <= cx < n and 0 <= cy < n and self.cells[cx + cy * n] == int(stone):
                    run += 1
                    cx += sign * dx
                    cy += sign * dy
            # Exact-five rule: a run of *exactly* five wins; six-or-more
            # in any direction is not a win.
            if run == 5:
                self._set_winner_for_stone(stone)
                return

        if all(c != int(Stone.EMPTY) for c in self.cells):
            self.result = GameResult.DRAW

    def _set_winner_for_stone(self, stone: Stone) -> None:
        if self.player_stones[Player.A] == stone:
            self.result = GameResult.PLAYER_A_WIN
        elif self.player_stones[Player.B] == stone:
            self.result = GameResult.PLAYER_B_WIN
        else:
            # During PLACE_INITIAL_THREE / SWAP2_PLACE_TWO no winner can
            # be set because no five-in-a-row is reachable yet.
            self.result = GameResult.DRAW

    # ----- Convenience iteration -------------------------------------

    def replay(self, actions: Iterable[int]) -> None:
        for action_id in actions:
            self.apply(int(action_id))
