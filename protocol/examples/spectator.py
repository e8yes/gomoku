"""Command-line spectator for Gomoku Match.

Connects as an observer, subscribes to all current and future games
(or one specific game via ``--game-id``), and renders each board to
the terminal as moves arrive. Use it to watch live engines play, as
a smoke test that events are flowing, or as a tail when debugging.

Usage::

    python examples/spectator.py                              # all games
    python examples/spectator.py --game-id g12ab34cd          # one game
    python examples/spectator.py --auth-token "$TOKEN" --host server.example.com

Stays connected until Ctrl-C. Boards are rendered with ``X`` for
black, ``O`` for white, ``.`` for empty.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Any, Mapping

from gomoku_match import ObserverClient, connect_tcp


_GLYPH = {0: ".", 1: "X", 2: "O"}


def render_board(state: Mapping[str, Any]) -> str:
    n = int(state["board_size"])
    cells = state["cells"]
    header = "   " + " ".join(f"{x:2d}" for x in range(n))
    rows = [header]
    for y in range(n):
        cells_str = " ".join(f"{_GLYPH[cells[x + y * n]]:>2}" for x in range(n))
        rows.append(f"{y:2d} {cells_str}")
    return "\n".join(rows)


def status_line(
    state: Mapping[str, Any],
    players: Mapping[str, str],
    last_label: str | None = None,
) -> str:
    seat = state["current_player"]
    name = players.get(seat, "?")
    parts = [f"phase={state['phase']}", f"to_move={seat} ({name})"]
    if state["stone_to_place"] != "EMPTY":
        parts.append(f"stone_to_place={state['stone_to_place']}")
    color_for_seat = state["player_stones"].get(seat, "EMPTY")
    if color_for_seat != "EMPTY":
        parts.append(f"seat_color={color_for_seat}")
    if last_label is not None:
        parts.append(f"last={last_label}")
    if state["result"] != "UNDETERMINED":
        parts.append(f"result={state['result']}")
    return "  |  ".join(parts)


class Renderer:
    """Stateful event renderer.

    Tracks each game's player names so ``state_changed`` events (which
    don't repeat the seat→name mapping) can label seats correctly.
    """

    def __init__(self) -> None:
        self._players: dict[str, dict[str, str]] = {}

    def __call__(self, event) -> None:
        method = event.method
        params = event.params
        if method == "game_started":
            gid = params["game_id"]
            self._players[gid] = dict(params["players"])
            print(
                f"\n=== game_started: {gid}  "
                f"A={params['players']['A']}  B={params['players']['B']} ==="
            )
            print(render_board(params["state"]))
            print(status_line(params["state"], self._players[gid]))
        elif method == "state_changed":
            gid = params["game_id"]
            players = self._players.get(gid, {"A": "?", "B": "?"})
            print(f"\n--- {gid}  move {params['by_player']} -> {params['label']} ---")
            print(render_board(params["new_state"]))
            print(status_line(params["new_state"], players, last_label=params["label"]))
        elif method == "game_finished":
            gid = params["game_id"]
            players = self._players.pop(gid, {"A": "?", "B": "?"})  # noqa: F841
            winner = params.get("winner") or "—"
            print(
                f"\n=== game_finished: {gid}  result={params['result']}  "
                f"reason={params['reason']}  winner={winner} ==="
            )

    def render_snapshot(
        self, game_id: str, state: Mapping[str, Any], players: Mapping[str, str]
    ) -> None:
        self._players[game_id] = dict(players)
        print(f"\n=== late-join snapshot: {game_id}  A={players.get('A','?')}  B={players.get('B','?')} ===")
        print(render_board(state))
        print(status_line(state, players))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="spectator")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7901)
    parser.add_argument("--auth-token", default=None, help="shared secret if the server requires one")
    parser.add_argument(
        "--game-id",
        default=None,
        help="subscribe to one specific game and bootstrap from query_state; "
        "omit to subscribe to all current and future games",
    )
    args = parser.parse_args(argv)

    renderer = Renderer()
    obs = ObserverClient(
        connect_tcp(args.host, args.port),
        on_event=renderer,
    )
    try:
        obs.handshake(auth_token=args.auth_token)
        obs.subscribe(game_id=args.game_id)
        if args.game_id is not None:
            # Bootstrap so a late-joiner sees the current state instead
            # of waiting for the next move.
            snap = obs.query_state(args.game_id)
            renderer.render_snapshot(args.game_id, snap["state"], snap["players"])
        print(
            "spectator connected; rendering events. Ctrl-C to exit.",
            file=sys.stderr,
        )
        try:
            while True:
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
    finally:
        obs.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
