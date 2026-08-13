# Gomoku Engine

## Game visualizer

Render a numbered move trace in the style of a 15x15 Gomoku board:

```powershell
cd python
python visualize_game.py `
  --moves-file examples/sample_game.json `
  --output ../artifacts/sample_game.png `
  --title "sample game"
```

Move traces can be JSON arrays, `{"moves": [...]}`, or one coordinate per
line. Coordinates use the engine convention `(x,y)`, with `(0,0)` at the
upper-left. To render a packed training position from a curriculum shard:

```powershell
python visualize_game.py `
  --record ../artifacts/iteration_000.bin `
  --index 0 `
  --output ../artifacts/training_position.png
```

Training shards contain positions rather than move history, so those images
do not include move numbers. Use a move trace for the numbered presentation.

## Match-server client

Phase 8 adds the native `gomoku_match_client` executable. It speaks the
project's JSON-RPC 2.0 JSON-Lines protocol directly and can run either the
current AOTInductor model or a `RandomEvaluator` for protocol smoke tests.

Build it with the normal engine build, then start the Python match server with
an admin token so one client can pair two registered players:

```bash
# Server, from protocol/python
python -m gomoku_match --listen tcp://127.0.0.1:7901 --admin-token phase8

# Player B
./gomoku_match_client --name bob --model exported_models/champion36.pt2

# Player A / match creator
./gomoku_match_client --name alice --opponent bob --seat A \
  --admin-token phase8 --model exported_models/champion36.pt2

# Optional wire smoke test: omit --model and use --simulations 8.
```

The server's spectator can observe the match independently:

```bash
PYTHONPATH=protocol/python python protocol/examples/spectator.py \
  --host 127.0.0.1 --port 7901
```

Use `--auth-token` when the server is also configured with an authentication
token. `--noise-plies N` enables optional root noise for the first `N` plies;
the default is deterministic match play. `--seat A|B` selects which side of
an admin-created pairing the client occupies. `--deadline-ms N` sets the clock
used when creating an admin-paired match (5,000 ms by default). MCTS uses the
remaining deadline by default, stopping between batches and reserving 250 ms
for move submission. Passing `--simulations N` explicitly selects a fixed
simulation-count search instead. Windows builds link Winsock automatically;
Linux builds use the native socket API.
