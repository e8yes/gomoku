# gomoku_match — Project-neutral Gomoku Swap2 matchmaking

`gomoku_match` is a standalone match server and protocol library for
Gomoku Swap2. It owns the canonical game state, validates every
submitted move against its own pure-Python rules engine, dispatches
turn notifications to whichever player is on the clock, broadcasts
state changes to observers, and maintains a centralised Elo table.

It is intentionally **project-neutral**: it has no dependency on any
particular Gomoku engine. The companion `gomoku_az/` package in this
repository is one consumer; any engine implementing the documented
player protocol can register and play.

## Status

- v0.1 — Phase 10 Workstream A: in-process transport, server core,
  player and observer clients, pure-Python Swap2 board, full Swap2
  round-trip test, SQLite match journal.
- v0.2 — Phase 10 Workstream B: TCP transport, optional shared-secret
  auth, `python -m gomoku_match` CLI, per-game disconnect-grace window
  with reconnect-by-name. WebSocket deferred (the JSON Lines wire
  format is identical, so it will land as a transport-only adapter).
- v0.3 — Phase 10 Workstream C, Step 1: centralised Elo engine.
  `EloEngine` maintains a `ratings` leaderboard and a per-match
  `match_ratings` audit table inside the same SQLite store. Optional on
  the server (`--elo`); `game_finished` events surface `pre`/`post`
  rating deltas. Steps 2 (Gomocup adapter) and 3 (`gomoku_az`
  campaign-runner integration) follow as separate commits.
- v0.4 — Wire format converted to strict JSON-RPC 2.0; the legacy
  `"type"`-discriminated envelope and string error codes are retired.
  `handshake.protocol_version` bumped from `"1.0"` to `"2.0"`. Rules
  and method surface are unchanged.

See [`spec.md`](spec.md) for the developer guide (rules, client
walkthroughs, method reference) and
[`docs/protocol_v2.md`](docs/protocol_v2.md) for the terse wire-format
reference. The protocol is strict
[JSON-RPC 2.0](https://www.jsonrpc.org/specification) over JSON Lines.

## Layout

```
gomoku_match/
  python/gomoku_match/
    board.py            # pure-Python Swap2 rules
    protocol.py         # Request/Response/Event types + JSON Lines codec
    transports.py       # Transport ABC + InProcessTransport
    server.py           # MatchServer (referee, clock, observer broadcast)
    player_client.py    # client library for engines
    observer_client.py  # client library for spectators / loggers
    persistence.py      # SQLite match/move journal
    elo.py              # centralised Elo engine + audit tables
    tcp_transport.py    # TCP transport + listener
    adapters/           # external-engine adapters (Gomocup, ...)
    __main__.py         # `python -m gomoku_match` CLI
  tests/python/         # pytest + unittest suite
  spec.md               # developer guide (rules + client walkthroughs)
  docs/
    protocol_v2.md      # terse wire-format reference (JSON-RPC 2.0)
```

## Quick start (in-process)

```python
from gomoku_match.server import MatchServer
from gomoku_match.transports import InProcessTransport
from gomoku_match.player_client import PlayerClient

server = MatchServer()
trans_a, server_a = InProcessTransport.pair()
trans_b, server_b = InProcessTransport.pair()
server.attach(server_a)
server.attach(server_b)

def pick_move(state, deadline_ms, sims_budget):
    return state["legal_actions"][0]

PlayerClient(trans_a, name="alice", on_turn=pick_move).register()
PlayerClient(trans_b, name="bob",   on_turn=pick_move).register()
# server.create_match(...) drives the game.
```

## Quick start (over TCP)

```bash
# Server
GOMOKU_TOKEN=$(python -c "import secrets; print(secrets.token_urlsafe(24))")
GOMOKU_TOKEN=$GOMOKU_TOKEN python -m gomoku_match \
    --listen tcp://0.0.0.0:7901 \
    --auth-token-env GOMOKU_TOKEN \
    --store ./matches.sqlite
```

```python
# Client
from gomoku_match import PlayerClient, connect_tcp

client = PlayerClient(
    connect_tcp("server.example.com", 7901),
    name="alice",
    on_turn=lambda s, _: s["legal_actions"][0],
    auth_token="...same token...",
)
client.register()
```

## Why a separate package

We want the protocol — and any independent engines that adopt it — to
live unencumbered by the AlphaZero stack. Swapping out a board model,
a rules variant, or a transport should not ripple through a deep
research codebase.

## Native C++ client

The engine tree now contains a native Phase 8 player, `gomoku_match_client`.
It implements the same lifecycle as `PlayerClient`: handshake, register,
optional admin-created pairing, Swap2-aware `your_turn` handling, move
submission, and `game_finished`. It reconstructs the server's canonical board
from the complete action history before each search, so the server remains the
sole referee.

The client uses the engine's persistent MCTS tree and VCF attacker/defender
callbacks. Pass `--model PATH` for an AOTInductor `.pt2` model; omit it to use
`RandomEvaluator` for a low-cost network smoke test. Root noise is opt-in via
`--noise-plies N` and is disabled by default for match play.

Example with the Python server:

```bash
python -m gomoku_match --listen tcp://127.0.0.1:7901 --admin-token phase8
gomoku_match_client --name bob --model exported_models/champion36.pt2
gomoku_match_client --name alice --opponent bob --admin-token phase8 \
  --model exported_models/champion36.pt2
PYTHONPATH=python python examples/spectator.py --host 127.0.0.1 --port 7901
```

The C++ client is portable across the supported native builds: CMake links
Winsock (`ws2_32`) on Windows and uses BSD sockets on Linux. The wire codec is
kept in `engine/src/match_json.*` and has unit coverage in
`engine/tests/match_json_test.cpp`.
