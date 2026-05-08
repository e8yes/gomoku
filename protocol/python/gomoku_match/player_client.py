"""PlayerClient: thin client library for engines.

Spawns a reader thread that consumes messages from the transport. Each
``your_turn`` event invokes the user's ``on_turn(state, deadline_ms)``
callback; the returned action id is wrapped into a ``submit_move``
request and dispatched. ``register`` is the only synchronous request
the user typically calls — everything else is event-driven.
"""

from __future__ import annotations

import threading
import uuid
from queue import Queue
from typing import Any, Callable, Mapping

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

OnTurnCallback = Callable[[Mapping[str, Any], int], int | str]
OnGameFinishedCallback = Callable[[Mapping[str, Any]], None]
OnGameStartedCallback = Callable[[Mapping[str, Any]], None]


class PlayerClientError(RuntimeError):
    pass


class PlayerClient:
    """Synchronous request/response wrapper around a player connection.

    The client owns a reader thread that pumps the transport. Inbound
    responses match outstanding requests by id; inbound ``your_turn``
    events fire the user's ``on_turn`` callback (default: pick the
    first legal action) and the returned action is sent as
    ``submit_move`` automatically.
    """

    def __init__(
        self,
        transport: Transport,
        *,
        name: str,
        on_turn: OnTurnCallback | None = None,
        on_game_started: OnGameStartedCallback | None = None,
        on_game_finished: OnGameFinishedCallback | None = None,
        auth_token: str | None = None,
    ) -> None:
        self._transport = transport
        self._name = name
        self._on_turn = on_turn or self._default_on_turn
        self._on_game_started = on_game_started
        self._on_game_finished = on_game_finished
        self._auth_token = auth_token

        self._lock = threading.Lock()
        self._pending: dict[str, "Queue[Response]"] = {}
        self._stop = threading.Event()
        self._player_id: str | None = None
        # Inbound events are queued on the reader thread and dispatched
        # on a separate worker thread. This is critical: an event
        # handler may call ``self.call(...)`` to submit a move, which
        # blocks waiting for the response. If the handler ran on the
        # reader thread, that response could never be delivered.
        self._events: "Queue[Event]" = Queue()
        self._reader = threading.Thread(
            target=self._reader_loop, name=f"player-{name}-reader", daemon=True
        )
        self._dispatcher = threading.Thread(
            target=self._dispatch_loop, name=f"player-{name}-dispatch", daemon=True
        )
        self._reader.start()
        self._dispatcher.start()

    # ----- Public surface ---------------------------------------------

    @property
    def name(self) -> str:
        return self._name

    @property
    def player_id(self) -> str | None:
        return self._player_id

    def register(self, *, timeout: float = 5.0) -> str:
        params: dict = {"protocol_version": "2.0", "client_name": self._name}
        if self._auth_token is not None:
            params["auth_token"] = self._auth_token
        self.call("handshake", params, timeout=timeout)
        result = self.call("register", {"name": self._name}, timeout=timeout)
        self._player_id = result["player_id"]
        return self._player_id

    def call(
        self,
        method: str,
        params: Mapping[str, Any] | None = None,
        *,
        timeout: float = 5.0,
    ) -> dict:
        request = Request(method=method, id=uuid.uuid4().hex, params=dict(params or {}))
        q: "Queue[Response]" = Queue(maxsize=1)
        with self._lock:
            self._pending[request.id] = q
        try:
            self._transport.send(encode_message(request))
        except TransportClosed as exc:
            with self._lock:
                self._pending.pop(request.id, None)
            raise PlayerClientError("transport closed") from exc
        try:
            response = q.get(timeout=timeout)
        except Exception as exc:
            with self._lock:
                self._pending.pop(request.id, None)
            raise PlayerClientError(
                f"{method} timed out after {timeout}s"
            ) from exc
        if response.error is not None:
            err = response.error
            data = err.get("data") or {}
            symbol = data.get("code_name") if isinstance(data, dict) else None
            label = symbol or str(err.get("code"))
            raise PlayerClientError(
                f"{method} returned {label}: {err.get('message')}"
            )
        return response.result or {}

    def close(self) -> None:
        self._stop.set()
        # Wake the dispatcher so it can exit promptly.
        try:
            self._events.put_nowait(None)  # type: ignore[arg-type]
        except Exception:  # pragma: no cover — unbounded queue.
            pass
        try:
            self._transport.close()
        except Exception:  # pragma: no cover
            pass

    # ----- Reader loop ------------------------------------------------

    def _reader_loop(self) -> None:
        while not self._stop.is_set():
            try:
                line = self._transport.recv(timeout=0.1)
            except TimeoutError:
                continue
            except TransportClosed:
                self._fail_pending(PlayerClientError("transport closed"))
                self._events.put(None)  # type: ignore[arg-type]
                return
            try:
                msg = decode_message(line)
            except ProtocolError:
                continue
            if isinstance(msg, Response):
                with self._lock:
                    q = self._pending.pop(msg.id, None)
                if q is not None:
                    q.put(msg)
            elif isinstance(msg, Event):
                self._events.put(msg)

    def _dispatch_loop(self) -> None:
        while not self._stop.is_set():
            event = self._events.get()
            if event is None:
                return
            self._handle_event(event)

    def _fail_pending(self, exc: Exception) -> None:
        with self._lock:
            queues = list(self._pending.values())
            self._pending.clear()
        for q in queues:
            q.put(Response.err(None, ErrorCode.ENGINE_ERROR, str(exc)))

    # ----- Event handling ---------------------------------------------

    def _handle_event(self, event: Event) -> None:
        if event.method == "your_turn":
            self._handle_your_turn(event.params)
        elif event.method == "game_started" and self._on_game_started is not None:
            try:
                self._on_game_started(event.params)
            except Exception:  # pragma: no cover — caller bug.
                pass
        elif event.method == "game_finished" and self._on_game_finished is not None:
            try:
                self._on_game_finished(event.params)
            except Exception:  # pragma: no cover — caller bug.
                pass

    def _handle_your_turn(self, params: dict) -> None:
        state = params.get("state", {})
        deadline_ms = int(params.get("deadline_ms", 0))
        game_id = params.get("game_id")
        try:
            action = self._on_turn(state, deadline_ms)
        except Exception as exc:  # noqa: BLE001
            # Resign on callback failure rather than letting the deadline
            # forfeit silently.
            try:
                self.call("resign", {"game_id": game_id}, timeout=1.0)
            except Exception:  # pragma: no cover
                pass
            return
        try:
            self.call(
                "submit_move",
                {"game_id": game_id, "action": action},
                timeout=max(1.0, deadline_ms / 1000.0),
            )
        except PlayerClientError:
            # The server already broadcast the rejection; let the
            # caller's on_game_finished handler deal with it.
            pass

    # ----- Defaults ---------------------------------------------------

    @staticmethod
    def _default_on_turn(state: Mapping[str, Any], deadline_ms: int) -> int:
        legal = state.get("legal_actions") or []
        if not legal:
            raise PlayerClientError("server reported no legal actions")
        return int(legal[0])
