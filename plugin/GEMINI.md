# Gomoku Engine Plugin & Player Interface SDK (`plugin/`)

This directory contains the standalone Gomoku Engine Plugin SDK, Dynamic Plugin Loader, Unified Player Interface, and Match Coordinator designed for the Gomoku QtQuick application.

The module is completely self-contained and operates with **zero dependencies on internal engine code**, allowing third-party engine developers and UI components to compile against clean, standardized C/C++ interfaces.

---

## 1. Architecture Overview

```
+------------------------------------------------------------------------------------+
|                               Gomoku QtQuick App                                   |
|                                                                                    |
|  +---------------------+   +----------------------------------------------------+  |
|  |     HumanPlayer     |   |                EnginePluginLoader                  |  |
|  | (GUI / QML Bridge)  |   |   (Discovers, loads & manages dynamic .so / .dll)  |  |
|  +----------+----------+   +-------------------------+--------------------------+  |
|             |                                        |                             |
|             +-------------------+                    | Instantiates                |
|                                 |                    v                             |
|                                 |         +----------------------+                 |
|                                 |         |  PluginPlayerAdapter |                 |
|                                 |         +----------+-----------+                 |
|                                 v                    v                             |
|                     +-------------------------------------+                        |
|                     |          IPlayer Interface          |                        |
|                     +-------------------------------------+                        |
|                     | + OnMatchStart(MatchSettings)       |                        |
|                     | + InquireAction(BoardState)         |                        |
|                     | + ApplyAction(action_id)            |                        |
|                     | + OnMatchEnd(MatchResultInfo)       |                        |
|                     | + GetEstimatedWinRate() (concurrent)|                        |
|                     | + GetPolicy()           (concurrent)|                        |
|                     +-------------------------------------+                        |
|                                        ^                                           |
|                                        | Coordinates                               |
|                          +-------------+------------+                              |
|                          |     MatchCoordinator     |                              |
|                          | (PvP, PvE, EvE Matches)  |                              |
|                          +--------------------------+                              |
+----------------------------------------+-------------------------------------------+
                                         | Pure C ABI (Dynamic Boundary)
                                         v
+------------------------------------------------------------------------------------+
|                         Gomoku Engine Plugin (.so / .dll)                          |
|                                                                                    |
|  +------------------------------------------------------------------------------+  |
|  | C-ABI Export Table:                                                          |  |
|  |  - gomoku_plugin_get_info()                                                  |  |
|  |  - gomoku_player_create(), gomoku_player_destroy()                           |  |
|  |  - gomoku_player_on_match_start(settings)                                    |  |
|  |  - gomoku_player_inquire_action(board_state)                                 |  |
|  |  - gomoku_player_apply_action(action_id)                                     |  |
|  |  - gomoku_player_on_match_end(result)                                        |  |
|  |  - gomoku_player_get_win_rate(), gomoku_player_get_policy()                   |  |
|  |  - gomoku_player_cancel_inquiry()                                            |  |
|  +------------------------------------------------------------------------------+  |
+------------------------------------------------------------------------------------+
```

---

## 2. Six Difficulty Levels

When starting a match, the `Difficulty` parameter is passed to `OnMatchStart`:

| Level | Enum | Description |
| :--- | :--- | :--- |
| **Apprentice** | `Difficulty::kApprentice` (`0`) | Novice strength: fast, high exploration, blunders possible |
| **Casual** | `Difficulty::kCasual` (`1`) | Casual strength: light tactical search, basic positional awareness |
| **Club** | `Difficulty::kClub` (`2`) | Intermediate strength: defensive tactical awareness, denies open threats |
| **Veteran** | `Difficulty::kVeteran` (`3`) | Strong amateur strength: multi-ply search, attacks and blocks forced lines |
| **Champion** | `Difficulty::kChampion` (`4`) | Master strength: deep search budget, deterministic optimal play |
| **Truth** | `Difficulty::kTruth` (`5`) | Maximum analytical strength: deepest search depth & full proof search |

Helper functions:
- `const char* DifficultyToString(Difficulty diff)`
- `std::optional<Difficulty> DifficultyFromString(const std::string& str)`

---

## 3. Core Components

### 3.1 Unified Player Interface (`IPlayer`)
Defined in [`include/plugin/player_interface.h`](file:///home/davis/gomoku/plugin/include/plugin/player_interface.h):
- `OnMatchStart(const MatchSettings& settings)`: Informs the player/engine that a match is starting with seat assignment and difficulty level.
- `InquireAction(const BoardState& board)`: Asks the player for its next move given the board state.
- `ApplyAction(int action_id)`: Informs the player what action was actually played (own move, opponent move, or referee placement), enabling MCTS search tree persistence.
- `OnMatchEnd(const MatchResultInfo& result_info)`: Informs the player of match conclusion.
- `GetEstimatedWinRate()`: Optional live win-rate query in `[-1.0, 1.0]` or `[0.0, 1.0]` (safe to call concurrently while `InquireAction` is in-flight).
- `GetPolicy()`: Optional live probability distribution across all 230 actions (safe to call concurrently while `InquireAction` is in-flight).
- `CancelInquiry()`: Immediately cancels an in-flight search or unblocks human input.

### 3.2 Human Player Bridge (`HumanPlayer`)
Defined in [`include/plugin/human_player.h`](file:///home/davis/gomoku/plugin/include/plugin/human_player.h):
- Implements `IPlayer` for QtQuick UI integration.
- `InquireAction(board)` waits on a thread-safe synchronization condition variable until user input is received.
- QtQuick / QML mouse click handlers call `SubmitAction(action_id)` to fulfill the move.
- `IsWaitingForAction()` indicates whether the UI should highlight legal moves or display a prompt.

### 3.3 Dynamic Plugin Loader (`EnginePluginLoader` & `LoadedPlugin`)
Defined in [`include/plugin/plugin_loader.h`](file:///home/davis/gomoku/plugin/include/plugin/plugin_loader.h):
- `EnginePluginLoader::LoadPlugin(path)`: Dynamically loads `.so` (Linux), `.dylib` (macOS), or `.dll` (Windows) plugins via standard C ABI.
- `EnginePluginLoader::DiscoverPlugins(directory)`: Automatically discovers all compatible engine plugins in a folder.
- `LoadedPlugin::CreatePlayer(config_json)`: Returns a managed `std::unique_ptr<IPlayer>` adapter.

### 3.4 Pure C ABI (`plugin_api.h`)
Defined in [`include/plugin/plugin_api.h`](file:///home/davis/gomoku/plugin/include/plugin/plugin_api.h):
- Standalone C header (`extern "C"`) enabling engine plugins to be built in C, C++, Rust, etc.
- Exported functions:
  - `gomoku_plugin_get_info()`
  - `gomoku_player_create(config_json)`
  - `gomoku_player_destroy(handle)`
  - `gomoku_player_on_match_start(handle, settings)`
  - `gomoku_player_inquire_action(handle, board_cells, current_seat, stone_to_place, phase)`
  - `gomoku_player_apply_action(handle, action_id)`
  - `gomoku_player_on_match_end(handle, result)`
  - `gomoku_player_get_win_rate(handle, out_win_rate)`
  - `gomoku_player_get_policy(handle, out_policy, max_actions)`
  - `gomoku_player_cancel_inquiry(handle)`

### 3.5 Match Coordinator (`MatchCoordinator`)
Defined in [`include/plugin/match_coordinator.h`](file:///home/davis/gomoku/plugin/include/plugin/match_coordinator.h):
- Manages complete match loops for:
  - **Player vs Player (PvP)**: Two `HumanPlayer` instances.
  - **Player vs Engine (PvE)**: Human vs Engine plugin.
  - **Engine vs Engine (EvE)**: Two Engine plugins.
- Dispatches `InquireAction`, validates legality, applies actions to `BoardState`, and notifies both players via `ApplyAction`.
- Supports step callbacks for live UI board updates, win-rate graphs, and policy heatmap rendering.

---

## 4. Directory Layout

```
plugin/
├── CMakeLists.txt                    # Standalone CMake configuration
├── GEMINI.md                         # This documentation
├── include/
│   └── plugin/
│       ├── difficulty.h              # 6 Difficulty levels & conversions
│       ├── gomoku_types.h            # Self-contained BoardState, Stone, Seat, Action
│       ├── human_player.h            # Human player IPlayer implementation for QtQuick
│       ├── match_coordinator.h       # PvP, PvE, EvE match orchestrator
│       ├── player_interface.h        # Unified IPlayer abstract base interface
│       ├── plugin_api.h              # Pure C-ABI DLL export specifications
│       └── plugin_loader.h           # Dynamic plugin loader & manager
├── sample_plugin/
│   └── sample_engine_plugin.cpp      # Reference DLL engine plugin implementation
├── src/
│   ├── difficulty.cpp
│   ├── gomoku_types.cpp
│   ├── human_player.cpp
│   ├── match_coordinator.cpp
│   └── plugin_loader.cpp
└── tests/
    ├── difficulty_test.cpp           # Tests difficulty conversions
    ├── gomoku_types_test.cpp         # Tests board rules, Swap2, exact-five win checks
    ├── human_player_test.cpp         # Tests human GUI move submission & cancellation
    ├── match_coordinator_test.cpp    # Tests PvP, PvE, EvE match execution
    └── plugin_loader_test.cpp        # Tests dynamic dlopen loading & live telemetry
```

---

## 5. Building & Running Tests

To build the plugin SDK, sample engine shared library, and test suite:

```bash
mkdir -p build_plugin && cd build_plugin
cmake ../plugin -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Run the unit tests:

```bash
./gomoku_plugin_test
```
