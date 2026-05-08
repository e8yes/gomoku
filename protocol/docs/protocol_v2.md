# Gomoku Match Protocol v2 (JSON-RPC 2.0)

Project-neutral wire protocol for Gomoku Swap2 matchmaking. The match
server is the source of truth; player engines and observers connect as
clients. This document is the **wire-format reference**. For a guided
walkthrough — rules of the game, full client examples, error handling
patterns — see [`../spec.md`](../spec.md).

## Wire format

JSON Lines: one JSON object per line, UTF-8, terminated by `\n`. Each
line is a strict [JSON-RPC 2.0](https://www.jsonrpc.org/specification)
message. Three message kinds, distinguished by the fields present:

```text
Request:      {"jsonrpc":"2.0", "id":"req-7", "method":"submit_move", "params":{...}}
Response OK:  {"jsonrpc":"2.0", "id":"req-7", "result":{"ok":true,"terminal":false}}
Response err: {"jsonrpc":"2.0", "id":"req-7", "error":{"code":1001,"message":"...","data":{...}}}
Notification: {"jsonrpc":"2.0",              "method":"your_turn",  "params":{...}}
```

Discrimination rules:

| Has `method`? | Has `id`? | Has `result`/`error`? | Kind |
|---|---|---|---|
| yes | yes | no | Request |
| yes | no  | no | Notification (server-pushed event) |
| no  | yes | yes (exactly one) | Response |

Every message MUST carry `"jsonrpc": "2.0"`. Messages that omit it or
carry a different value are rejected with `protocol_error`.

`id` is opaque, client-chosen for requests, and the server echoes it on
the matching response. `id` is `null` only when the server cannot
recover the request id (e.g. parse error). Notifications have no `id`
and never expect a response.

JSON-RPC 2.0 batches (arrays of messages) are not supported in v2.

## Action encoding

A 15×15 board has `230` actions: `0..224` for placements,
`225..229` for Swap2 control actions.

| ID range | Meaning |
|---|---|
| `0 .. N²-1` | Placement at `(x, y)` where `x = id % N`, `y = id // N`. Top-left is `(0, 0)`. |
| `N² + 0` | `swap2_choose_white` — responder takes white; opener keeps black. |
| `N² + 1` | `swap2_choose_black` — responder takes black; opener becomes white. |
| `N² + 2` | `swap2_place_two` — responder places two more stones (white, then black). |
| `N² + 3` | `choose_white` — opener takes white after the responder's two extra stones. |
| `N² + 4` | `choose_black` — opener takes black. |

Methods that take or return an action accept either the integer id or a
string label (`"(8,7)"` for placements, `"swap2_choose_white"` for
control actions). The server returns both forms in `state_changed`.

## Roles

A connection can hold more than one role.

| Role | Methods | Notifications received |
|---|---|---|
| Player | `handshake`, `register`, `submit_move`, `resign` | `your_turn`, `state_changed`, `game_started`, `game_finished` |
| Observer | `handshake`, `subscribe`, `query_state`, `query_history` | `state_changed`, `game_started`, `game_finished` |
| Admin | `handshake`, `create_match` (and observer methods) | match-list events |

## Methods

### `handshake`

Always the first request on a fresh connection.

Params: `{ "client_name"?: string, "protocol_version": "2.0", "auth_token"?: string, "admin_token"?: string }`

Result: `{ "engine_name", "protocol_version", "supported_rules", "supported_board_sizes", "action_count", "capabilities": [...], "auth_required": bool }`

If the server was started with a shared secret (`--auth-token` /
`--auth-token-env`), every connection MUST send the matching
`auth_token`. A missing or wrong token returns `auth_failed` and the
server then closes the socket. Loopback servers may run with auth
disabled; clients can pre-flight by sending an empty handshake and
inspecting `auth_required` in the result.

### `register` (player)

Claims a player identity. Names are unique on the server. If a player
with the same name has been disconnected and the match has a non-zero
`disconnect_grace_ms`, registering with that name within the grace
window resumes the player's seat (`reconnected: true`) and the server
re-emits `your_turn` if the player is on the clock.

Params: `{ "name": string }` — Result: `{ "player_id": string, "name": string, "reconnected": bool }`

### `subscribe` (observer)

Without `game_id`: subscribe to all current and future games.

Params: `{ "game_id"?: string }` — Result: `{ "subscribed": "all" | game_id }`

### `create_match` (admin)

Pairs two registered players into a new game. Requires `admin_token`
to be presented in `handshake`.

Params: `{ "player_a": name, "player_b": name, "board_size"?: 15, "deadline_ms_per_move"?: 5000, "disconnect_grace_ms"?: 0 }`

`disconnect_grace_ms` is a per-game window during which a participant
may drop their TCP connection and re-register with the same name to
resume play. With `0` (the default), a disconnect immediately forfeits
the absent player.

Result: `{ "game_id": string }`

### `submit_move` (player, on turn)

Apply the player's move. Server validates against canonical board.

Params: `{ "game_id": string, "action": int|label }`

Result: `{ "ok": true, "terminal": bool }`

Errors: `not_your_turn`, `illegal_action`, `terminal_position`, `unknown_game`.

### `resign` (player)

The opposite player wins.

Params: `{ "game_id": string }` — Result: `{ "ok": true }`

### `query_state` / `query_history`

Read-only inspection. Useful for late-joining observers and post-game
analysis. `query_history` requires admin privileges.

## Notifications (server-pushed events)

### `game_started`

Broadcast to participants and observers when a match is created.

```json
{"jsonrpc":"2.0","method":"game_started","params":{
  "game_id": "g1",
  "players": {"A": "alice", "B": "bob"},
  "settings": {"board_size":15,"deadline_ms_per_move":5000},
  "state": { ... initial board state ... }
}}
```

### `your_turn`

Pushed to whichever player is on the clock. The recipient must reply
with `submit_move` before the deadline elapses, or forfeit.

```json
{"jsonrpc":"2.0","method":"your_turn","params":{
  "game_id": "g1",
  "state": { ... full board state ... },
  "deadline_ms": 5000
}}
```

### `state_changed`

Pushed to participants and subscribed observers after each accepted
move.

```json
{"jsonrpc":"2.0","method":"state_changed","params":{
  "game_id": "g1",
  "action": 113,
  "label": "(8,7)",
  "by_player": "alice",
  "new_state": { ... updated state ... }
}}
```

### `game_finished`

Broadcast when the game ends, regardless of reason.

```json
{"jsonrpc":"2.0","method":"game_finished","params":{
  "game_id": "g1",
  "result": "PLAYER_A_WIN" | "PLAYER_B_WIN" | "DRAW",
  "reason": "five_in_a_row" | "draw" | "timeout" | "resignation" | "disconnect" | "illegal_move",
  "winner": "alice" | null,
  "final_state": { ... },
  "ratings"?: {
    "player_a": {"name": "alice", "pre": 1212.0, "post": 1228.4, "k": 40.0},
    "player_b": {"name": "bob",   "pre": 1188.0, "post": 1171.6, "k": 40.0}
  }
}}
```

The `ratings` field is present only if the server was started with an
Elo engine attached (`--elo` on the CLI, or `MatchServer(elo=...)`
in-process). Plain Elo with K schedule 40 → 20 → 10 at 30 / 100 game
thresholds.

## State payload schema

Every notification that carries a `state` (or `new_state`,
`final_state`) returns the same shape:

```json
{
  "board_size": 15,
  "phase": "PLACE_INITIAL_THREE" | "SWAP2_DECISION" | "SWAP2_PLACE_TWO" | "CHOOSE_COLOR" | "STANDARD",
  "phase_id": 0,
  "current_player": "A" | "B",
  "stone_to_place": "BLACK" | "WHITE" | "EMPTY",
  "move_count": 0,
  "player_stones": {"A": "BLACK"|"WHITE"|"EMPTY", "B": "..."},
  "result": "UNDETERMINED" | "PLAYER_A_WIN" | "PLAYER_B_WIN" | "DRAW",
  "result_id": 0,
  "moves": [int, ...],
  "cells": [int, ...],
  "legal_actions": [int, ...]
}
```

`cells` is row-major: `cells[y * board_size + x]` is `0` (empty), `1`
(black), or `2` (white).

The state never names the recipient. To know which seat you occupy,
match your registered `name` against `players.A` / `players.B` in
`game_started`. Your stone color for the rest of the game is then
`player_stones[your_seat]` — but during `PLACE_INITIAL_THREE` and
`SWAP2_PLACE_TWO` that field is `EMPTY` because seat-color isn't bound
yet; in those phases the relevant field is `stone_to_place` (the color
of the *next* placement, regardless of seat). See `spec.md`.

## Transports

The protocol is transport-agnostic. Two transports ship today:

- **In-process** — `InProcessTransport.pair()` returns a connected pair
  of queue-backed transports. Used by tests and embedded clients.
- **TCP** — line-buffered JSON Lines over a TCP socket. Run a server
  with `python -m gomoku_match --listen tcp://0.0.0.0:7901`. Clients
  connect with `connect_tcp(host, port)` and pass the result to
  `PlayerClient` / `ObserverClient`. Messages must not embed raw `\n`;
  the transport rejects oversized lines (default 4 MiB).

WebSocket is intentionally deferred: the JSON Lines wire format is
identical to what a WS implementation would carry, and the asyncio
split would complicate the otherwise-threaded server. A WS transport
can be added later as a pure adapter on top of the existing `Transport`
ABC.

## Errors

Every error response carries a structured `error` object with an
integer `code` (per JSON-RPC 2.0), a human-readable `message`, and an
optional `data` field with structured context. The symbolic name of
the code is always echoed in `data.code_name`.

```json
{"code": 1001, "message": "...", "data": {"code_name": "illegal_action", ...}}
```

| Code | Symbolic name | Meaning |
|---:|---|---|
| `-32600` | `protocol_error` | Message could not be parsed or violated the envelope contract. |
| `-32601` | `unknown_method` | The server does not implement that method. |
| `-32602` | `bad_request` | Method-specific argument validation failed. |
| `-32603` | `engine_error` | Internal server error; bug. |
| `1001` | `illegal_action` | The action is not legal in the current phase or position. |
| `1002` | `not_your_turn` | A player attempted to move when not on the clock. |
| `1003` | `unknown_game` | `game_id` does not exist. |
| `1004` | `unknown_player` | `player_id` / name does not exist or is not registered. |
| `1005` | `terminal_position` | The game has already finished. |
| `1006` | `cancelled` | The request was cancelled by stop or shutdown. |
| `1007` | `auth_failed` | Shared-secret authentication did not match. |
| `1008` | `busy` | Reserved for future use (concurrency limits). |

Negative codes occupy the JSON-RPC 2.0 reserved range
(-32768 .. -32000) and are emitted exactly as the spec prescribes for
their meaning. Application errors are positive integers outside that
range so they don't collide with future JSON-RPC additions.

## Persistence

The server may journal every match to a SQLite database (one row per
match plus one row per move). Tools wishing to persist Elo or run
post-game analysis should read from the same store rather than relying
on observer events alone — events can be missed by clients that
disconnect mid-game.

## Versioning

The protocol uses semver-style strings. The server's major version
must match the client's; minor versions add optional capabilities and
new methods that older clients can ignore. Breaking changes bump the
major and require a new spec doc (`protocol_v3.md`). The v1 envelope
(explicit `"type"` field, string error codes) was retired in v2 in
favor of strict JSON-RPC 2.0.
