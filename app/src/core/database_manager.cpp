#include "core/database_manager.h"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>

namespace gomoku::app {

namespace {

std::string GetDefaultDatabasePath() {
  std::filesystem::path app_dir =
      std::filesystem::current_path() / "gomoku_app.sqlite";
  return app_dir.string();
}

}  // namespace

DatabaseManager& DatabaseManager::Instance() {
  static DatabaseManager instance;
  return instance;
}

DatabaseManager::DatabaseManager(const std::string& db_path) {
  if (!db_path.empty()) {
    Open(db_path);
  }
}

DatabaseManager::~DatabaseManager() { Close(); }

bool DatabaseManager::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  Close();

  db_path_ = db_path.empty() ? GetDefaultDatabasePath() : db_path;

  // Ensure directory exists if not in-memory
  if (db_path_ != ":memory:") {
    std::filesystem::path path(db_path_);
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path());
    }
  }

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::cerr << "Failed to open SQLite database: "
              << (db_ ? sqlite3_errmsg(db_) : "unknown") << std::endl;
    Close();
    return false;
  }

  // Enable WAL mode for better concurrency and performance
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

  return InitializeSchema();
}

void DatabaseManager::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool DatabaseManager::InitializeSchema() {
  if (!db_) return false;

  const char* schema_sql = R"(
    CREATE TABLE IF NOT EXISTS matches (
        match_id INTEGER PRIMARY KEY AUTOINCREMENT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        player_a_name TEXT NOT NULL,
        player_a_type TEXT NOT NULL,
        player_b_name TEXT NOT NULL,
        player_b_type TEXT NOT NULL,
        difficulty_a INTEGER DEFAULT 3,
        difficulty_b INTEGER DEFAULT 3,
        board_size INTEGER DEFAULT 15,
        result TEXT NOT NULL,
        winner_seat TEXT,
        termination_reason TEXT NOT NULL,
        total_plies INTEGER NOT NULL,
        opening_path INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS match_moves (
        move_id INTEGER PRIMARY KEY AUTOINCREMENT,
        match_id INTEGER NOT NULL,
        ply_index INTEGER NOT NULL,
        action_id INTEGER NOT NULL,
        action_label TEXT NOT NULL,
        x_coord INTEGER,
        y_coord INTEGER,
        seat TEXT NOT NULL,
        stone_placed TEXT NOT NULL,
        phase_at_move TEXT NOT NULL,
        estimated_win_rate REAL,
        time_spent_ms INTEGER,
        timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (match_id) REFERENCES matches(match_id) ON DELETE CASCADE
    );

    CREATE INDEX IF NOT EXISTS idx_moves_match_id ON match_moves(match_id);
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, schema_sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "Failed to initialize database schema: "
              << (err_msg ? err_msg : "unknown") << std::endl;
    sqlite3_free(err_msg);
    return false;
  }
  return true;
}

int64_t DatabaseManager::CreateMatch(
    const std::string& player_a_name, const std::string& player_a_type,
    const std::string& player_b_name, const std::string& player_b_type,
    int difficulty_a, int difficulty_b, int board_size) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  if (!db_) return -1;

  const char* sql = R"(
    INSERT INTO matches (
        player_a_name, player_a_type, player_b_name, player_b_type,
        difficulty_a, difficulty_b, board_size, result, winner_seat,
        termination_reason, total_plies, opening_path
    ) VALUES (?, ?, ?, ?, ?, ?, ?, 'UNDETERMINED', NULL, 'in_progress', 0, 0);
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, player_a_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, player_a_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, player_b_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, player_b_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, difficulty_a);
  sqlite3_bind_int(stmt, 6, difficulty_b);
  sqlite3_bind_int(stmt, 7, board_size);

  int64_t match_id = -1;
  if (sqlite3_step(stmt) == SQLITE_DONE) {
    match_id = sqlite3_last_insert_rowid(db_);
  }
  sqlite3_finalize(stmt);
  return match_id;
}

bool DatabaseManager::RecordMove(
    int64_t match_id, int ply_index, int action_id,
    const std::string& action_label, int x_coord, int y_coord,
    const std::string& seat, const std::string& stone_placed,
    const std::string& phase_at_move, std::optional<float> estimated_win_rate,
    int time_spent_ms) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  if (!db_) return false;

  const char* sql = R"(
    INSERT INTO match_moves (
        match_id, ply_index, action_id, action_label, x_coord, y_coord,
        seat, stone_placed, phase_at_move, estimated_win_rate, time_spent_ms
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, match_id);
  sqlite3_bind_int(stmt, 2, ply_index);
  sqlite3_bind_int(stmt, 3, action_id);
  sqlite3_bind_text(stmt, 4, action_label.c_str(), -1, SQLITE_TRANSIENT);

  if (x_coord >= 0) {
    sqlite3_bind_int(stmt, 5, x_coord);
  } else {
    sqlite3_bind_null(stmt, 5);
  }

  if (y_coord >= 0) {
    sqlite3_bind_int(stmt, 6, y_coord);
  } else {
    sqlite3_bind_null(stmt, 6);
  }

  sqlite3_bind_text(stmt, 7, seat.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, stone_placed.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, phase_at_move.c_str(), -1, SQLITE_TRANSIENT);

  if (estimated_win_rate.has_value()) {
    sqlite3_bind_double(stmt, 10, static_cast<double>(*estimated_win_rate));
  } else {
    sqlite3_bind_null(stmt, 10);
  }

  sqlite3_bind_int(stmt, 11, time_spent_ms);

  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

bool DatabaseManager::FinishMatch(int64_t match_id, const std::string& result,
                                  const std::string& winner_seat,
                                  const std::string& termination_reason,
                                  int total_plies, int opening_path) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  if (!db_) return false;

  const char* sql = R"(
    UPDATE matches SET
        result = ?,
        winner_seat = ?,
        termination_reason = ?,
        total_plies = ?,
        opening_path = ?
    WHERE match_id = ?;
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, result.c_str(), -1, SQLITE_TRANSIENT);
  if (!winner_seat.empty()) {
    sqlite3_bind_text(stmt, 2, winner_seat.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 2);
  }
  sqlite3_bind_text(stmt, 3, termination_reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, total_plies);
  sqlite3_bind_int(stmt, 5, opening_path);
  sqlite3_bind_int64(stmt, 6, match_id);

  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<MatchRecord> DatabaseManager::GetRecentMatches(int limit,
                                                           int offset) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  std::vector<MatchRecord> results;
  if (!db_) return results;

  const char* sql = R"(
    SELECT match_id, created_at, player_a_name, player_a_type,
           player_b_name, player_b_type, difficulty_a, difficulty_b,
           board_size, result, winner_seat, termination_reason,
           total_plies, opening_path
    FROM matches
    ORDER BY match_id DESC
    LIMIT ? OFFSET ?;
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, limit);
  sqlite3_bind_int(stmt, 2, offset);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MatchRecord record;
    record.match_id = sqlite3_column_int64(stmt, 0);
    record.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.player_a_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.player_a_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    record.player_b_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    record.player_b_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    record.difficulty_a = sqlite3_column_int(stmt, 6);
    record.difficulty_b = sqlite3_column_int(stmt, 7);
    record.board_size = sqlite3_column_int(stmt, 8);
    record.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));

    const char* winner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    record.winner_seat = winner ? winner : "";

    const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    record.termination_reason = reason ? reason : "";

    record.total_plies = sqlite3_column_int(stmt, 12);
    record.opening_path = sqlite3_column_int(stmt, 13);

    results.push_back(std::move(record));
  }
  sqlite3_finalize(stmt);
  return results;
}

std::optional<MatchRecord> DatabaseManager::GetMatchById(int64_t match_id) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  if (!db_) return std::nullopt;

  const char* sql = R"(
    SELECT match_id, created_at, player_a_name, player_a_type,
           player_b_name, player_b_type, difficulty_a, difficulty_b,
           board_size, result, winner_seat, termination_reason,
           total_plies, opening_path
    FROM matches
    WHERE match_id = ?;
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_int64(stmt, 1, match_id);

  std::optional<MatchRecord> result = std::nullopt;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    MatchRecord record;
    record.match_id = sqlite3_column_int64(stmt, 0);
    record.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    record.player_a_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.player_a_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    record.player_b_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    record.player_b_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    record.difficulty_a = sqlite3_column_int(stmt, 6);
    record.difficulty_b = sqlite3_column_int(stmt, 7);
    record.board_size = sqlite3_column_int(stmt, 8);
    record.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));

    const char* winner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    record.winner_seat = winner ? winner : "";

    const char* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    record.termination_reason = reason ? reason : "";

    record.total_plies = sqlite3_column_int(stmt, 12);
    record.opening_path = sqlite3_column_int(stmt, 13);

    result = std::move(record);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<MoveRecord> DatabaseManager::GetMovesForMatch(int64_t match_id) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  std::vector<MoveRecord> moves;
  if (!db_) return moves;

  const char* sql = R"(
    SELECT move_id, match_id, ply_index, action_id, action_label,
           x_coord, y_coord, seat, stone_placed, phase_at_move,
           estimated_win_rate, time_spent_ms, timestamp
    FROM match_moves
    WHERE match_id = ?
    ORDER BY ply_index ASC;
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return moves;
  }

  sqlite3_bind_int64(stmt, 1, match_id);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MoveRecord move;
    move.move_id = sqlite3_column_int64(stmt, 0);
    move.match_id = sqlite3_column_int64(stmt, 1);
    move.ply_index = sqlite3_column_int(stmt, 2);
    move.action_id = sqlite3_column_int(stmt, 3);
    move.action_label = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
      move.x_coord = sqlite3_column_int(stmt, 5);
    } else {
      move.x_coord = -1;
    }

    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
      move.y_coord = sqlite3_column_int(stmt, 6);
    } else {
      move.y_coord = -1;
    }

    move.seat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    move.stone_placed = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    move.phase_at_move = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));

    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
      move.estimated_win_rate = static_cast<float>(sqlite3_column_double(stmt, 10));
    } else {
      move.estimated_win_rate = std::nullopt;
    }

    move.time_spent_ms = sqlite3_column_int(stmt, 11);
    move.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));

    moves.push_back(std::move(move));
  }
  sqlite3_finalize(stmt);
  return moves;
}

bool DatabaseManager::DeleteMatch(int64_t match_id) {
  std::lock_guard<std::mutex> lock(db_mutex_);
  if (!db_) return false;

  const char* sql = "DELETE FROM matches WHERE match_id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, match_id);
  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

}  // namespace gomoku::app
