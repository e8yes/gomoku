"""TCP transport for the match protocol.

Line-oriented JSON Lines over TCP. ``connect_tcp`` returns a client-side
``Transport``; ``TcpListener`` accepts connections on a port and hands
each one to the server via a callback (typically ``MatchServer.attach``).

Threading model: each socket is owned by one Transport. Reads block on
the socket up to a ``timeout``, raising :class:`TimeoutError`. Writes
hold a per-transport lock so concurrent senders cannot interleave bytes.
"""

from __future__ import annotations

import socket
import threading
from typing import Callable
from urllib.parse import urlparse

from .transports import Transport, TransportClosed

_RECV_CHUNK = 8192
_MAX_LINE_BYTES = 4 * 1024 * 1024  # 4 MiB ceiling per JSON Lines message.


class TcpTransport(Transport):
    """Line-buffered JSON Lines TCP transport."""

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._buffer = bytearray()
        self._closed = threading.Event()
        self._send_lock = threading.Lock()

    # ----- Transport ABC ---------------------------------------------

    def send(self, line: str) -> None:
        if self._closed.is_set():
            raise TransportClosed("transport is closed")
        if "\n" in line:
            raise ValueError("JSON Lines messages must not embed '\\n'")
        data = (line + "\n").encode("utf-8")
        try:
            with self._send_lock:
                self._sock.sendall(data)
        except OSError as exc:
            self._mark_closed()
            raise TransportClosed(f"send failed: {exc}") from exc

    def recv(self, timeout: float | None = None) -> str:
        if self._closed.is_set():
            raise TransportClosed("transport is closed")
        # Fast path: a complete line is already buffered.
        line = self._take_line()
        if line is not None:
            return line
        try:
            self._sock.settimeout(timeout)
        except OSError as exc:  # pragma: no cover — already-closed socket.
            self._mark_closed()
            raise TransportClosed(str(exc)) from exc
        while True:
            try:
                chunk = self._sock.recv(_RECV_CHUNK)
            except socket.timeout as exc:
                raise TimeoutError("no message within timeout") from exc
            except OSError as exc:
                self._mark_closed()
                raise TransportClosed(f"recv failed: {exc}") from exc
            if not chunk:
                self._mark_closed()
                raise TransportClosed("peer closed the channel")
            self._buffer.extend(chunk)
            if len(self._buffer) > _MAX_LINE_BYTES and b"\n" not in self._buffer:
                self._mark_closed()
                raise TransportClosed("line exceeded max length without newline")
            line = self._take_line()
            if line is not None:
                return line

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._mark_closed()
        try:
            self._sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self._sock.close()
        except OSError:  # pragma: no cover
            pass

    @property
    def closed(self) -> bool:
        return self._closed.is_set()

    # ----- Internals --------------------------------------------------

    def _take_line(self) -> str | None:
        idx = self._buffer.find(b"\n")
        if idx < 0:
            return None
        raw = bytes(self._buffer[:idx])
        del self._buffer[: idx + 1]
        # Strip a stray CR if a peer sends CRLF.
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        # Strict UTF-8: reject malformed sequences rather than silently
        # substituting U+FFFD. A hostile peer could otherwise smuggle
        # bytes that decode-then-parse as a valid request with corrupted
        # parameters (e.g. player names, game ids). Treat the decode
        # failure as a transport-level protocol error so the caller can
        # surface a structured PROTOCOL_ERROR response.
        try:
            return raw.decode("utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            self._mark_closed()
            raise TransportClosed(f"invalid UTF-8 from peer: {exc}") from exc

    def _mark_closed(self) -> None:
        self._closed.set()


def connect_tcp(host: str, port: int, *, timeout: float = 5.0) -> TcpTransport:
    """Open a client-side TCP transport to ``host:port``."""
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(None)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return TcpTransport(sock)


def parse_listen_url(url: str) -> tuple[str, int]:
    """Parse ``tcp://host:port`` into ``(host, port)``.

    Bare ``host:port`` is also accepted for convenience.
    """
    if "://" not in url:
        url = "tcp://" + url
    parsed = urlparse(url)
    if parsed.scheme != "tcp":
        raise ValueError(f"unsupported listen scheme: {parsed.scheme!r}")
    host = parsed.hostname or "0.0.0.0"
    if parsed.port is None:
        raise ValueError(f"listen URL missing port: {url!r}")
    return host, int(parsed.port)


class TcpListener:
    """Accept TCP connections and hand each socket to a callback.

    Designed to feed ``MatchServer.attach``. Owns a daemon thread that
    accepts connections; pass each accepted ``TcpTransport`` to the
    callback synchronously (the server starts its own per-connection
    reader thread, so the handler returns quickly).
    """

    def __init__(
        self,
        host: str,
        port: int,
        on_connection: Callable[[TcpTransport], None],
        *,
        backlog: int = 16,
    ) -> None:
        self._host = host
        self._port = port
        self._on_connection = on_connection
        self._backlog = backlog
        self._sock: socket.socket | None = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._bound_port: int | None = None

    @property
    def port(self) -> int:
        if self._bound_port is None:
            raise RuntimeError("listener is not started")
        return self._bound_port

    def start(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self._host, self._port))
        sock.listen(self._backlog)
        sock.settimeout(0.2)
        self._sock = sock
        self._bound_port = sock.getsockname()[1]
        self._thread = threading.Thread(
            target=self._accept_loop,
            name=f"match-listener-{self._bound_port}",
            daemon=True,
        )
        self._thread.start()

    def stop(self, *, wait: bool = True) -> None:
        self._stop.set()
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:  # pragma: no cover
                pass
        if wait and self._thread is not None:
            self._thread.join(timeout=2.0)

    def _accept_loop(self) -> None:
        assert self._sock is not None
        while not self._stop.is_set():
            try:
                client_sock, _addr = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            client_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            client_sock.settimeout(None)
            transport = TcpTransport(client_sock)
            try:
                self._on_connection(transport)
            except Exception:  # pragma: no cover — caller bug.
                transport.close()
