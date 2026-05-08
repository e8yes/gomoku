"""``python -m gomoku_match`` — run a network-accessible match server.

Usage:

    python -m gomoku_match --listen tcp://0.0.0.0:7901 \\
        --auth-token-env GOMOKU_MATCH_TOKEN \\
        --store ./matches.sqlite

The process accepts TCP connections, hands each socket to a
``MatchServer``, and runs until SIGINT. ``--auth-token`` (literal) or
``--auth-token-env`` (read from environment) makes auth required for
every handshake; if both are omitted the server runs in open mode and
should only be exposed on a trusted loopback interface.
"""

from __future__ import annotations

import argparse
import logging
import os
import signal
import sys
import threading
from pathlib import Path

from .elo import DEFAULT_INITIAL_RATING, EloEngine
from .persistence import MatchStore
from .server import MatchServer
from .tcp_transport import TcpListener, parse_listen_url

logger = logging.getLogger("gomoku_match")


def _build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="python -m gomoku_match")
    p.add_argument(
        "--listen",
        default="tcp://127.0.0.1:7901",
        help="listen URL, e.g. tcp://0.0.0.0:7901 (default: %(default)s)",
    )
    auth = p.add_mutually_exclusive_group()
    auth.add_argument(
        "--auth-token",
        default=None,
        help="literal shared-secret token clients must send during handshake",
    )
    auth.add_argument(
        "--auth-token-env",
        default=None,
        help="environment variable to read the auth token from (preferred)",
    )
    admin = p.add_mutually_exclusive_group()
    admin.add_argument(
        "--admin-token",
        default=None,
        help=(
            "literal admin token; clients presenting it during handshake "
            "may call privileged methods (create_match, query_history)"
        ),
    )
    admin.add_argument(
        "--admin-token-env",
        default=None,
        help="environment variable to read the admin token from (preferred)",
    )
    p.add_argument(
        "--store",
        default=None,
        help="path to SQLite match journal (omit to disable persistence)",
    )
    p.add_argument(
        "--elo",
        action="store_true",
        help="maintain a centralised Elo leaderboard in the same SQLite "
             "store (requires --store)",
    )
    p.add_argument(
        "--elo-initial-rating",
        type=float,
        default=DEFAULT_INITIAL_RATING,
        help="initial rating for new players (default: %(default)s)",
    )
    p.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="logging level (default: %(default)s)",
    )
    p.add_argument(
        "--strict-stderr",
        action="store_true",
        help="exit nonzero on the first ENGINE_ERROR-level event",
    )
    return p


def _resolve_auth_token(args: argparse.Namespace) -> str | None:
    if args.auth_token is not None:
        return args.auth_token
    if args.auth_token_env is not None:
        token = os.environ.get(args.auth_token_env)
        if not token:
            raise SystemExit(
                f"--auth-token-env {args.auth_token_env!r} is unset or empty"
            )
        return token
    return None


def _resolve_admin_token(args: argparse.Namespace) -> str | None:
    if args.admin_token is not None:
        return args.admin_token
    if args.admin_token_env is not None:
        token = os.environ.get(args.admin_token_env)
        if not token:
            raise SystemExit(
                f"--admin-token-env {args.admin_token_env!r} is unset or empty"
            )
        return token
    return None


def main(argv: list[str] | None = None) -> int:
    args = _build_arg_parser().parse_args(argv)
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        stream=sys.stderr,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    auth_token = _resolve_auth_token(args)
    admin_token = _resolve_admin_token(args)
    host, port = parse_listen_url(args.listen)

    store = MatchStore(Path(args.store)) if args.store else None
    elo: EloEngine | None = None
    if args.elo:
        if store is None:
            raise SystemExit("--elo requires --store")
        elo = EloEngine(store, initial_rating=args.elo_initial_rating)
    server = MatchServer(
        store=store, elo=elo, auth_token=auth_token, admin_token=admin_token
    )

    def on_connection(transport) -> None:
        server.attach(transport)

    listener = TcpListener(host, port, on_connection)
    listener.start()
    logger.info(
        "match server listening on %s:%d (auth=%s, admin=%s, store=%s, elo=%s)",
        host,
        listener.port,
        "on" if auth_token else "off",
        "on" if admin_token else "off",
        args.store or "in-memory",
        "on" if elo is not None else "off",
    )

    stop = threading.Event()

    def _handle_sigint(signum, frame):  # noqa: ARG001
        logger.info("received signal %d, shutting down", signum)
        stop.set()

    signal.signal(signal.SIGINT, _handle_sigint)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, _handle_sigint)

    try:
        stop.wait()
    finally:
        listener.stop()
        server.shutdown()
        if store is not None:
            store.close()
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
