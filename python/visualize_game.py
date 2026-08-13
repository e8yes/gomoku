#!/usr/bin/env python3
"""Render Gomoku move traces and packed training positions as board images.

Examples:

    python visualize_game.py \\
        --moves-file examples/sample_game.json \\
        --output sample_game.png \\
        --title "sample game"

    python visualize_game.py \\
        --record ../artifacts/iteration_000.bin \\
        --index 0 \\
        --output training_position.png

Move files may be JSON arrays, ``{"moves": [...]}`` objects, or one move per
line. A move can be ``"(x,y)"``, ``"x,y"``, or an object with ``x``, ``y``,
and optionally ``player``/``color``. Player names are ``black`` and ``white``.
Swap2 control actions may be present in a trace; they are ignored because
they do not occupy a board coordinate.

The training shard format stores feature planes rather than move history, so
positions loaded with ``--record`` are rendered without move numbers.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


BOARD_SIZE = 15
SAMPLE_SIZE = 716
PACKED_STATE_BYTES = 254
CONTROL_ACTIONS = {
    "swap2_choose_white",
    "swap2_choose_black",
    "swap2_place_two",
    "choose_white",
    "choose_black",
}
MOVE_RE = re.compile(
    r"^\(?\s*(-?\d+)\s*[, ]\s*(-?\d+)\s*\)?"
    r"(?:\s+(black|white|b|w|1|2))?\s*$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Move:
    """One drawable placement, using the engine's x/y coordinates."""

    x: int
    y: int
    player: str
    number: int | None = None


def parse_player(value: Any) -> str | None:
    """Normalize a player value to ``black``, ``white``, or ``None``."""

    if value is None:
        return None
    if isinstance(value, (int, np.integer)):
        if int(value) == 1:
            return "black"
        if int(value) == 2:
            return "white"
    normalized = str(value).strip().lower()
    if normalized in {"black", "b", "1"}:
        return "black"
    if normalized in {"white", "w", "2"}:
        return "white"
    raise ValueError(f"Unknown player value: {value!r}")


def _parse_raw_move(raw: Any) -> tuple[int, int, str | None, int | None] | None:
    if isinstance(raw, dict):
        if "action" in raw and ("x" not in raw or "y" not in raw):
            action = str(raw["action"]).strip().lower()
            if action in CONTROL_ACTIONS:
                return None
            raw = raw["action"]
        else:
            if "x" not in raw or "y" not in raw:
                raise ValueError(f"Move object needs x and y: {raw!r}")
            return (
                int(raw["x"]),
                int(raw["y"]),
                parse_player(raw.get("player", raw.get("color"))),
                int(raw["number"]) if "number" in raw else None,
            )

    if isinstance(raw, (list, tuple)):
        if len(raw) not in {2, 3}:
            raise ValueError(f"Move sequence needs [x, y] or [x, y, player]: {raw!r}")
        return int(raw[0]), int(raw[1]), parse_player(raw[2] if len(raw) == 3 else None), None

    if isinstance(raw, str):
        text = raw.strip()
        if text.lower() in CONTROL_ACTIONS:
            return None
        match = MOVE_RE.match(text)
        if match is None:
            raise ValueError(f"Could not parse move: {raw!r}")
        return int(match.group(1)), int(match.group(2)), parse_player(match.group(3)), None

    raise ValueError(f"Unsupported move value: {raw!r}")


def normalize_moves(
    raw_moves: Iterable[Any],
    *,
    first_player: str = "black",
    label_start: int = 0,
) -> list[Move]:
    """Validate and normalize a trace, alternating omitted player colours."""

    next_player = parse_player(first_player)
    assert next_player is not None
    normalized: list[Move] = []
    occupied: set[tuple[int, int]] = set()

    for raw in raw_moves:
        parsed = _parse_raw_move(raw)
        if parsed is None:
            continue
        x, y, player, explicit_number = parsed
        if not 0 <= x < BOARD_SIZE or not 0 <= y < BOARD_SIZE:
            raise ValueError(f"Move ({x},{y}) is outside the 15x15 board")
        if (x, y) in occupied:
            raise ValueError(f"Duplicate placement at ({x},{y})")
        player = player or next_player
        occupied.add((x, y))
        number = explicit_number if explicit_number is not None else label_start + len(normalized)
        normalized.append(Move(x=x, y=y, player=player, number=number))
        next_player = "white" if player == "black" else "black"

    return normalized


def load_move_trace(path: str | Path) -> list[Any]:
    """Load a JSON or one-move-per-line trace."""

    source = Path(path)
    text = source.read_text(encoding="utf-8")
    try:
        payload = json.loads(text)
    except json.JSONDecodeError:
        return [line.strip() for line in text.splitlines() if line.strip()]

    if isinstance(payload, dict):
        payload = payload.get("moves", payload.get("actions"))
    if not isinstance(payload, list):
        raise ValueError("A move trace must be a JSON list or an object containing moves")
    return payload


def decode_training_record(path: str | Path, index: int = 0) -> list[Move]:
    """Decode one packed training state into coloured, unnumbered placements."""

    record_path = Path(path)
    size = record_path.stat().st_size
    count, remainder = divmod(size, SAMPLE_SIZE)
    if remainder:
        raise ValueError(f"{record_path} has {remainder} trailing bytes")
    if count == 0:
        raise ValueError(f"{record_path} contains no training records")
    if index < 0:
        index += count
    if not 0 <= index < count:
        raise IndexError(f"record index {index} outside [0, {count})")

    with record_path.open("rb") as stream:
        stream.seek(index * SAMPLE_SIZE)
        raw = stream.read(SAMPLE_SIZE)
    if len(raw) != SAMPLE_SIZE:
        raise ValueError(f"Could not read complete record {index} from {record_path}")

    packed = np.frombuffer(raw[:PACKED_STATE_BYTES], dtype=np.uint8)
    features = np.unpackbits(packed)[: 9 * BOARD_SIZE * BOARD_SIZE].reshape(
        9, BOARD_SIZE, BOARD_SIZE
    )

    # The feature encoder treats a missing stone-to-place value as Black for
    # channels 0/1, so the absence of both colour planes has the same useful
    # fallback here.
    current_is_black = bool(features[2].any() or not features[3].any())
    current_player = "black" if current_is_black else "white"
    opponent = "white" if current_is_black else "black"
    moves: list[Move] = []
    for y, x in zip(*np.nonzero(features[0])):
        moves.append(Move(int(x), int(y), current_player))
    for y, x in zip(*np.nonzero(features[1])):
        moves.append(Move(int(x), int(y), opponent))
    return sorted(moves, key=lambda move: (move.y, move.x, move.player))


def render_board(
    moves: Sequence[Move],
    output: str | Path,
    *,
    title: str = "Gomoku game",
    annotate_moves: bool = True,
    dpi: int = 160,
) -> Path:
    """Render a 15x15 board using the visual style of the reference image."""

    output_path = Path(output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    figure, axes = plt.subplots(figsize=(8, 8), dpi=dpi)
    axes.set_facecolor("#ddb65b")
    axes.set_xlim(-0.6, BOARD_SIZE - 0.4)
    axes.set_ylim(BOARD_SIZE - 0.4, -0.6)
    axes.set_aspect("equal", adjustable="box")
    axes.set_xticks(range(BOARD_SIZE))
    axes.set_yticks(range(BOARD_SIZE))
    axes.grid(True, color="#1b1b1b", linewidth=0.65, alpha=0.85)
    axes.set_axisbelow(True)
    axes.set_title(title, fontsize=15, pad=12)

    for player, face, label_color in (
        ("black", "#000000", "#ffffff"),
        ("white", "#ffffff", "#202020"),
    ):
        player_moves = [move for move in moves if move.player == player]
        if not player_moves:
            continue
        axes.scatter(
            [move.x for move in player_moves],
            [move.y for move in player_moves],
            s=1050,
            c=face,
            edgecolors="#555555" if player == "white" else "#000000",
            linewidths=1.2,
            zorder=3,
        )
        if annotate_moves:
            for move in player_moves:
                if move.number is not None:
                    axes.text(
                        move.x,
                        move.y,
                        str(move.number),
                        ha="center",
                        va="center",
                        color=label_color,
                        fontsize=8.5,
                        zorder=4,
                    )

    figure.tight_layout()
    figure.savefig(output_path, bbox_inches="tight")
    plt.close(figure)
    return output_path


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--moves-file", help="JSON or text move trace")
    source.add_argument("--moves", help="JSON move list, e.g. '[\"(7,7)\", \"(8,8)\"]'")
    source.add_argument("--record", help="packed training shard (.bin)")
    parser.add_argument("--index", type=int, default=0, help="record index for --record")
    parser.add_argument("--output", default="game.png", help="output PNG/SVG path")
    parser.add_argument("--title", default="Gomoku game")
    parser.add_argument("--first-player", choices=("black", "white"), default="black")
    parser.add_argument("--label-start", type=int, default=0)
    parser.add_argument("--no-move-numbers", action="store_true")
    parser.add_argument("--dpi", type=int, default=160)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.record:
        moves = decode_training_record(args.record, args.index)
        annotate = False
    elif args.moves_file:
        moves = normalize_moves(
            load_move_trace(args.moves_file),
            first_player=args.first_player,
            label_start=args.label_start,
        )
        annotate = not args.no_move_numbers
    else:
        try:
            payload = json.loads(args.moves)
        except json.JSONDecodeError as error:
            raise SystemExit(f"--moves must be valid JSON: {error}") from error
        if not isinstance(payload, list):
            raise SystemExit("--moves must contain a JSON list")
        moves = normalize_moves(
            payload,
            first_player=args.first_player,
            label_start=args.label_start,
        )
        annotate = not args.no_move_numbers

    output = render_board(
        moves,
        args.output,
        title=args.title,
        annotate_moves=annotate,
        dpi=args.dpi,
    )
    print(f"Rendered {len(moves)} stones to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
