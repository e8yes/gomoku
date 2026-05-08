"""Match protocol message types and JSON Lines codec.

The wire format is **strict JSON-RPC 2.0** (https://www.jsonrpc.org/specification)
carried as JSON Lines: one JSON object per line, UTF-8, terminated by ``\\n``.

Three message kinds share one envelope, distinguished by the fields
present rather than an explicit ``type`` field:

* **Request** — has ``method``, ``id`` (and usually ``params``).
* **Notification** — has ``method``, no ``id``. Server pushes match events
  (``your_turn``, ``state_changed``, …) as notifications.
* **Response** — has ``id`` plus exactly one of ``result`` or ``error``.

Every message carries ``"jsonrpc": "2.0"``. Error codes are integers per
JSON-RPC 2.0; the symbolic name is preserved in ``error.data.code_name``.

See ``docs/protocol_v2.md`` for the full method/event surface and
``spec.md`` for a developer-oriented guide.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any, Mapping

JSONRPC_VERSION = "2.0"


class ProtocolError(RuntimeError):
    """Raised by the codec when a line cannot be parsed as a valid message."""


class ErrorCode(IntEnum):
    """Wire error codes.

    Negative values are JSON-RPC 2.0 reserved codes; positive values are
    application-defined. Every error response carries the symbolic name
    in ``error.data.code_name`` for human readability.
    """

    # JSON-RPC 2.0 reserved range (-32768 .. -32000).
    PROTOCOL_ERROR = -32600   # "Invalid Request"
    UNKNOWN_METHOD = -32601   # "Method not found"
    BAD_REQUEST = -32602      # "Invalid params"
    ENGINE_ERROR = -32603     # "Internal error"

    # Application-defined codes (outside the reserved range).
    ILLEGAL_ACTION = 1001
    NOT_YOUR_TURN = 1002
    UNKNOWN_GAME = 1003
    UNKNOWN_PLAYER = 1004
    TERMINAL_POSITION = 1005
    CANCELLED = 1006
    AUTH_FAILED = 1007
    BUSY = 1008


_CODE_NAMES: dict[int, str] = {
    ErrorCode.PROTOCOL_ERROR: "protocol_error",
    ErrorCode.UNKNOWN_METHOD: "unknown_method",
    ErrorCode.BAD_REQUEST: "bad_request",
    ErrorCode.ENGINE_ERROR: "engine_error",
    ErrorCode.ILLEGAL_ACTION: "illegal_action",
    ErrorCode.NOT_YOUR_TURN: "not_your_turn",
    ErrorCode.UNKNOWN_GAME: "unknown_game",
    ErrorCode.UNKNOWN_PLAYER: "unknown_player",
    ErrorCode.TERMINAL_POSITION: "terminal_position",
    ErrorCode.CANCELLED: "cancelled",
    ErrorCode.AUTH_FAILED: "auth_failed",
    ErrorCode.BUSY: "busy",
}


def code_name(code: int | ErrorCode) -> str:
    """Return the symbolic name for an integer error code, or "" if unknown."""

    try:
        return _CODE_NAMES[int(code)]
    except KeyError:
        return ""


@dataclass
class Request:
    """JSON-RPC 2.0 request: method + id + params."""

    method: str
    id: str
    params: dict = field(default_factory=dict)


@dataclass
class Response:
    """JSON-RPC 2.0 response: id + (result XOR error)."""

    id: str | None
    result: dict | None = None
    error: dict | None = None

    @classmethod
    def ok(cls, request_id: str, result: Mapping[str, Any]) -> "Response":
        return cls(id=request_id, result=dict(result))

    @classmethod
    def err(
        cls,
        request_id: str | None,
        code: ErrorCode | int,
        message: str,
        data: Mapping[str, Any] | None = None,
    ) -> "Response":
        """Build an error response.

        The integer code goes on the wire (``error.code``); the symbolic
        name is added to ``error.data.code_name`` so logs and human
        readers see a meaningful identifier.
        """

        int_code = int(code)
        payload: dict = {"code": int_code, "message": message}
        merged: dict = dict(data or {})
        name = code_name(int_code)
        if name and "code_name" not in merged:
            merged["code_name"] = name
        if merged:
            payload["data"] = merged
        return cls(id=request_id, error=payload)


@dataclass
class Event:
    """JSON-RPC 2.0 notification: method + params, no id.

    Named ``Event`` rather than ``Notification`` because the match
    server only uses id-less messages for game events (``your_turn``,
    ``state_changed``, …).
    """

    method: str
    params: dict = field(default_factory=dict)


Message = Request | Response | Event


def encode_message(msg: Message) -> str:
    """Render a message as a single JSON Lines string (no trailing ``\\n``)."""

    obj: dict[str, Any] = {"jsonrpc": JSONRPC_VERSION}
    if isinstance(msg, Request):
        obj["method"] = msg.method
        obj["id"] = msg.id
        if msg.params:
            obj["params"] = msg.params
        else:
            obj["params"] = {}
    elif isinstance(msg, Event):
        obj["method"] = msg.method
        if msg.params:
            obj["params"] = msg.params
        else:
            obj["params"] = {}
    elif isinstance(msg, Response):
        obj["id"] = msg.id
        if msg.error is not None:
            obj["error"] = msg.error
        else:
            # JSON-RPC 2.0 requires exactly one of result/error; emit
            # an empty object rather than dropping the field.
            obj["result"] = msg.result if msg.result is not None else {}
    else:  # pragma: no cover — exhaustive over the Message union.
        raise TypeError(f"unknown message type {type(msg).__name__}")
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


def decode_message(line: str) -> Message:
    """Parse a JSON Lines string into a typed message.

    Strict JSON-RPC 2.0: every message must carry ``"jsonrpc": "2.0"``.
    Discrimination is by field presence, not an explicit ``type``:
        - ``method`` + ``id``   → Request
        - ``method`` no ``id``  → Event (notification)
        - ``id`` + ``result|error`` → Response
    """

    line = line.strip()
    if not line:
        raise ProtocolError("empty message")
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"invalid JSON: {exc}") from exc
    if not isinstance(obj, dict):
        raise ProtocolError("top-level message must be a JSON object")
    if obj.get("jsonrpc") != JSONRPC_VERSION:
        raise ProtocolError(
            f"missing or wrong 'jsonrpc' field; expected {JSONRPC_VERSION!r}"
        )

    has_method = "method" in obj
    has_id = "id" in obj
    has_result = "result" in obj
    has_error = "error" in obj

    if has_method and has_id:
        return _decode_request(obj)
    if has_method and not has_id:
        return _decode_event(obj)
    if has_id and (has_result or has_error):
        return _decode_response(obj)
    raise ProtocolError(
        "message must be a request, response, or notification"
    )


def _decode_request(obj: dict) -> Request:
    method = obj.get("method")
    rid = obj.get("id")
    if not isinstance(method, str):
        raise ProtocolError("request missing 'method'")
    if not isinstance(rid, str):
        raise ProtocolError("request 'id' must be a string")
    params = obj.get("params", {})
    if not isinstance(params, dict):
        raise ProtocolError("request 'params' must be an object")
    return Request(method=method, id=rid, params=params)


def _decode_response(obj: dict) -> Response:
    rid = obj.get("id")
    if rid is not None and not isinstance(rid, str):
        raise ProtocolError("response 'id' must be a string or null")
    result = obj.get("result")
    error = obj.get("error")
    if result is not None and error is not None:
        raise ProtocolError("response must not have both 'result' and 'error'")
    if result is None and error is None:
        raise ProtocolError("response must have 'result' or 'error'")
    if result is not None and not isinstance(result, dict):
        raise ProtocolError("response 'result' must be an object")
    if error is not None and not isinstance(error, dict):
        raise ProtocolError("response 'error' must be an object")
    if error is not None:
        if not isinstance(error.get("code"), int):
            raise ProtocolError("error 'code' must be an integer")
        if not isinstance(error.get("message"), str):
            raise ProtocolError("error 'message' must be a string")
    return Response(id=rid, result=result, error=error)


def _decode_event(obj: dict) -> Event:
    method = obj.get("method")
    if not isinstance(method, str):
        raise ProtocolError("notification missing 'method'")
    params = obj.get("params", {})
    if not isinstance(params, dict):
        raise ProtocolError("notification 'params' must be an object")
    return Event(method=method, params=params)
