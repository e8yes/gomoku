"""Random-policy match player.

Connects to a Gomoku Match server, registers under a chosen name, and
plays every received ``your_turn`` by picking a uniformly-random
legal action. Useful as a wire-level smoke test and as the canonical
zero-skill Elo baseline.

Usage::

    python -m gomoku_match --listen tcp://127.0.0.1:7901 &     # server
    python examples/random_player.py --name alice              # client A
    python examples/random_player.py --name bob                # client B
    # (admin) create_match alice vs bob; both play random moves.

The script stays connected until Ctrl-C; it plays as many matches as
the server pairs it into.
"""

from __future__ import annotations

import argparse
import random
import sys
import threading
import time

from gomoku_match import PlayerClient, connect_tcp


def build_on_turn(rng: random.Random):
    def on_turn(state, deadline_ms):  # noqa: ARG001 — deadline unused for random
        legal = state["legal_actions"]
        if not legal:
            raise RuntimeError("server reported no legal actions")
        return rng.choice(legal)
    return on_turn


def log_game_finished(params: dict) -> None:
    winner = params.get("winner") or "—"
    print(
        f"game_finished {params['game_id']}: result={params['result']} "
        f"reason={params['reason']} winner={winner}",
        file=sys.stderr,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="random_player")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7901)
    parser.add_argument("--name", required=True, help="player name to register")
    parser.add_argument("--auth-token", default=None, help="shared secret if the server requires one")
    parser.add_argument("--seed", type=int, default=None, help="RNG seed (omit for non-deterministic)")
    args = parser.parse_args(argv)

    rng = random.Random(args.seed)
    client = PlayerClient(
        connect_tcp(args.host, args.port),
        name=args.name,
        on_turn=build_on_turn(rng),
        on_game_finished=log_game_finished,
        auth_token=args.auth_token,
    )
    pid = client.register()
    print(f"registered as {args.name} ({pid}); awaiting matches", file=sys.stderr)

    # Idle until Ctrl-C. PlayerClient's reader/dispatcher threads drive
    # the rest. Polling instead of Event.wait so KeyboardInterrupt fires
    # promptly on Windows.
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
