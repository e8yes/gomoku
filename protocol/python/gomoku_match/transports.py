"""Transports for the match protocol.

A :class:`Transport` is a duplex byte channel that exchanges single
JSON Lines per send/recv. Workstream A ships only an in-process
queue-pair transport sufficient for tests; network transports (TCP,
WebSocket) land in Workstream B.
"""

from __future__ import annotations

import queue
import threading
from abc import ABC, abstractmethod
from typing import Optional


class TransportClosed(RuntimeError):
    """Raised by ``recv`` when the peer has closed the channel."""


class Transport(ABC):
    """Bidirectional JSON-Lines channel."""

    @abstractmethod
    def send(self, line: str) -> None: ...

    @abstractmethod
    def recv(self, timeout: float | None = None) -> str: ...

    @abstractmethod
    def close(self) -> None: ...

    @property
    @abstractmethod
    def closed(self) -> bool: ...


class _QueueTransport(Transport):
    def __init__(
        self,
        in_queue: "queue.Queue[Optional[str]]",
        out_queue: "queue.Queue[Optional[str]]",
        peer: "Optional[_QueueTransport]" = None,
    ) -> None:
        self._in = in_queue
        self._out = out_queue
        self._peer = peer
        self._closed = threading.Event()

    def link_peer(self, peer: "_QueueTransport") -> None:
        self._peer = peer

    def send(self, line: str) -> None:
        if self._closed.is_set():
            raise TransportClosed("transport is closed")
        if "\n" in line:
            raise ValueError("JSON Lines messages must not embed '\\n'")
        self._out.put(line)

    def recv(self, timeout: float | None = None) -> str:
        try:
            line = self._in.get(timeout=timeout)
        except queue.Empty as exc:
            raise TimeoutError("no message within timeout") from exc
        if line is None:
            self._closed.set()
            raise TransportClosed("peer closed the channel")
        return line

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._closed.set()
        # Sending None tells the peer's recv loop that we're done.
        try:
            self._out.put_nowait(None)
        except queue.Full:  # pragma: no cover — unbounded queue.
            pass
        if self._peer is not None and not self._peer.closed:
            self._peer._closed.set()

    @property
    def closed(self) -> bool:
        return self._closed.is_set()


class InProcessTransport:
    """Factory + namespace for the in-process transport pair.

    ``InProcessTransport.pair()`` returns ``(client_side, server_side)``
    transports that talk to each other through two unbounded queues.
    """

    @staticmethod
    def pair() -> tuple[Transport, Transport]:
        client_to_server: "queue.Queue[Optional[str]]" = queue.Queue()
        server_to_client: "queue.Queue[Optional[str]]" = queue.Queue()
        client_side = _QueueTransport(server_to_client, client_to_server)
        server_side = _QueueTransport(client_to_server, server_to_client)
        client_side.link_peer(server_side)
        server_side.link_peer(client_side)
        return client_side, server_side
