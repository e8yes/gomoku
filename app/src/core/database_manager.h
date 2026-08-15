#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace gomoku::app {

struct MatchRecord {
  int64_t match_id = 0;
  std::string created_at;
  std::string player_a_name;
  std::string player_a_type;  // "human" or plugin name
  std::string player_b_name;
  std::string player_b_type;  // "human" or plugin name
  int difficulty_a = 3;
  int difficulty_b = 3;
  int board_size = 15;
  std::string result;  // "PLAYER_A_WIN", "PLAYER_B_WIN", "DRAW", "UNDETERMINED"
  std::string winner_seat;        // "A", "B", or ""
  std::string termination_reason; // "five_in_a_row", "illegal_action", "resignation", "draw", etc.
  int total_plies = 0;
  int opening_path = 0;
};

struct MoveRecord {
  int64_t move_id = 0;
  int64_t match_id = 0;
  int ply_index = 0;  // 1-based ply index
  int action_id = 0;
  std::string action_label;
  int x_coord = -1;
  int y_coord = -1;
  std::string seat;          // "A" or "B"
  std::string stone_placed;  // "BLACK", "WHITE", "NONE"
  std::string phase_at_move;
  std::optional<float> estimated_win_rate;  // Win rate recorded right after inquiry
  int time_spent_ms = 0;
  std::string timestamp;
};

class DatabaseManager {
 public:
  static DatabaseManager& Instance();

  explicit DatabaseManager(const std::string& db_path = "");
  ~DatabaseManager();

  DatabaseManager(const DatabaseManager&) = delete;
  DatabaseManager& operator=(const DatabaseManager&) = delete;

  // Opens SQLite database at specified path. If empty, uses default application data directory.
  bool Open(const std::string& db_path = "");
  void Close();
  bool IsOpen() const { return db_ != nullptr; }

  // Match lifecycle methods
  int64_t CreateMatch(const std::string& player_a_name,
                      const std::string& player_a_type,
                      const std::string& player_b_name,
                      const std::string& player_b_type, int difficulty_a,
                      int difficulty_b, int board_size = 15);

  bool RecordMove(int64_t match_id, int ply_index, int action_id,
                  const std::string& action_label, int x_coord, int y_coord,
                  const std::string& seat, const std::string& stone_placed,
                  const std::string& phase_at_move,
                  std::optional<float> estimated_win_rate,
                  int time_spent_ms = 0);

  bool FinishMatch(int64_t match_id, const std::string& result,
                   const std::string& winner_seat,
                   const std::string& termination_reason, int total_plies,
                   int opening_path = 0);

  // Queries
  std::vector<MatchRecord> GetRecentMatches(int limit = 100, int offset = 0);
  std::optional<MatchRecord> GetMatchById(int64_t match_id);
  std::vector<MoveRecord> GetMovesForMatch(int64_t match_id);
  bool DeleteMatch(int64_t match_id);

 private:
  bool InitializeSchema();

  sqlite3* db_{nullptr};
  mutable std::mutex db_mutex_;
  std::string db_path_;
};

}  // namespace gomoku::app
