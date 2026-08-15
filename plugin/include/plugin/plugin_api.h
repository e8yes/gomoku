#ifndef GOMOKU_PLUGIN_API_H
#define GOMOKU_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef GOMOKU_PLUGIN_EXPORTS
#define GOMOKU_PLUGIN_API __declspec(dllexport)
#else
#define GOMOKU_PLUGIN_API __declspec(dllimport)
#endif
#else
#define GOMOKU_PLUGIN_API __attribute__((visibility("default")))
#endif

#define GOMOKU_PLUGIN_API_VERSION_MAJOR 1
#define GOMOKU_PLUGIN_API_VERSION_MINOR 0

#ifdef __cplusplus
extern "C" {
#endif

typedef void* GomokuPlayerHandle;

typedef struct {
  uint32_t api_version_major;
  uint32_t api_version_minor;
  const char* plugin_name;
  const char* plugin_version;
  const char* author;
  const char* description;
  uint32_t capabilities_flags;
} GomokuPluginInfo;

typedef enum {
  GOMOKU_DIFF_APPRENTICE = 0,
  GOMOKU_DIFF_CASUAL = 1,
  GOMOKU_DIFF_CLUB = 2,
  GOMOKU_DIFF_VETERAN = 3,
  GOMOKU_DIFF_CHAMPION = 4,
  GOMOKU_DIFF_TRUTH = 5
} GomokuDifficulty;

typedef struct {
  int seat;  // 0 = Seat A, 1 = Seat B
  GomokuDifficulty difficulty;
  const char* opponent_name;
  const char* custom_config_json;
} GomokuMatchSettings;

typedef struct {
  int result;  // 0 = undetermined, 1 = player A win, 2 = player B win, 3 = draw
  int winner_seat;  // 0 = Seat A, 1 = Seat B
  const char* termination_reason;
} GomokuMatchResult;

// Query plugin metadata
GOMOKU_PLUGIN_API GomokuPluginInfo gomoku_plugin_get_info(void);

// Create and destroy engine player instances
GOMOKU_PLUGIN_API GomokuPlayerHandle
gomoku_player_create(const char* config_json);
GOMOKU_PLUGIN_API void gomoku_player_destroy(GomokuPlayerHandle handle);

// Match lifecycle: match start
GOMOKU_PLUGIN_API void gomoku_player_on_match_start(
    GomokuPlayerHandle handle, const GomokuMatchSettings* settings);

// Inquire next optimal action given current board state
// board_cells: pointer to 225 uint8_t array (0=Empty, 1=Black, 2=White)
// current_seat: 0=Seat A, 1=Seat B
// stone_to_place: 0=Empty, 1=Black, 2=White
// phase: 0=PlaceInitialThree, 1=Swap2Decision, 2=Swap2PlaceTwo, 3=ChooseColor,
// 4=Standard Returns chosen action ID (0-224 placement, 225-229 Swap2 choice)
GOMOKU_PLUGIN_API int gomoku_player_inquire_action(GomokuPlayerHandle handle,
                                                   const uint8_t* board_cells,
                                                   int current_seat,
                                                   int stone_to_place,
                                                   int phase);

// Informs engine player of the action actually applied to the board
GOMOKU_PLUGIN_API void gomoku_player_apply_action(GomokuPlayerHandle handle,
                                                  int action_id);

// Match lifecycle: match end
GOMOKU_PLUGIN_API void gomoku_player_on_match_end(
    GomokuPlayerHandle handle, const GomokuMatchResult* result);

// Optional live telemetry: estimated win rate ([-1.0, 1.0] or [0.0, 1.0])
// Safe to call concurrently while gomoku_player_inquire_action is in-flight.
// Returns 1 if supported and populated, 0 otherwise.
GOMOKU_PLUGIN_API int gomoku_player_get_win_rate(GomokuPlayerHandle handle,
                                                 float* out_win_rate);

// Optional live telemetry: policy distribution over all actions (typically
// 230). Safe to call concurrently while gomoku_player_inquire_action is
// in-flight. Returns number of action probabilities written, or 0 if not
// supported.
GOMOKU_PLUGIN_API int gomoku_player_get_policy(GomokuPlayerHandle handle,
                                               float* out_policy,
                                               int max_actions);

// Cancels / aborts an active in-flight action inquiry
GOMOKU_PLUGIN_API void gomoku_player_cancel_inquiry(GomokuPlayerHandle handle);

#ifdef __cplusplus
}
#endif

#endif  // GOMOKU_PLUGIN_API_H
