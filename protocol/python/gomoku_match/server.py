"""Match server: referee, clock, and observer broadcaster.

A :class:`MatchServer` instance owns the canonical game state. Player
engines and GUI observers attach as clients (one transport per
connection). The server reads requests, validates them, applies them
to the appropriate :class:`Board`, and broadcasts ``state_changed`` /
``game_finished`` events to every subscribed observer. Players whose
deadline expires forfeit; players who submit an illegal action receive
a structured error and (depending on policy) forfeit on a strike.

Workstream A ships an in-process server: tests attach a queue-pair
transport for each participant. Workstream B will add network
transports without changing the server core.
"""

from __future__ import annotations

import logging
import queue
import threading
import time
import uuid
from dataclasses import dataclass, field
from typing import Any, Mapping, Optional

from .board import (
    Board,
    BoardConfig,
    CONTROL_ACTION_COUNT,
    GamePhase,
    GameResult,
    IllegalActionError,
    Player,
    decode_action_label,
    encode_action_label,
)
from .elo import EloEngine, MatchRatingRow
from .persistence import MatchStore
from .protocol import (
    ErrorCode,
    Event,
    ProtocolError,
    Request,
    Response,
    decode_message,
    encode_message,
)
from .transports import Transport, TransportClosed

PROTOCOL_VERSION = "2.0"
ENGINE_NAME = "gomoku_match"

# Board sizes the server is willing to host. Single-element today; if
# this ever extends to multiple sizes the per-game state already carries
# the right ``action_count`` (see ``Board.action_count``) — only the
# handshake's ``action_count`` becomes ambiguous, in which case we
# advertise the largest supported value as a hint to clients that
# pre-allocate one big policy vector.
SUPPORTED_BOARD_SIZES: tuple[int, ...] = (15,)

logger = logging.getLogger(__name__)


def _safe_token_eq(a: str, b: str) -> bool:
    """Constant-time string compare for auth tokens."""
    import hmac

    return hmac.compare_digest(a.encode("utf-8"), b.encode("utf-8"))


class ServerError(RuntimeError):
    """Mappable error raised by request handlers; carries an :class:`ErrorCode`."""

    def __init__(
        self,
        code: ErrorCode,
        message: str,
        data: Mapping[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.data = dict(data or {})


@dataclass(frozen=True)
class MatchSettings:
    board_size: int = 15
    deadline_ms_per_move: int = 15000
    disconnect_grace_ms: int = 0


@dataclass
class _Connection:
    conn_id: str
    transport: Transport
    handshook: bool = False
    authed: bool = False
    player_id: str | None = None  # set after register()
    subscribed_games: set[str] = field(default_factory=set)  # set of game ids; '*' = all
    is_admin: bool = False
    # Outbound payloads waiting to be flushed by ``_writer_loop``. Producers
    # (broadcast / send_your_turn / send_response) ``put_nowait`` here while
    # they may still be holding ``MatchServer._lock``; the writer thread
    # then performs the actual blocking ``transport.send`` outside the
    # lock. ``None`` is the shutdown sentinel.
    outbound: "queue.Queue[bytes | None]" = field(default_factory=queue.Queue)
    writer_thread: threading.Thread | None = None
    closed: bool = False


@dataclass
class _Game:
    game_id: str
    board: Board
    player_a_id: str
    player_b_id: str
    player_a_name: str
    player_b_name: str
    settings: MatchSettings
    deadline_at: float | None = None  # monotonic deadline for the current player
    finished: bool = False
    result_reason: str = ""
    # If a participant disconnects mid-game we schedule a forfeit at
    # this monotonic time; reconnect-as-same-name cancels it.
    disconnect_forfeit_at: float | None = None
    disconnect_loser: Player | None = None


class MatchServer:
    """In-process match referee."""

    def __init__(
        self,
        *,
        clock_resolution_s: float = 0.05,
        store: MatchStore | None = None,
        elo: EloEngine | None = None,
        auth_token: str | None = None,
        admin_token: str | None = None,
    ) -> None:
        self._lock = threading.RLock()
        self._connections: dict[str, _Connection] = {}
        self._reader_threads: dict[str, threading.Thread] = {}
        self._players_by_id: dict[str, _Connection] = {}
        self._players_by_name: dict[str, str] = {}  # name -> player_id
        self._games: dict[str, _Game] = {}
        self._store = store
        self._elo = elo
        self._auth_token = auth_token or None
        # Optional second token: callers presenting this token at handshake
        # are flagged as admin and can call privileged methods
        # (``create_match``, ``query_history``). When unset, those methods
        # are disabled over the network and only reachable through the
        # programmatic ``MatchServer.create_match`` API.
        self._admin_token = admin_token or None

        self._stop = threading.Event()
        self._clock_resolution_s = clock_resolution_s
        self._clock_thread = threading.Thread(
            target=self._clock_loop, name="match-clock", daemon=True
        )
        self._clock_thread.start()

    @property
    def store(self) -> MatchStore | None:
        return self._store

    @property
    def elo(self) -> EloEngine | None:
        return self._elo

    # ----- Lifecycle --------------------------------------------------

    def attach(self, transport: Transport) -> str:
        conn_id = uuid.uuid4().hex
        conn = _Connection(conn_id=conn_id, transport=transport)
        with self._lock:
            self._connections[conn_id] = conn
        # Writer first: subsequent reader-loop responses depend on it
        # already being able to drain the outbound queue.
        writer = threading.Thread(
            target=self._writer_loop, args=(conn,),
            name=f"match-writer-{conn_id[:8]}", daemon=True,
        )
        writer.start()
        conn.writer_thread = writer
        thread = threading.Thread(
            target=self._reader_loop, args=(conn_id,),
            name=f"match-conn-{conn_id[:8]}", daemon=True,
        )
        thread.start()
        with self._lock:
            self._reader_threads[conn_id] = thread
        return conn_id

    def _writer_loop(self, conn: _Connection) -> None:
        """Drain ``conn.outbound`` and perform the blocking ``transport.send``.

        Decoupling the send from the request-handling path means a slow or
        backpressured peer cannot stall every other client through
        ``MatchServer._lock``. A queue.put under the lock is bounded by
        memcpy speed; a TCP send-window-full peer would otherwise hold
        the lock for the duration of its disconnect-grace window.
        """

        while not self._stop.is_set():
            try:
                payload = conn.outbound.get(timeout=0.1)
            except queue.Empty:
                if conn.closed:
                    return
                continue
            try:
                if payload is None:  # shutdown sentinel
                    return
                try:
                    conn.transport.send(payload)
                except TransportClosed:
                    conn.closed = True
                    return
                except Exception:  # pragma: no cover — never kill the writer.
                    logger.exception(
                        "writer send failed for %s", conn.conn_id
                    )
            finally:
                # Pair every successful ``get`` with exactly one
                # ``task_done`` so the dispatcher can ``outbound.join``
                # before closing the transport on AUTH_FAILED — the
                # client must observe the error response, not the close.
                conn.outbound.task_done()

    def _enqueue_send(self, conn: _Connection, payload: bytes) -> None:
        """Hand a payload to the writer thread.

        Safe to call while holding ``self._lock``: the queue put is O(1)
        and never blocks on the transport.
        """

        if conn.closed:
            return
        try:
            conn.outbound.put_nowait(payload)
        except queue.Full:  # pragma: no cover — Queue is unbounded today.
            logger.warning("outbound queue full for %s; dropping", conn.conn_id)

    def _flush_outbound(self, conn: _Connection, *, timeout: float) -> None:
        """Block until ``conn``'s outbound queue is drained or ``timeout`` lapses.

        Used before a deliberate ``transport.close`` so the client
        observes the final response rather than an unexplained EOF.
        Polls ``unfinished_tasks`` directly so we get a bounded wait
        without ``Queue.join``'s no-timeout limitation.
        """

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if conn.outbound.unfinished_tasks == 0:
                return
            time.sleep(0.005)

    def shutdown(self, *, wait: bool = True) -> None:
        self._stop.set()
        with self._lock:
            conns = list(self._connections.values())
            threads = list(self._reader_threads.values())
        for conn in conns:
            # Wake the writer thread so it observes ``_stop`` / closed
            # state instead of waiting out the get-timeout.
            try:
                conn.outbound.put_nowait(None)
            except queue.Full:  # pragma: no cover
                pass
            try:
                conn.transport.close()
            except Exception:  # pragma: no cover — best effort.
                pass
        if wait:
            self._clock_thread.join(timeout=2.0)
            for t in threads:
                t.join(timeout=2.0)
            for conn in conns:
                if conn.writer_thread is not None:
                    conn.writer_thread.join(timeout=2.0)

    # ----- Programmatic admin API (tests, embedded use) --------------

    def create_match(
        self,
        player_a_name: str,
        player_b_name: str,
        settings: MatchSettings | None = None,
    ) -> str:
        cfg = settings or MatchSettings()
        with self._lock:
            if player_a_name not in self._players_by_name:
                raise ServerError(
                    ErrorCode.UNKNOWN_PLAYER, f"player '{player_a_name}' not registered"
                )
            if player_b_name not in self._players_by_name:
                raise ServerError(
                    ErrorCode.UNKNOWN_PLAYER, f"player '{player_b_name}' not registered"
                )
            game_id = "g" + uuid.uuid4().hex[:8]
            game = _Game(
                game_id=game_id,
                board=Board(BoardConfig(size=cfg.board_size)),
                player_a_id=self._players_by_name[player_a_name],
                player_b_id=self._players_by_name[player_b_name],
                player_a_name=player_a_name,
                player_b_name=player_b_name,
                settings=cfg,
            )
            self._games[game_id] = game
        if self._store is not None:
            self._store.record_match_started(
                game_id=game_id,
                player_a_name=player_a_name,
                player_b_name=player_b_name,
                board_size=cfg.board_size,
                deadline_ms_per_move=cfg.deadline_ms_per_move,
            )
        self._broadcast_game_started(game)
        self._send_your_turn(game)
        return game_id

    # ----- Reader loop ------------------------------------------------

    def _reader_loop(self, conn_id: str) -> None:
        conn = self._connections[conn_id]
        try:
            while not self._stop.is_set():
                try:
                    line = conn.transport.recv(timeout=self._clock_resolution_s)
                except TimeoutError:
                    continue
                except TransportClosed:
                    return
                try:
                    msg = decode_message(line)
                except ProtocolError as exc:
                    self._send_response(
                        conn,
                        Response.err(
                            None, ErrorCode.PROTOCOL_ERROR, str(exc),
                            data={"raw": line[:200]},
                        ),
                    )
                    continue
                if isinstance(msg, Request):
                    self._dispatch_request(conn, msg)
                # Responses and unsolicited events from clients are ignored.
        finally:
            self._cleanup_connection(conn_id)

    def _cleanup_connection(self, conn_id: str) -> None:
        with self._lock:
            conn = self._connections.pop(conn_id, None)
            self._reader_threads.pop(conn_id, None)
            if conn is None:
                return
        # Signal the writer thread to drain and exit. Done outside the
        # lock so the writer can finish in-flight sends without
        # contending with handlers that still hold the lock.
        conn.closed = True
        try:
            conn.outbound.put_nowait(None)
        except queue.Full:  # pragma: no cover
            pass
        with self._lock:
            if conn.player_id is not None:
                # Drop the active connection for this player, but KEEP
                # the name → player_id mapping so a reconnect with the
                # same name can resume the game.
                self._players_by_id.pop(conn.player_id, None)
                # Schedule (or immediately apply) forfeits for any
                # active games the player was participating in.
                for game in list(self._games.values()):
                    if game.finished:
                        continue
                    if conn.player_id not in (game.player_a_id, game.player_b_id):
                        continue
                    loser = (
                        Player.A if conn.player_id == game.player_a_id else Player.B
                    )
                    grace_ms = game.settings.disconnect_grace_ms
                    if grace_ms > 0:
                        game.disconnect_forfeit_at = (
                            time.monotonic() + grace_ms / 1000.0
                        )
                        game.disconnect_loser = loser
                    else:
                        self._finish_game(
                            game,
                            self._winner_for_loser(loser),
                            reason="disconnect",
                        )

    # ----- Request dispatch -------------------------------------------

    def _dispatch_request(self, conn: _Connection, request: Request) -> None:
        handler_name = f"_handle_{request.method}"
        handler = getattr(self, handler_name, None)
        if handler is None:
            self._send_response(
                conn,
                Response.err(
                    request.id,
                    ErrorCode.UNKNOWN_METHOD,
                    f"unknown method '{request.method}'",
                ),
            )
            return
        # All methods except ``handshake`` require a completed handshake.
        # ``conn.handshook`` is only set after the auth check in
        # ``_handle_handshake`` passes, so this single gate is sufficient
        # to enforce both "must handshake first" and "must be authed when
        # auth is required" — bypassing it lets a peer reach
        # ``query_state``/``submit_move`` over a fresh socket.
        if request.method != "handshake" and not conn.handshook:
            self._send_response(
                conn,
                Response.err(
                    request.id,
                    ErrorCode.BAD_REQUEST,
                    "must call handshake before any other method",
                ),
            )
            return
        try:
            with self._lock:
                result = handler(conn, request.params)
        except ServerError as exc:
            self._send_response(
                conn, Response.err(request.id, exc.code, str(exc), data=exc.data)
            )
            if exc.code == ErrorCode.AUTH_FAILED:
                # Wait for the AUTH_FAILED response to actually go on the
                # wire before closing the transport. Without this the
                # writer-thread queue plus an immediate ``close()`` race:
                # the client typically sees the EOF first and reports a
                # generic "transport closed" instead of the structured
                # auth_failed error. ``join`` blocks on
                # ``unfinished_tasks``; bound it with a poll so a stuck
                # writer cannot wedge the dispatcher.
                self._flush_outbound(conn, timeout=1.0)
                try:
                    conn.transport.close()
                except Exception:  # pragma: no cover
                    pass
            return
        except IllegalActionError as exc:
            self._send_response(
                conn,
                Response.err(request.id, ErrorCode.ILLEGAL_ACTION, str(exc)),
            )
            return
        except Exception as exc:  # noqa: BLE001
            logger.exception("unhandled error in %s", request.method)
            self._send_response(
                conn,
                Response.err(request.id, ErrorCode.ENGINE_ERROR, repr(exc)),
            )
            return
        self._send_response(conn, Response.ok(request.id, result or {}))

    # ----- Handlers ---------------------------------------------------

    def _handle_handshake(self, conn: _Connection, params: dict) -> dict:
        client_proto = params.get("protocol_version", "")
        if client_proto and client_proto.split(".")[0] != PROTOCOL_VERSION.split(".")[0]:
            raise ServerError(
                ErrorCode.PROTOCOL_ERROR,
                f"unsupported protocol version {client_proto!r}",
                data={"server_version": PROTOCOL_VERSION},
            )
        if self._auth_token is not None:
            offered = params.get("auth_token")
            if not isinstance(offered, str) or not _safe_token_eq(
                offered, self._auth_token
            ):
                raise ServerError(
                    ErrorCode.AUTH_FAILED,
                    "auth_token is required and did not match",
                )
            conn.authed = True
        else:
            conn.authed = True
        if self._admin_token is not None:
            offered_admin = params.get("admin_token")
            if isinstance(offered_admin, str) and _safe_token_eq(
                offered_admin, self._admin_token
            ):
                conn.is_admin = True
        conn.handshook = True
        max_board_size = max(SUPPORTED_BOARD_SIZES)
        return {
            "engine_name": ENGINE_NAME,
            "protocol_version": PROTOCOL_VERSION,
            "supported_rules": ["exact_five"],
            "supported_board_sizes": list(SUPPORTED_BOARD_SIZES),
            # Worst-case action count across all advertised sizes. Per-
            # game ``action_count`` is correct for the actual board the
            # game uses; clients that pre-allocate by handshake should
            # size for the maximum.
            "action_count": max_board_size * max_board_size + CONTROL_ACTION_COUNT,
            "capabilities": [
                "in_process_transport",
                "tcp_transport",
                "auth_token" if self._auth_token is not None else "no_auth",
                "disconnect_grace",
            ],
            "auth_required": self._auth_token is not None,
        }

    def _handle_register(self, conn: _Connection, params: dict) -> dict:
        if not conn.handshook:
            raise ServerError(
                ErrorCode.BAD_REQUEST, "must call handshake before register"
            )
        name = params.get("name")
        if not isinstance(name, str) or not name:
            raise ServerError(ErrorCode.BAD_REQUEST, "register requires a 'name' string")
        if conn.player_id is not None:
            raise ServerError(
                ErrorCode.BAD_REQUEST,
                "this connection has already registered as a player",
            )
        existing_id = self._players_by_name.get(name)
        if existing_id is not None and existing_id in self._players_by_id:
            raise ServerError(
                ErrorCode.BAD_REQUEST, f"player '{name}' is already registered"
            )
        resumed_games: list[_Game] = []
        if existing_id is not None:
            # Same-name reconnect: reuse the player_id and clear any
            # pending disconnect-forfeit on games this player was in.
            player_id = existing_id
            for game in self._games.values():
                if game.finished:
                    continue
                if player_id in (game.player_a_id, game.player_b_id):
                    game.disconnect_forfeit_at = None
                    game.disconnect_loser = None
                    resumed_games.append(game)
        else:
            player_id = "p" + uuid.uuid4().hex[:8]
        conn.player_id = player_id
        self._players_by_id[player_id] = conn
        self._players_by_name[name] = player_id
        # If the reconnecting player is currently on the clock in any
        # active game, re-deliver your_turn so they can resume play.
        for game in resumed_games:
            on_clock_id = (
                game.player_a_id
                if game.board.current_player == Player.A
                else game.player_b_id
            )
            if on_clock_id == player_id:
                # Reset deadline from "now" — they've earned a fresh slot.
                game.deadline_at = self._next_deadline(game)
                self._send_your_turn(game)
        return {
            "player_id": player_id,
            "name": name,
            "reconnected": existing_id is not None,
        }

    def _handle_subscribe(self, conn: _Connection, params: dict) -> dict:
        if not conn.handshook:
            raise ServerError(ErrorCode.BAD_REQUEST, "must handshake first")
        game_id = params.get("game_id")
        if game_id is None:
            conn.subscribed_games.add("*")
            return {"subscribed": "all"}
        if game_id not in self._games:
            raise ServerError(
                ErrorCode.UNKNOWN_GAME, f"no such game '{game_id}'"
            )
        conn.subscribed_games.add(game_id)
        return {"subscribed": game_id}

    def _handle_create_match(self, conn: _Connection, params: dict) -> dict:
        # Programmatic admin call has its own entry point; expose via
        # protocol so a network admin can create matches too. Gate on
        # ``is_admin`` so a regular authenticated client cannot pair other
        # players into matches without their consent.
        if not conn.is_admin:
            raise ServerError(
                ErrorCode.AUTH_FAILED,
                "create_match requires admin privileges",
            )
        a = params.get("player_a")
        b = params.get("player_b")
        if not isinstance(a, str) or not isinstance(b, str):
            raise ServerError(
                ErrorCode.BAD_REQUEST, "create_match requires 'player_a' and 'player_b'"
            )
        cfg = MatchSettings(
            board_size=int(params.get("board_size", 15)),
            deadline_ms_per_move=int(params.get("deadline_ms_per_move", 5000)),
            disconnect_grace_ms=int(params.get("disconnect_grace_ms", 0)),
        )
        # Drop the lock briefly while spawning the match (create_match
        # acquires it again). Calling _from_ the locked dispatcher is
        # OK because the lock is reentrant.
        game_id = self.create_match(a, b, cfg)
        return {"game_id": game_id}

    def _handle_submit_move(self, conn: _Connection, params: dict) -> dict:
        if conn.player_id is None:
            raise ServerError(
                ErrorCode.UNKNOWN_PLAYER, "must register before submitting moves"
            )
        game_id = params.get("game_id")
        if game_id not in self._games:
            raise ServerError(ErrorCode.UNKNOWN_GAME, f"no such game '{game_id}'")
        game = self._games[game_id]
        if game.finished:
            raise ServerError(
                ErrorCode.TERMINAL_POSITION, f"game '{game_id}' is finished"
            )
        # Resolve action id (accept either int or label).
        raw_action = params.get("action")
        try:
            action_id = self._resolve_action(raw_action, game.board.config.size)
        except ValueError as exc:
            raise ServerError(ErrorCode.BAD_REQUEST, str(exc))
        # Verify it's this player's turn.
        expected_id = (
            game.player_a_id if game.board.current_player == Player.A
            else game.player_b_id
        )
        if conn.player_id != expected_id:
            raise ServerError(
                ErrorCode.NOT_YOUR_TURN,
                "submit_move arrived from a player who is not on the clock",
                data={"on_clock_player_id": expected_id},
            )

        # Snapshot just enough state to roll the move back on a journal
        # failure. Without this, ``apply`` mutates the in-memory board
        # before ``record_move`` runs, so a disk-full / IntegrityError /
        # schema-drift exception leaves the in-memory game one move
        # ahead of the on-disk journal — the opponent never sees
        # ``state_changed`` and the next ``your_turn`` reflects a move
        # that the reviewing client thinks "didn't happen".
        snap = self._snapshot_board(game.board)
        # Apply (raises IllegalActionError → response error).
        game.board.apply(action_id)
        label = encode_action_label(action_id, game.board.config.size)
        by_player_name = self._player_name(conn.player_id)
        ply = len(game.board.move_history)
        if self._store is not None:
            try:
                self._store.record_move(
                    game_id=game.game_id,
                    ply=ply,
                    action_id=action_id,
                    label=label,
                    by_player_name=by_player_name,
                )
            except Exception:
                # Roll the in-memory board back so apply+journal stay in
                # lockstep. The dispatcher's outer ``except Exception``
                # then maps the original exception to ENGINE_ERROR.
                self._restore_board(game.board, snap)
                raise
        # Game finished?
        if game.board.result != GameResult.UNDETERMINED:
            self._broadcast_state_changed(game, action_id, label, by_player_name)
            self._finish_game_from_board(game, action_id, label)
            return {"ok": True, "terminal": True}

        # Reset deadline for the next mover.
        game.deadline_at = self._next_deadline(game)
        self._broadcast_state_changed(game, action_id, label, by_player_name)
        self._send_your_turn(game)
        return {"ok": True, "terminal": False}

    def _handle_resign(self, conn: _Connection, params: dict) -> dict:
        if conn.player_id is None:
            raise ServerError(ErrorCode.UNKNOWN_PLAYER, "must register first")
        game_id = params.get("game_id")
        if game_id not in self._games:
            raise ServerError(ErrorCode.UNKNOWN_GAME, f"no such game '{game_id}'")
        game = self._games[game_id]
        if game.finished:
            raise ServerError(
                ErrorCode.TERMINAL_POSITION, f"game '{game_id}' already finished"
            )
        if conn.player_id == game.player_a_id:
            self._finish_game(game, GameResult.PLAYER_B_WIN, reason="resignation")
        elif conn.player_id == game.player_b_id:
            self._finish_game(game, GameResult.PLAYER_A_WIN, reason="resignation")
        else:
            raise ServerError(
                ErrorCode.NOT_YOUR_TURN, "you are not a participant in that game"
            )
        return {"ok": True}

    def _handle_query_state(self, conn: _Connection, params: dict) -> dict:
        game_id = params.get("game_id")
        if game_id not in self._games:
            raise ServerError(ErrorCode.UNKNOWN_GAME, f"no such game '{game_id}'")
        game = self._games[game_id]
        return {
            "game_id": game_id,
            "state": game.board.to_state_dict(),
            "finished": game.finished,
            "result": game.board.result.name,
            "result_reason": game.result_reason,
            "players": {
                "A": self._player_name(game.player_a_id),
                "B": self._player_name(game.player_b_id),
            },
        }

    def _handle_query_history(self, conn: _Connection, params: dict) -> dict:
        # Returns history for every game on the server, so gate on
        # ``is_admin``. Players that want their own history can already
        # query individual games by id via ``query_state``.
        if not conn.is_admin:
            raise ServerError(
                ErrorCode.AUTH_FAILED,
                "query_history requires admin privileges",
            )
        return {
            "games": [
                {
                    "game_id": gid,
                    "finished": g.finished,
                    "result": g.board.result.name,
                    "result_reason": g.result_reason,
                    "moves": list(g.board.move_history),
                }
                for gid, g in self._games.items()
            ]
        }

    # ----- Broadcasting ------------------------------------------------

    def _broadcast(self, event: Event, game: _Game) -> None:
        payload = encode_message(event)
        targets: list[_Connection] = []
        # Players in the game always see the events.
        for pid in (game.player_a_id, game.player_b_id):
            conn = self._players_by_id.get(pid)
            if conn is not None:
                targets.append(conn)
        # Observers subscribed to all games or this specific game.
        for conn in self._connections.values():
            if conn.player_id is not None and conn in targets:
                continue
            if "*" in conn.subscribed_games or game.game_id in conn.subscribed_games:
                targets.append(conn)
        # De-duplicate while preserving order.
        seen: set[str] = set()
        for conn in targets:
            if conn.conn_id in seen:
                continue
            seen.add(conn.conn_id)
            self._enqueue_send(conn, payload)

    def _broadcast_game_started(self, game: _Game) -> None:
        self._broadcast(
            Event(
                method="game_started",
                params={
                    "game_id": game.game_id,
                    "players": {
                        "A": self._player_name(game.player_a_id),
                        "B": self._player_name(game.player_b_id),
                    },
                    "settings": {
                        "board_size": game.settings.board_size,
                        "deadline_ms_per_move": game.settings.deadline_ms_per_move,
                    },
                    "state": game.board.to_state_dict(),
                },
            ),
            game,
        )

    def _broadcast_state_changed(
        self, game: _Game, action_id: int, label: str, by_player_name: str
    ) -> None:
        self._broadcast(
            Event(
                method="state_changed",
                params={
                    "game_id": game.game_id,
                    "action": action_id,
                    "label": label,
                    "by_player": by_player_name,
                    "new_state": game.board.to_state_dict(),
                },
            ),
            game,
        )

    def _broadcast_game_finished(
        self, game: _Game, rating_update: MatchRatingRow | None = None
    ) -> None:
        winner_id = self._winner_id(game)
        params: dict = {
            "game_id": game.game_id,
            "result": game.board.result.name,
            "reason": game.result_reason,
            "winner": self._player_name(winner_id) if winner_id else None,
            "final_state": game.board.to_state_dict(),
        }
        if rating_update is not None:
            params["ratings"] = {
                "player_a": {
                    "name": rating_update.player_a_name,
                    "pre": rating_update.pre_rating_a,
                    "post": rating_update.post_rating_a,
                    "k": rating_update.k_factor_a,
                },
                "player_b": {
                    "name": rating_update.player_b_name,
                    "pre": rating_update.pre_rating_b,
                    "post": rating_update.post_rating_b,
                    "k": rating_update.k_factor_b,
                },
            }
        self._broadcast(Event(method="game_finished", params=params), game)

    def _send_your_turn(self, game: _Game) -> None:
        if game.finished:
            return
        on_clock = (
            game.player_a_id if game.board.current_player == Player.A
            else game.player_b_id
        )
        conn = self._players_by_id.get(on_clock)
        if conn is None:
            # Player disconnected before we got here; force a forfeit.
            winner = (
                GameResult.PLAYER_B_WIN
                if on_clock == game.player_a_id
                else GameResult.PLAYER_A_WIN
            )
            self._finish_game(game, winner, reason="disconnect")
            return
        if game.deadline_at is None:
            game.deadline_at = self._next_deadline(game)
        deadline_ms = max(
            0, int(round((game.deadline_at - time.monotonic()) * 1000))
        )
        event = Event(
            method="your_turn",
            params={
                "game_id": game.game_id,
                "state": game.board.to_state_dict(),
                "deadline_ms": deadline_ms,
            },
        )
        # ``_send_your_turn`` runs from inside the dispatcher's lock, so
        # use the writer-thread queue rather than calling ``transport.send``
        # directly. Detection of a closed transport now happens in the
        # writer loop, which marks ``conn.closed=True`` — the next clock
        # tick or follow-up handler will observe the absence of the
        # connection in ``_players_by_id`` and forfeit the match.
        self._enqueue_send(conn, encode_message(event))

    def _send_response(self, conn: _Connection, response: Response) -> None:
        self._enqueue_send(conn, encode_message(response))

    # ----- Game finalisation ------------------------------------------

    def _finish_game_from_board(
        self, game: _Game, action_id: int, label: str
    ) -> None:
        result = game.board.result
        reason = "five_in_a_row" if result != GameResult.DRAW else "draw"
        self._finish_game(game, result, reason=reason)

    def _finish_game(self, game: _Game, result: GameResult, *, reason: str) -> None:
        if game.finished:
            return
        # Reflect the result on the canonical board if it isn't already set
        # (e.g. timeout, resignation, disconnect).
        if game.board.result == GameResult.UNDETERMINED:
            game.board.result = result
        game.finished = True
        game.result_reason = reason
        game.deadline_at = None
        if self._store is not None:
            self._store.record_match_finished(
                game_id=game.game_id,
                result=game.board.result.name,
                reason=reason,
            )
        rating_update: MatchRatingRow | None = None
        if self._elo is not None:
            try:
                rating_update = self._elo.update_after_match(
                    game_id=game.game_id,
                    player_a_name=game.player_a_name,
                    player_b_name=game.player_b_name,
                    result=game.board.result.name,
                )
            except ValueError:
                # Already rated (replay). Surface the existing audit row.
                rating_update = self._elo.get_match_rating(game.game_id)
            except Exception:  # pragma: no cover — never fail a match.
                logger.exception("elo update failed for %s", game.game_id)
        self._broadcast_game_finished(game, rating_update)

    def _winner_id(self, game: _Game) -> str | None:
        if game.board.result == GameResult.PLAYER_A_WIN:
            return game.player_a_id
        if game.board.result == GameResult.PLAYER_B_WIN:
            return game.player_b_id
        return None

    @staticmethod
    def _winner_for_loser(loser: Player) -> GameResult:
        return GameResult.PLAYER_B_WIN if loser == Player.A else GameResult.PLAYER_A_WIN

    # ----- Clock loop -------------------------------------------------

    def _clock_loop(self) -> None:
        while not self._stop.is_set():
            time.sleep(self._clock_resolution_s)
            now = time.monotonic()
            with self._lock:
                expired_move: list[_Game] = []
                expired_disc: list[_Game] = []
                for game in self._games.values():
                    if game.finished:
                        continue
                    if (
                        game.deadline_at is not None
                        and now >= game.deadline_at
                    ):
                        expired_move.append(game)
                    if (
                        game.disconnect_forfeit_at is not None
                        and now >= game.disconnect_forfeit_at
                    ):
                        expired_disc.append(game)
                for game in expired_move:
                    if game.board.current_player == Player.A:
                        self._finish_game(
                            game, GameResult.PLAYER_B_WIN, reason="timeout"
                        )
                    else:
                        self._finish_game(
                            game, GameResult.PLAYER_A_WIN, reason="timeout"
                        )
                for game in expired_disc:
                    if game.finished:
                        continue
                    loser = game.disconnect_loser or Player.A
                    self._finish_game(
                        game, self._winner_for_loser(loser), reason="disconnect"
                    )

    # ----- Helpers ----------------------------------------------------

    def _next_deadline(self, game: _Game) -> float:
        return time.monotonic() + game.settings.deadline_ms_per_move / 1000.0

    def _player_name(self, player_id: str | None) -> str:
        if player_id is None:
            return ""
        for name, pid in self._players_by_name.items():
            if pid == player_id:
                return name
        return player_id

    @staticmethod
    def _snapshot_board(board: Board) -> tuple:
        """Capture the mutable state of ``board`` for rollback.

        Used by ``_handle_submit_move`` to undo an applied move when the
        SQL journal write fails. Returns an opaque tuple consumed by
        ``_restore_board``.
        """

        return (
            list(board.cells),
            list(board.move_history),
            board.phase,
            board.current_player,
            board.stone_to_place,
            dict(board.player_stones),
            board.move_count,
            board.result,
        )

    @staticmethod
    def _restore_board(board: Board, snap: tuple) -> None:
        """Restore ``board`` from a previously taken ``_snapshot_board``."""

        (
            cells,
            move_history,
            phase,
            current_player,
            stone_to_place,
            player_stones,
            move_count,
            result,
        ) = snap
        # Copy back into fresh containers so a future snapshot/restore on
        # the same board cannot accidentally alias.
        board.cells = list(cells)
        board.move_history = list(move_history)
        board.phase = phase
        board.current_player = current_player
        board.stone_to_place = stone_to_place
        board.player_stones = dict(player_stones)
        board.move_count = move_count
        board.result = result

    @staticmethod
    def _resolve_action(raw_action: Any, board_size: int) -> int:
        if isinstance(raw_action, bool):
            raise ValueError("action must be int or string label, not bool")
        if isinstance(raw_action, int):
            return raw_action
        if isinstance(raw_action, str):
            return decode_action_label(raw_action, board_size)
        raise ValueError(f"unsupported action type {type(raw_action).__name__}")
