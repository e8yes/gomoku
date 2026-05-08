"""Protocol message round-trip tests (JSON-RPC 2.0 envelope)."""

from __future__ import annotations

import json
import unittest

from gomoku_match.protocol import (
    JSONRPC_VERSION,
    ErrorCode,
    Event,
    ProtocolError,
    Request,
    Response,
    code_name,
    decode_message,
    encode_message,
)


class CodecTests(unittest.TestCase):
    def test_request_round_trip(self) -> None:
        req = Request(method="register", id="r1", params={"name": "alice"})
        wire = encode_message(req)
        # Wire must carry the JSON-RPC discriminator.
        self.assertIn('"jsonrpc":"2.0"', wire)
        self.assertNotIn('"type"', wire)
        decoded = decode_message(wire)
        self.assertIsInstance(decoded, Request)
        self.assertEqual(decoded.method, "register")
        self.assertEqual(decoded.id, "r1")
        self.assertEqual(decoded.params, {"name": "alice"})

    def test_response_ok_round_trip(self) -> None:
        rsp = Response.ok("r1", {"player_id": "p1"})
        wire = encode_message(rsp)
        self.assertIn('"jsonrpc":"2.0"', wire)
        decoded = decode_message(wire)
        self.assertIsInstance(decoded, Response)
        self.assertEqual(decoded.id, "r1")
        self.assertEqual(decoded.result, {"player_id": "p1"})
        self.assertIsNone(decoded.error)

    def test_response_err_round_trip(self) -> None:
        rsp = Response.err(
            "r1",
            ErrorCode.ILLEGAL_ACTION,
            "out of range",
            data={"action": 999},
        )
        wire = encode_message(rsp)
        decoded = decode_message(wire)
        self.assertIsInstance(decoded, Response)
        self.assertIsNone(decoded.result)
        assert decoded.error is not None
        # Wire code is the integer (per JSON-RPC 2.0).
        self.assertEqual(decoded.error["code"], int(ErrorCode.ILLEGAL_ACTION))
        # Symbolic name preserved in error.data.code_name.
        self.assertEqual(decoded.error["data"]["code_name"], "illegal_action")
        self.assertEqual(decoded.error["data"]["action"], 999)

    def test_response_err_null_id_for_parse_failure(self) -> None:
        # JSON-RPC 2.0 §5: id is null when it could not be determined
        # (e.g. parse error). The codec must accept and round-trip null.
        rsp = Response.err(None, ErrorCode.PROTOCOL_ERROR, "bad input")
        wire = encode_message(rsp)
        obj = json.loads(wire)
        self.assertIsNone(obj["id"])
        decoded = decode_message(wire)
        assert isinstance(decoded, Response)
        self.assertIsNone(decoded.id)

    def test_event_is_idless_notification(self) -> None:
        ev = Event(method="state_changed", params={"action": 113})
        wire = encode_message(ev)
        obj = json.loads(wire)
        self.assertEqual(obj["jsonrpc"], JSONRPC_VERSION)
        self.assertEqual(obj["method"], "state_changed")
        self.assertNotIn("id", obj)
        decoded = decode_message(wire)
        self.assertIsInstance(decoded, Event)
        self.assertEqual(decoded.method, "state_changed")
        self.assertEqual(decoded.params["action"], 113)

    def test_rejects_messages_without_jsonrpc_version(self) -> None:
        # Legacy v1 envelope (with "type" but no "jsonrpc") must be rejected.
        legacy = (
            '{"type":"request","id":"r1","method":"register","params":{}}'
        )
        with self.assertRaises(ProtocolError):
            decode_message(legacy)

    def test_rejects_wrong_jsonrpc_version(self) -> None:
        bad = '{"jsonrpc":"1.0","id":"r1","method":"x","params":{}}'
        with self.assertRaises(ProtocolError):
            decode_message(bad)

    def test_rejects_response_with_both_result_and_error(self) -> None:
        bad = (
            '{"jsonrpc":"2.0","id":"r1","result":{},'
            '"error":{"code":-32603,"message":"x"}}'
        )
        with self.assertRaises(ProtocolError):
            decode_message(bad)

    def test_rejects_error_with_non_integer_code(self) -> None:
        bad = (
            '{"jsonrpc":"2.0","id":"r1",'
            '"error":{"code":"illegal_action","message":"x"}}'
        )
        with self.assertRaises(ProtocolError):
            decode_message(bad)

    def test_protocol_errors(self) -> None:
        for bad in [
            "",
            "not json",
            '"hi"',
            "[]",
            '{"jsonrpc":"2.0"}',  # neither request, response, nor event
        ]:
            with self.assertRaises(ProtocolError):
                decode_message(bad)

    def test_jsonrpc_2_0_doc_examples(self) -> None:
        # Spec-doc example lines: every snippet in spec.md / protocol_v2.md
        # must round-trip through the codec.
        request_line = (
            '{"jsonrpc":"2.0","id":"req-7","method":"submit_move",'
            '"params":{"game_id":"g1","action":113}}'
        )
        decoded = decode_message(request_line)
        assert isinstance(decoded, Request)
        self.assertEqual(decoded.method, "submit_move")
        self.assertEqual(decoded.params["action"], 113)

        response_line = (
            '{"jsonrpc":"2.0","id":"req-7",'
            '"result":{"ok":true,"terminal":false}}'
        )
        decoded = decode_message(response_line)
        assert isinstance(decoded, Response)
        self.assertEqual(decoded.result, {"ok": True, "terminal": False})

        notification_line = (
            '{"jsonrpc":"2.0","method":"your_turn",'
            '"params":{"game_id":"g1","state":{},"deadline_ms":5000}}'
        )
        decoded = decode_message(notification_line)
        assert isinstance(decoded, Event)
        self.assertEqual(decoded.method, "your_turn")

    def test_code_name_helper(self) -> None:
        self.assertEqual(code_name(ErrorCode.ILLEGAL_ACTION), "illegal_action")
        self.assertEqual(code_name(-32601), "unknown_method")
        self.assertEqual(code_name(99999), "")


if __name__ == "__main__":
    unittest.main()
