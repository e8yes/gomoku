"""ObserverClient: read-only client that records state-change events.

Useful as both a spectator and a test harness. Stores every received
event in a thread-safe deque so tests can assert on the sequence
without racing with the reader thread.
"""

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
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


class ObserverClientError(RuntimeError):
    pass


class ObserverClient:
    def __init__(
        self,
        transport: Transport,
        *,
        on_event: Callable[[Event], None] | None = None,
    ) -> None:
        self._transport = transport
        self._on_event = on_event
        self._lock = threading.Lock()
        self._pending: dict[str, "Queue[Response]"] = {}
        self._stop = threading.Event()
        self._events: deque[Event] = deque()
        self._event_added = threading.Event()
        self._reader = threading.Thread(
            target=self._reader_loop, name="observer-reader", daemon=True
        )
        self._reader.start()

    # ----- Public surface ---------------------------------------------

    def handshake(
        self, *, auth_token: str | None = None, timeout: float = 5.0
    ) -> dict:
        params: dict = {"protocol_version": "2.0"}
        if auth_token is not None:
            params["auth_token"] = auth_token
        return self.call("handshake", params, timeout=timeout)

    def subscribe(self, game_id: str | None = None, *, timeout: float = 5.0) -> dict:
        params: dict = {} if game_id is None else {"game_id": game_id}
        return self.call("subscribe", params, timeout=timeout)

    def query_state(self, game_id: str, *, timeout: float = 5.0) -> dict:
        return self.call("query_state", {"game_id": game_id}, timeout=timeout)

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
            raise ObserverClientError("transport closed") from exc
        try:
            response = q.get(timeout=timeout)
        except Exception as exc:
            with self._lock:
                self._pending.pop(request.id, None)
            raise ObserverClientError(
                f"{method} timed out after {timeout}s"
            ) from exc
        if response.error is not None:
            err = response.error
            data = err.get("data") or {}
            symbol = data.get("code_name") if isinstance(data, dict) else None
            label = symbol or str(err.get("code"))
            raise ObserverClientError(
                f"{method} returned {label}: {err.get('message')}"
            )
        return response.result or {}

    def events(self) -> list[Event]:
        with self._lock:
            return list(self._events)

    def wait_for_event(
        self,
        method: str,
        *,
        timeout: float = 5.0,
    ) -> Event:
        # Track an absolute monotonic deadline. Each pass through the
        # loop only waits for the time remaining, so a slow trickle of
        # *unrelated* events (which set ``self._event_added``) cannot
        # extend the total wait beyond ``timeout``.
        deadline = time.monotonic() + timeout

        def _has() -> Event | None:
            with self._lock:
                for e in self._events:
                    if e.method == method:
                        return e
                return None

        first = _has()
        if first is not None:
            return first
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ObserverClientError(
                    f"no '{method}' event within {timeout}s"
                )
            self._event_added.wait(timeout=remaining)
            self._event_added.clear()
            hit = _has()
            if hit is not None:
                return hit
            if self._stop.is_set():
                raise ObserverClientError(
                    f"observer closed before receiving '{method}'"
                )

    def close(self) -> None:
        self._stop.set()
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
                self._stop.set()
                self._event_added.set()
                with self._lock:
                    queues = list(self._pending.values())
                    self._pending.clear()
                for q in queues:
                    q.put(Response.err(None, ErrorCode.ENGINE_ERROR, "closed"))
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
                with self._lock:
                    self._events.append(msg)
                self._event_added.set()
                if self._on_event is not None:
                    try:
                        self._on_event(msg)
                    except Exception:  # pragma: no cover
                        pass
