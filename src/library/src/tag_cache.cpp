#include "wamp/tag_cache.h"

#include <sqlite3.h>

#include <utility>

#include "wamp/file_info.h"

namespace wamp {
namespace {

/// Quantas gravações ficam numa transação antes de confirmar. Lote grande demais segura
/// dados em memória se o app fechar de repente; pequeno demais perde o ganho do WAL.
constexpr int kFlushEvery = 32;

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
  sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()),
                    SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt* statement, int index) {
  const unsigned char* text = sqlite3_column_text(statement, index);
  if (text == nullptr) return {};
  return std::string(reinterpret_cast<const char*>(text));
}

}  // namespace

TagCache::TagCache(std::string path) : path_(std::move(path)) {}

TagCache::~TagCache() {
  flush();
  if (db_ != nullptr) sqlite3_close(db_);
  db_ = nullptr;
}

bool TagCache::open(std::string* error) {
  if (db_ != nullptr) return true;

  const int rc = sqlite3_open(path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    if (error != nullptr) *error = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // WAL deixa a escrita barata e não trava o arquivo para leitura.
  char* message = nullptr;
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", nullptr, nullptr,
               &message);
  if (message != nullptr) sqlite3_free(message);
  return ensureSchema(error);
}

bool TagCache::ensureSchema(std::string* error) {
  const char* schema =
      "CREATE TABLE IF NOT EXISTS tracks ("
      " path TEXT PRIMARY KEY,"
      " size INTEGER NOT NULL,"
      " mtime INTEGER NOT NULL,"
      " title TEXT, artist TEXT, album TEXT, album_artist TEXT, genre TEXT,"
      " track_no INTEGER, year INTEGER,"
      " duration_ms INTEGER, sample_rate INTEGER, channels INTEGER, bitrate INTEGER"
      ");";
  char* message = nullptr;
  if (sqlite3_exec(db_, schema, nullptr, nullptr, &message) != SQLITE_OK) {
    if (error != nullptr) *error = message != nullptr ? message : "falha ao criar a tabela";
    if (message != nullptr) sqlite3_free(message);
    return false;
  }
  if (message != nullptr) sqlite3_free(message);
  return true;
}

bool TagCache::lookup(const std::string& path, TrackInfo* info) {
  if (db_ == nullptr || info == nullptr) return false;

  const std::int64_t size = fileSizeBytes(path);
  const std::int64_t mtime = fileModifiedMs(path);
  if (size <= 0 || mtime <= 0) return false;  // arquivo sumiu ou não dá para verificar

  const char* sql =
      "SELECT size, mtime, title, artist, album, album_artist, genre, track_no, year,"
      " duration_ms, sample_rate, channels, bitrate FROM tracks WHERE path = ?;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  bindText(statement, 1, path);

  const bool row = sqlite3_step(statement) == SQLITE_ROW;
  const bool valid = row && sqlite3_column_int64(statement, 0) == size &&
                     sqlite3_column_int64(statement, 1) == mtime;
  if (valid) {
    TrackInfo cached;
    cached.path = path;
    cached.fileSize = size;
    cached.modifiedMs = mtime;
    cached.title = columnText(statement, 2);
    cached.artist = columnText(statement, 3);
    cached.album = columnText(statement, 4);
    cached.albumArtist = columnText(statement, 5);
    cached.genre = columnText(statement, 6);
    cached.trackNo = sqlite3_column_int(statement, 7);
    cached.year = sqlite3_column_int(statement, 8);
    cached.durationMs = sqlite3_column_int64(statement, 9);
    cached.sampleRate = sqlite3_column_int(statement, 10);
    cached.channels = sqlite3_column_int(statement, 11);
    cached.bitrate = sqlite3_column_int(statement, 12);
    *info = std::move(cached);
  }
  sqlite3_finalize(statement);
  return valid;
}

void TagCache::store(const TrackInfo& info) {
  if (db_ == nullptr || info.path.empty()) return;

  if (!inTransaction_) {
    sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
    inTransaction_ = true;
  }

  const char* sql =
      "INSERT INTO tracks (path, size, mtime, title, artist, album, album_artist, genre,"
      " track_no, year, duration_ms, sample_rate, channels, bitrate)"
      " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
      " ON CONFLICT(path) DO UPDATE SET"
      " size = excluded.size, mtime = excluded.mtime, title = excluded.title,"
      " artist = excluded.artist, album = excluded.album,"
      " album_artist = excluded.album_artist, genre = excluded.genre,"
      " track_no = excluded.track_no, year = excluded.year,"
      " duration_ms = excluded.duration_ms, sample_rate = excluded.sample_rate,"
      " channels = excluded.channels, bitrate = excluded.bitrate;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;

  bindText(statement, 1, info.path);
  sqlite3_bind_int64(statement, 2, info.fileSize > 0 ? info.fileSize : fileSizeBytes(info.path));
  sqlite3_bind_int64(statement, 3,
                     info.modifiedMs > 0 ? info.modifiedMs : fileModifiedMs(info.path));
  bindText(statement, 4, info.title);
  bindText(statement, 5, info.artist);
  bindText(statement, 6, info.album);
  bindText(statement, 7, info.albumArtist);
  bindText(statement, 8, info.genre);
  sqlite3_bind_int(statement, 9, info.trackNo);
  sqlite3_bind_int(statement, 10, info.year);
  sqlite3_bind_int64(statement, 11, info.durationMs);
  sqlite3_bind_int(statement, 12, info.sampleRate);
  sqlite3_bind_int(statement, 13, info.channels);
  sqlite3_bind_int(statement, 14, info.bitrate);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  if (++pending_ >= kFlushEvery) flush();
}

void TagCache::flush() {
  if (db_ == nullptr || !inTransaction_) {
    pending_ = 0;
    return;
  }
  sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  inTransaction_ = false;
  pending_ = 0;
}

std::size_t TagCache::count() {
  if (db_ == nullptr) return 0;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tracks;", -1, &statement, nullptr) !=
      SQLITE_OK) {
    return 0;
  }
  std::size_t total = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    total = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
  }
  sqlite3_finalize(statement);
  return total;
}

}  // namespace wamp
