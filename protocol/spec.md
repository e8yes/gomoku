# gomoku_match — Developer Specification

This document is the reference for anyone building a player engine or
spectator UI against the Gomoku Match server. It covers:

- What the server is and what it enforces.
- The Gomoku Swap2 rules the server is the source of truth for.
- The full wire protocol (JSON-RPC 2.0) with field-by-field semantics.
- Step-by-step Python walkthroughs for player and spectator clients.
- Method/notification reference, error codes, and deployment notes.

The wire format is also documented as a terse, language-neutral
reference in [`docs/protocol_v2.md`](docs/protocol_v2.md). When the two
disagree, this spec is normative for *behavior*; `protocol_v2.md` is
normative for the literal wire bytes.

---

## 1. Overview

`gomoku_match` is a standalone match server. It owns the canonical
game state for every game it hosts, validates every submitted move
against its own pure-Python rules engine, dispatches turn
notifications to whichever player is on the clock, broadcasts state
changes to subscribed observers, and (optionally) maintains a
centralised Elo table.

It is intentionally **project-neutral**: it has no dependency on any
particular Gomoku engine. Any client implementing the documented
protocol can register and play. Two transports ship today:

- **In-process** — used by tests and embedded clients.
- **TCP** — line-buffered JSON Lines.

WebSocket can be added later as a pure transport adapter; the wire
format would be identical.

### Roles

A connection can hold more than one role simultaneously.

| Role | Sends | Receives |
|---|---|---|
| **Player** | `handshake`, `register`, `submit_move`, `resign` | `your_turn`, `state_changed`, `game_started`, `game_finished` |
| **Observer** | `handshake`, `subscribe`, `query_state`, `query_history` | `state_changed`, `game_started`, `game_finished` |
| **Admin** | `handshake`, `create_match` (and observer methods) | match-list events |

### Trust model

The server is the referee. Clients never share or compute game state
themselves — they receive it. Every submitted move is validated
against the server's `Board`. An illegal move returns
`illegal_action` and (depending on policy) costs the player the
match. A timeout forfeits. A disconnect forfeits unless
`disconnect_grace_ms` is configured for the match.

---

## 2. Gomoku Swap2 rules enforced by the server

The server enforces a strict implementation of Gomoku Swap2 on a
square board (default 15×15, configurable down to 5×5). Two players,
A and B; A is the **opener**, B is the **responder**.

### 2.1 Board and stones

- An `N×N` grid of intersections, each cell either empty, black, or white.
- Cells are addressed by `(x, y)` with `(0, 0)` at the top-left.
  `cells` in the state payload is row-major:
  `cells[y * N + x]`.
- Stone values: `0 = empty`, `1 = black`, `2 = white`.

### 2.2 Win conditions

The server enforces the **exact-five** rule: a player wins by
forming a run of *exactly five* same-colour stones in a row, column,
or diagonal. A run of six or more (an "overline") does **not** win
— it leaves the game undetermined and play continues. The exact-five
restriction is the standard tournament rule for balanced Gomoku and
is what the engine is fixed to; the freestyle variant (≥ 5 wins) and
the `rule_variant` parameter from earlier drafts have been removed.

The terminal check runs after every placement in `STANDARD` phase.
During the Swap2 opening phases (described next) no win can be scored
because seat-to-color hasn't been bound yet — a five-in-a-row in those
phases would set the result to `DRAW` rather than awarding a win.

### 2.3 Swap2 opening — the five-phase machine

Swap2 is a balanced opening protocol that gives the responder rich
options to either accept the opener's position, swap colors, or
reshape the position before committing to a color. The server walks
every game through up to five phases:

```
PLACE_INITIAL_THREE  →  SWAP2_DECISION  →  SWAP2_PLACE_TWO  →  CHOOSE_COLOR  →  STANDARD
                                       ↘ (path 1)        ↗
                                                ↘ (path 2)
                          (path 3, direct color choice)
```

#### Phase `PLACE_INITIAL_THREE`

- The **opener (A)** places three stones in **B-W-B** order.
- All three placements happen during this phase; the current player
  stays A throughout. `stone_to_place` cycles `BLACK → WHITE → BLACK`.
- After move 3 the phase transitions to `SWAP2_DECISION`.

#### Phase `SWAP2_DECISION`

- The **responder (B)** picks one of three control actions:
  - `swap2_choose_white` — B takes white. A keeps black. Phase
    becomes `STANDARD` with white to move (so B plays move 4).
  - `swap2_choose_black` — B takes black. A becomes white. Phase
    becomes `STANDARD` with white to move (so A plays move 4).
  - `swap2_place_two` — B declines color, places two more stones,
    then hands the color decision back to A. Phase becomes
    `SWAP2_PLACE_TWO`.

#### Phase `SWAP2_PLACE_TWO`

- The **responder (B)** places two stones in **W-B** order
  (the 4th and 5th stones on the board).
- After move 5 the phase becomes `CHOOSE_COLOR`.

#### Phase `CHOOSE_COLOR`

- The **opener (A)** picks one of two control actions:
  - `choose_white` — A takes white, B takes black. Phase becomes
    `STANDARD` with white to move (so A plays move 6).
  - `choose_black` — A takes black, B takes white. Phase becomes
    `STANDARD` with white to move (so B plays move 6).

#### Phase `STANDARD`

- Players alternate placements, white moves on every odd ply, black
  on every even ply (counting from move 1). After every placement the
  server checks for a terminal win/draw.
- The game ends when one side wins by stone, the board fills (`DRAW`),
  or a player resigns / times out / disconnects past grace.

### 2.4 Stone-to-place vs. player-stone

Two related-but-distinct concepts that confuse first-time client
implementers:

| Concept | Field | Meaning |
|---|---|---|
| The color of the **next placement** | `state.stone_to_place` | Which color goes onto the board next. Always `BLACK` or `WHITE` during a placement-driven phase; `EMPTY` only during the two control phases (`SWAP2_DECISION`, `CHOOSE_COLOR`) where the next action is a control action, not a placement. |
| The seat-to-color **assignment for the rest of the game** | `state.player_stones[seat]` | Which color a given seat holds for the remainder of the match. `EMPTY` during the opening (`PLACE_INITIAL_THREE`, `SWAP2_PLACE_TWO`) because seat-to-color hasn't been bound yet. Becomes `BLACK` or `WHITE` once the Swap2 decision/choose action fires. |

A player asking "what stone do I hold?" has a different answer
depending on phase:

- During `STANDARD`: `player_stones[my_seat]`.
- During the opening, you don't *hold* a stone — you place whatever
  `stone_to_place` says, and your final color is decided later.

### 2.5 Turn ownership

`state.current_player` is the seat (`"A"` or `"B"`) that must act
next. The server only delivers `your_turn` to the connection it has
registered for that seat. The clock starts ticking when `your_turn`
goes out and a `submit_move` arriving from the wrong seat returns
`not_your_turn`.

---

## 3. Wire format

JSON Lines: one [JSON-RPC 2.0](https://www.jsonrpc.org/specification)
message per line, UTF-8, terminated by `\n`. Three message kinds:

```text
Request:       {"jsonrpc":"2.0", "id":"r-7", "method":"submit_move", "params":{...}}
Response (ok): {"jsonrpc":"2.0", "id":"r-7", "result":{...}}
Response (err):{"jsonrpc":"2.0", "id":"r-7", "error":{"code":1001,"message":"...","data":{...}}}
Notification:  {"jsonrpc":"2.0", "method":"your_turn", "params":{...}}
```

Discrimination rules:

| Has `method` | Has `id` | Has `result`/`error` | Kind |
|---|---|---|---|
| ✓ | ✓ | ✗ | Request |
| ✓ | ✗ | ✗ | Notification (server-pushed event) |
| ✗ | ✓ | ✓ (exactly one) | Response |

Every message MUST carry `"jsonrpc": "2.0"`. The `id` is opaque,
client-chosen for requests, and the server echoes it on the matching
response. `id` is `null` only when the server cannot recover the
request id (parse error). Notifications never have an `id` and never
expect a response.

> **Migration note.** The v1 envelope used an explicit `"type"` field
> and string error codes (e.g. `"code": "illegal_action"`). v2 drops
> `type` in favor of JSON-RPC 2.0 discrimination, and codes are now
> integers. Symbolic names are preserved in `error.data.code_name`
> for human readers and logs. The major version mismatch
> (`handshake.protocol_version: "2.0"` vs an old client's `"1.0"`)
> rejects v1 clients with `protocol_error`.

JSON-RPC 2.0 batches (arrays) are not supported in v2.

---

## 4. Action encoding

Actions are addressed by a single integer `action_id` so a neural
network's policy head can index into a fixed-size vector. For a board
of size `N` (default 15), there are `N² + 5` actions.

| ID range | Meaning |
|---|---|
| `0 .. N²-1` | Placement at `(x, y)` where `x = id % N`, `y = id // N`. |
| `N² + 0` | `swap2_choose_white` — responder takes white; opener keeps black. |
| `N² + 1` | `swap2_choose_black` — responder takes black; opener becomes white. |
| `N² + 2` | `swap2_place_two` — responder places two more stones. |
| `N² + 3` | `choose_white` — opener takes white. |
| `N² + 4` | `choose_black` — opener takes black. |

Methods that take or return an action accept either form. The server
returns both forms (`action`: int, `label`: str) in
`state_changed` so observers don't need to translate.

Examples (15×15):

```text
113   → "(8,7)"          (placement at column 8, row 7)
225   → "swap2_choose_white"
228   → "choose_white"
```

---

## 5. State payload schema

Every notification that carries a `state`, `new_state`, or
`final_state` returns the same shape:

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

| Field | Type | Notes |
|---|---|---|
| `board_size` | int | Side length `N`. Currently always 15. |
| `phase` | string | Symbolic phase name, see §2.3. |
| `phase_id` | int | Numeric phase enum. Stable: 0..4. |
| `current_player` | string | `"A"` or `"B"` — whichever seat is on the clock. |
| `stone_to_place` | string | Color of the next placement (`BLACK`/`WHITE`), or `EMPTY` during the two control phases. |
| `move_count` | int | Total stones on the board (placements only — control actions don't increment). |
| `player_stones` | object | Seat → final color. `EMPTY` during opening; `BLACK`/`WHITE` once Swap2 has resolved. |
| `result` | string | `UNDETERMINED` while the game is in progress. Set on a terminal action. |
| `result_id` | int | Numeric result enum. 0=undetermined, 1=A win, 2=B win, 3=draw. |
| `moves` | int[] | Full action history (every accepted action, in order). |
| `cells` | int[] | Row-major board: `cells[y * N + x]` ∈ {0, 1, 2}. |
| `legal_actions` | int[] | Sorted list of action ids legal in the current state. |

### 5.1 How a client identifies its seat and stone

`state` never names the recipient. To know which seat you occupy:

1. Save your registered `name` after `register`.
2. When `game_started` fires, look at `params.players` —
   `{"A": <name>, "B": <name>}`. Whichever entry matches your name
   is your seat.
3. Store that mapping for the rest of the game; it doesn't change.

Once you know your seat:

- **Standard play (`phase == "STANDARD"`):** `player_stones[my_seat]`
  is your color. Your turn iff `current_player == my_seat`.
- **During the opening:** `player_stones[my_seat]` is `EMPTY`. Use
  `stone_to_place` to know what color the next placement uses, and
  `current_player` to know whose turn it is.

---

## 6. Player client guide (Python)

A complete runnable random-policy player ships at
[`examples/random_player.py`](examples/random_player.py) — useful as a
wire-level smoke test and as the canonical zero-skill Elo baseline.
The walkthrough below covers the same lifecycle in pieces.

### 6.1 Minimal example

```python
from gomoku_match import PlayerClient, connect_tcp

def on_turn(state, deadline_ms):
    # Naive: pick the first legal action.
    return state["legal_actions"][0]

client = PlayerClient(
    connect_tcp("127.0.0.1", 7901),
    name="alice",
    on_turn=on_turn,
    auth_token="...optional shared secret...",
)
client.register()  # blocks until handshake + register both succeed
# The reader thread now drives the rest of the lifecycle: every
# `your_turn` invokes `on_turn`, the result is wrapped into
# `submit_move` and sent. Stay alive however your app likes.
client.close()
```

`PlayerClient` owns two threads: a reader pumps the transport, a
dispatcher serialises event handling. `on_turn` is invoked on the
dispatcher, which means it can safely call `client.call(...)` without
deadlocking against the reader.

### 6.2 Full lifecycle

| Step | What happens | Notes |
|---|---|---|
| 1. Connect | Open transport (TCP or in-process). | The transport is independent of the server's lifecycle. |
| 2. `handshake` | First request. Sends `protocol_version: "2.0"`, optional `auth_token`, optional `client_name`. | Server replies with engine name, supported variants, and `auth_required`. Required even on loopback. |
| 3. `register` | Claims a player name. | Names are unique. Re-registering with a same name within `disconnect_grace_ms` resumes a prior seat (`reconnected: true`). |
| 4. Wait for `game_started` | Server sends this when an admin / programmatic call pairs you. | Save the seat (`A`/`B`) by matching your name against `params.players`. |
| 5. Loop on `your_turn` | Receive state, return action. | Action may be int or string label. |
| 6. `game_finished` | Final state plus `result` and `reason`. | If `ratings` is present, your post-rating is authoritative. |
| 7. Close | Shut the transport down. | Optional — leaving it open lets you play another match without re-registering. |

### 6.3 Writing a real `on_turn`

The default callback (used if you pass `on_turn=None`) picks the
first legal action and is correct only as a smoke test. A real engine
needs to:

1. Detect the phase. The action you can return depends on it:
   - `PLACE_INITIAL_THREE` / `SWAP2_PLACE_TWO` / `STANDARD` →
     placement (an int in `[0, N²)` or a `"(x,y)"` label).
   - `SWAP2_DECISION` → one of `swap2_choose_white`,
     `swap2_choose_black`, `swap2_place_two`.
   - `CHOOSE_COLOR` → `choose_white` or `choose_black`.
2. Restrict your search to `state["legal_actions"]`. Returning an
   action outside that list raises `illegal_action` on the server
   (and may forfeit the match under strict policies).
3. Watch the clock. `deadline_ms` is the **remaining** ms before
   forfeit, not the configured per-move budget. A pondering search
   that doesn't respect this will lose on time.
4. Identify your stone color (see §5.1) so heuristics like "do I
   have an open four?" can ask the right question.

### 6.4 Error handling

Every `client.call(...)` raises `PlayerClientError` on a non-2xx
response or a transport failure. The exception message includes the
symbolic error code (`"submit_move returned illegal_action: ..."`).
The reader thread also surfaces server-pushed errors by failing any
in-flight pending requests.

A failing `on_turn` (any exception) is caught and turned into an
automatic `resign` — the alternative is to silently let the deadline
forfeit, which is harder to debug.

### 6.5 Reconnect

If the server is started with `disconnect_grace_ms > 0` for a match
and your connection drops mid-game, you can re-create a transport,
construct a fresh `PlayerClient` with the same `name`, and call
`register()`. The server matches the name to the existing seat,
returns `reconnected: true`, and re-emits `your_turn` if you're on
the clock. The deadline is reset to the configured per-move budget
from "now" — no penalty for the disconnect itself.

---

## 7. Spectator client guide (Python)

A complete runnable spectator that pretty-prints each board to the
terminal as moves arrive ships at
[`examples/spectator.py`](examples/spectator.py). The walkthrough
below covers the underlying API.

### 7.1 Minimal example

```python
from gomoku_match import ObserverClient, connect_tcp

obs = ObserverClient(connect_tcp("127.0.0.1", 7901))
obs.handshake()
obs.subscribe()                     # subscribe to all games
# Or: obs.subscribe(game_id="g1")   # one specific game
state = obs.query_state("g1")       # snapshot of one game
# Stream events:
ev = obs.wait_for_event("state_changed", timeout=10.0)
print(ev.params["new_state"])
obs.close()
```

### 7.2 Lifecycle

1. Connect + `handshake` (same as a player; provide `auth_token` if
   the server requires one).
2. `subscribe` once per scope. `subscribe()` with no `game_id`
   subscribes to all current and future games. `subscribe(game_id=...)`
   restricts to a single game.
3. Receive notifications. `ObserverClient` accumulates every received
   `Event` into a thread-safe deque (`obs.events()`) and signals
   `wait_for_event` consumers.
4. Late-join: call `query_state(game_id)` to get the full current
   state, then layer subsequent `state_changed` events on top.
5. Close.

### 7.3 What you receive

Every observer subscribed to a game receives `game_started`,
`state_changed` (per move), and `game_finished`. Observers do **not**
receive `your_turn` — that's player-only.

If your observer disconnects mid-game and reconnects, you'll miss
events that fired in between. To recover, call `query_state` for the
game(s) you care about and re-subscribe. Alternatively, query the
SQLite store directly (§11).

### 7.4 Reconstructing state

The state payload's `moves` field is a complete action history. Any
client can replay it through a local `Board` to reproduce the
canonical state — useful for debug overlays, post-game analysis, or
sanity checks against the server.

```python
from gomoku_match import Board, BoardConfig

state = obs.query_state("g1")["state"]
board = Board(BoardConfig(size=state["board_size"]))
for action_id in state["moves"]:
    board.apply(action_id)
assert board.cells == state["cells"]   # canonical match
```

---

## 8. Method reference

All methods take a JSON object as `params` and return a JSON object as
`result`. Errors are listed by their symbolic name; see §10 for the
integer code mapping.

### 8.1 `handshake`

| | |
|---|---|
| **Roles** | Any |
| **When** | Always the first request on a fresh connection. |
| **Params** | `{ "client_name"?: string, "protocol_version": "2.0", "auth_token"?: string, "admin_token"?: string }` |
| **Result** | `{ "engine_name", "protocol_version", "supported_rules", "supported_board_sizes", "action_count", "capabilities": [...], "auth_required": bool }` |
| **Errors** | `protocol_error` (version mismatch), `auth_failed` (wrong token; the server then closes the socket) |

The `protocol_version` field uses major.minor strings. The server
rejects clients whose major version doesn't match its own. Minor
version differences are tolerated — newer minors add optional
capabilities, older clients ignore them.

`capabilities` is an array of feature flags the server supports, e.g.
`"in_process_transport"`, `"tcp_transport"`, `"auth_token"`,
`"disconnect_grace"`.

### 8.2 `register` (player)

| | |
|---|---|
| **Roles** | Player |
| **When** | After `handshake`, before any game-related method. |
| **Params** | `{ "name": string }` |
| **Result** | `{ "player_id": string, "name": string, "reconnected": bool }` |
| **Errors** | `bad_request` (name missing or already registered live) |

Names are unique across the server. If a previous connection with
the same name has dropped and at least one of its games has a
non-zero `disconnect_grace_ms`, registering with that name within the
grace window resumes the prior seat (`reconnected: true`) and the
server re-emits `your_turn` if you're on the clock.

### 8.3 `subscribe` (observer)

| | |
|---|---|
| **Roles** | Observer |
| **When** | After `handshake`. |
| **Params** | `{ "game_id"?: string }` (omit for all games) |
| **Result** | `{ "subscribed": "all" | game_id }` |
| **Errors** | `unknown_game` |

### 8.4 `create_match` (admin)

| | |
|---|---|
| **Roles** | Admin |
| **When** | After `handshake`, with `admin_token` presented. Both players already registered. |
| **Params** | `{ "player_a": name, "player_b": name, "board_size"?: 15, "deadline_ms_per_move"?: 5000, "disconnect_grace_ms"?: 0 }` |
| **Result** | `{ "game_id": string }` |
| **Errors** | `auth_failed`, `unknown_player`, `bad_request` |

`disconnect_grace_ms` is the per-game grace window for the
reconnect-by-name flow described in §6.5. With `0` (default), a
disconnect immediately forfeits.

### 8.5 `submit_move` (player)

| | |
|---|---|
| **Roles** | Player on the clock |
| **When** | In response to `your_turn`. Sending earlier or later is rejected. |
| **Params** | `{ "game_id": string, "action": int|label }` |
| **Result** | `{ "ok": true, "terminal": bool }` |
| **Errors** | `not_your_turn`, `illegal_action`, `terminal_position`, `unknown_game`, `bad_request` |

`action` may be the integer id or the human-readable label. The
server validates against `Board.legal_actions()` and applies on
success.

### 8.6 `resign` (player)

| | |
|---|---|
| **Roles** | Player |
| **When** | Any time during an unfinished game in which you're a participant. |
| **Params** | `{ "game_id": string }` |
| **Result** | `{ "ok": true }` |
| **Errors** | `unknown_player`, `unknown_game`, `terminal_position`, `not_your_turn` (if you're not in the game at all) |

The opposite seat wins. `game_finished` is broadcast with
`reason: "resignation"`.

### 8.7 `query_state`

| | |
|---|---|
| **Roles** | Any |
| **When** | Any time after `handshake`. |
| **Params** | `{ "game_id": string }` |
| **Result** | `{ "game_id", "state", "finished", "result", "result_reason", "players": {"A","B"} }` |
| **Errors** | `unknown_game` |

### 8.8 `query_history`

| | |
|---|---|
| **Roles** | Admin |
| **When** | Any time after `handshake`. |
| **Params** | `{}` |
| **Result** | `{ "games": [{"game_id", "finished", "result", "result_reason", "moves": [int]}, ...] }` |
| **Errors** | `auth_failed` (non-admin) |

For non-admin observers wanting historical data, use `query_state`
per game id, or read directly from the SQLite store (§11).

---

## 9. Notification reference

Server-pushed messages with no `id`. Clients subscribe to them via
their role (no explicit subscription is needed for player events;
observers must `subscribe`).

### 9.1 `game_started`

```json
{"jsonrpc":"2.0","method":"game_started","params":{
  "game_id": "g1",
  "players": {"A": "alice", "B": "bob"},
  "settings": {"board_size":15,"deadline_ms_per_move":5000},
  "state": { ... initial board state ... }
}}
```

### 9.2 `your_turn`

```json
{"jsonrpc":"2.0","method":"your_turn","params":{
  "game_id": "g1",
  "state": { ... full board state ... },
  "deadline_ms": 5000
}}
```

`deadline_ms` is the time remaining before forfeit, not the
configured budget. On reconnect-by-name the deadline is reset to the
full budget from "now".

### 9.3 `state_changed`

```json
{"jsonrpc":"2.0","method":"state_changed","params":{
  "game_id": "g1",
  "action": 113,
  "label": "(8,7)",
  "by_player": "alice",
  "new_state": { ... updated state ... }
}}
```

### 9.4 `game_finished`

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

`ratings` is present only if the server has an Elo engine attached
(`--elo` on CLI, `MatchServer(elo=...)` programmatically). The K
schedule is `40 → 20 → 10` at `30 / 100` games-played thresholds for
each player; draws score 0.5/0.5.

---

## 10. Error code reference

Every error response carries:

```json
{"code": <int>, "message": "<human-readable>", "data": {"code_name": "<symbolic>", ...}}
```

`error.data` is always present and always carries `code_name`. Other
fields in `data` are method-specific context (e.g. `on_clock_player_id`
for `not_your_turn`).

| Code | Symbolic name | Meaning |
|---:|---|---|
| `-32600` | `protocol_error` | Message could not be parsed or violated the JSON-RPC envelope. |
| `-32601` | `unknown_method` | The server does not implement that method. |
| `-32602` | `bad_request` | Method-specific argument validation failed. |
| `-32603` | `engine_error` | Internal server error; bug. Always treated as a server-side incident. |
| `1001` | `illegal_action` | The action is not legal in the current phase or position. |
| `1002` | `not_your_turn` | A player attempted to move when not on the clock. |
| `1003` | `unknown_game` | `game_id` does not exist. |
| `1004` | `unknown_player` | `player_id` / name does not exist or is not registered. |
| `1005` | `terminal_position` | The game has already finished. |
| `1006` | `cancelled` | The request was cancelled by stop or shutdown. |
| `1007` | `auth_failed` | Shared-secret authentication did not match. After this error the server closes the socket. |
| `1008` | `busy` | Reserved for future use (concurrency limits). |

Negative codes are JSON-RPC 2.0 reserved (-32768 .. -32000) and
emitted with the meanings the spec prescribes. Application errors
use positive integers outside that range so they don't collide with
future JSON-RPC additions.

---

## 11. Authentication

The server supports two independent shared secrets, both optional:

- **`auth_token`** — required of every connection if set. A missing
  or wrong token returns `auth_failed` and the server closes the
  socket. Loopback servers may run without auth; production / shared
  hosting should always run with one.
- **`admin_token`** — flags the connection as admin if present and
  matching. Required for `create_match` and `query_history` over the
  wire.

Configure on the CLI:

```bash
GOMOKU_TOKEN=$(python -c "import secrets; print(secrets.token_urlsafe(24))")
GOMOKU_ADMIN=$(python -c "import secrets; print(secrets.token_urlsafe(24))")
GOMOKU_TOKEN=$GOMOKU_TOKEN GOMOKU_ADMIN=$GOMOKU_ADMIN python -m gomoku_match \
    --listen tcp://0.0.0.0:7901 \
    --auth-token-env GOMOKU_TOKEN
# (admin token is read from --admin-token-env in newer builds; check
# `python -m gomoku_match --help`.)
```

Tokens are compared with `hmac.compare_digest` to avoid timing
side channels.

---

## 12. Persistence

The server may journal every match to a SQLite database (one row per
match plus one row per move). With `--store ./matches.sqlite` and
`--elo`, the same store also carries:

- `ratings` — current rating per registered player.
- `match_ratings` — one row per finished match with `pre_rating_*`,
  `post_rating_*`, and the K used.

Tools wishing to persist Elo or run post-game analysis should read
from the same store rather than relying on observer events alone —
events can be missed by clients that disconnect mid-game.

The schema is a stable contract. Direct read access (e.g. for an
analytics dashboard) is supported; direct write access from outside
the server is not.

---

## 13. Versioning and capability negotiation

- Major version: a breaking change to the wire envelope or method
  surface. Major mismatch is rejected by `handshake`. v1 (the legacy
  `"type"`-discriminated envelope) was retired in v2.
- Minor version: additive — new methods, new capabilities. Older
  clients continue to work; they simply don't see the new features.
- `capabilities` in the handshake result is a string array of
  optional features the server implements. Clients can use it to
  decide whether to enable feature-flagged behavior. Examples:
  `"in_process_transport"`, `"tcp_transport"`, `"auth_token"`,
  `"disconnect_grace"`.

When in doubt, send `protocol_version: "2.0"` and treat unknown
capabilities as absent.

---

## 14. Pointers

- Wire format reference (terse): [`docs/protocol_v2.md`](docs/protocol_v2.md)
- Source of truth for rules: `python/gomoku_match/board.py`
- Client libraries: `python/gomoku_match/player_client.py`,
  `python/gomoku_match/observer_client.py`
- Server: `python/gomoku_match/server.py`
- CLI: `python -m gomoku_match`
