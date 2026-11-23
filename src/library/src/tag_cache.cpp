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
      " duration_ms INTEGER, sample_rate INTEGER, channels INTEGER, bitrate INTEGER,"
      " dateadded INTEGER, playcount INTEGER NOT NULL DEFAULT 0,"
      " rating INTEGER NOT NULL DEFAULT 0, lastplay INTEGER NOT NULL DEFAULT 0,"
      " lastupd INTEGER NOT NULL DEFAULT 0"
      ");"
      "CREATE TABLE IF NOT EXISTS roots (path TEXT PRIMARY KEY);"
      "CREATE TABLE IF NOT EXISTS scan_queue ("
      " path TEXT PRIMARY KEY, added_at INTEGER NOT NULL"
      ");"
      // Removido da biblioteca pelo usuário: a varredura respeita esta lista, senão a
      // faixa voltaria no próximo Atualizar e pareceria que a remoção não funcionou.
      "CREATE TABLE IF NOT EXISTS ignored ("
      " path TEXT PRIMARY KEY, ignored_at INTEGER NOT NULL"
      ");";
  char* message = nullptr;
  if (sqlite3_exec(db_, schema, nullptr, nullptr, &message) != SQLITE_OK) {
    if (error != nullptr) *error = message != nullptr ? message : "falha ao criar a tabela";
    if (message != nullptr) sqlite3_free(message);
    return false;
  }
  if (message != nullptr) sqlite3_free(message);

  // Bancos criados antes da F3 não têm as colunas novas; ALTER TABLE é barato e só roda
  // uma vez por arquivo.
  const char* columns[] = {
      "ALTER TABLE tracks ADD COLUMN dateadded INTEGER;",
      "ALTER TABLE tracks ADD COLUMN playcount INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN rating INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN lastplay INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN lastupd INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN bpm REAL NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN key_pc INTEGER NOT NULL DEFAULT -1;",
      "ALTER TABLE tracks ADD COLUMN key_major INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN analyzed INTEGER NOT NULL DEFAULT 0;",
  };
  for (const char* alter : columns) {
    sqlite3_exec(db_, alter, nullptr, nullptr, nullptr);  // erro = coluna já existe
  }
  return true;
}

void TagCache::queueScan(const std::vector<std::string>& paths) {
  if (db_ == nullptr) return;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT OR IGNORE INTO scan_queue (path, added_at)"
          " VALUES (?, CAST(strftime('%s','now') AS INTEGER));",
          -1, &statement, nullptr) != SQLITE_OK) {
    return;
  }
  sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
  for (const auto& path : paths) {
    bindText(statement, 1, path);
    sqlite3_step(statement);
    sqlite3_reset(statement);
  }
  sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  sqlite3_finalize(statement);
}

std::vector<std::string> TagCache::scanQueue(std::string* error) {
  std::vector<std::string> out;
  if (db_ == nullptr) {
    if (error) *error = "banco fechado";
    return out;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT path FROM scan_queue ORDER BY added_at, path;", -1,
                         &statement, nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return out;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(statement, 0);
    if (text != nullptr) out.emplace_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(statement);
  return out;
}

void TagCache::dropFromScanQueue(const std::string& path) {
  if (db_ == nullptr || path.empty()) return;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM scan_queue WHERE path = ?;", -1, &statement,
                         nullptr) != SQLITE_OK) {
    return;
  }
  bindText(statement, 1, path);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void TagCache::bumpPlaycount(const std::string& path) {
  if (db_ == nullptr || path.empty()) return;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "UPDATE tracks SET playcount = playcount + 1,"
                         " lastplay = CAST(strftime('%s','now') AS INTEGER)"
                         " WHERE path = ?;",
                         -1, &statement, nullptr) != SQLITE_OK) {
    return;
  }
  bindText(statement, 1, path);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void TagCache::setRating(const std::string& path, int rating) {
  if (db_ == nullptr || path.empty()) return;
  if (rating < 0) rating = 0;
  if (rating > 5) rating = 5;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE tracks SET rating = ? WHERE path = ?;", -1,
                         &statement, nullptr) != SQLITE_OK) {
    return;
  }
  sqlite3_bind_int(statement, 1, rating);
  bindText(statement, 2, path);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

bool TagCache::addRoot(const std::string& path, std::string* error) {
  if (db_ == nullptr) { if (error) *error = "banco fechado"; return false; }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "INSERT OR IGNORE INTO roots (path) VALUES (?);", -1,
                         &statement, nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return false;
  }
  bindText(statement, 1, path);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (rc != SQLITE_DONE) { if (error) *error = sqlite3_errmsg(db_); return false; }
  return true;
}

void TagCache::saveAnalysis(const std::string& path, float bpm, int keyPc, int keyMajor) {
  if (db_ == nullptr || path.empty()) return;
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "UPDATE tracks SET bpm = ?2, key_pc = ?3, key_major = ?4, analyzed = 1 WHERE path = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;
  bindText(statement, 1, path);
  sqlite3_bind_double(statement, 2, static_cast<double>(bpm));
  sqlite3_bind_int(statement, 3, keyPc);
  sqlite3_bind_int(statement, 4, keyMajor);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

bool TagCache::analysisFor(const std::string& path, float* bpm, int* keyPc, int* keyMajor,
                           bool* valid) {
  if (db_ == nullptr || path.empty()) return false;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT bpm, key_pc, key_major, analyzed FROM tracks WHERE path = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  bindText(statement, 1, path);
  bool found = false;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    // A linha existe mesmo sem análise: o Alt+3 mostra "—" em vez de esconder o grupo.
    if (bpm != nullptr) *bpm = static_cast<float>(sqlite3_column_double(statement, 0));
    if (keyPc != nullptr) *keyPc = sqlite3_column_int(statement, 1);
    if (keyMajor != nullptr) *keyMajor = sqlite3_column_int(statement, 2);
    if (valid != nullptr) *valid = sqlite3_column_int(statement, 3) != 0;
    found = true;
  }
  sqlite3_finalize(statement);
  return found;
}

TagCache::AnalysisStats TagCache::analysisStats(std::string* error) {
  AnalysisStats stats;
  if (db_ == nullptr) {
    if (error) *error = "banco fechado";
    return stats;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT count(*), "
                         "coalesce(sum(CASE WHEN analyzed = 1 THEN 1 ELSE 0 END), 0), "
                         "coalesce(sum(CASE WHEN analyzed = 1 THEN duration_ms ELSE 0 END), 0) "
                         "FROM tracks;",
                         -1, &statement, nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return stats;
  }
  if (sqlite3_step(statement) == SQLITE_ROW) {
    stats.totalTracks = sqlite3_column_int(statement, 0);
    stats.tracks = sqlite3_column_int(statement, 1);
    stats.milliseconds = sqlite3_column_int64(statement, 2);
  }
  sqlite3_finalize(statement);
  return stats;
}

std::size_t TagCache::forgetTracks(const std::vector<std::string>& paths, std::string* error) {
  // Fecha a transação de gravação pendente (a varredura pode estar no meio de um lote):
  // sem isso o BEGIN abaixo falha e a remoção não acontece.
  flush();
  if (db_ == nullptr) {
    if (error) *error = "banco fechado";
    return 0;
  }
  if (paths.empty()) return 0;
  char* message = nullptr;
  if (sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, &message) != SQLITE_OK) {
    if (error) *error = message != nullptr ? message : "falha ao abrir transacao";
    sqlite3_free(message);
    return 0;
  }
  sqlite3_stmt* drop = nullptr;
  sqlite3_stmt* keep = nullptr;
  sqlite3_prepare_v2(db_, "DELETE FROM tracks WHERE path = ?;", -1, &drop, nullptr);
  sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO ignored (path, ignored_at) VALUES (?1, strftime('%s','now'));",
                     -1, &keep, nullptr);
  std::size_t removed = 0;
  for (const auto& path : paths) {
    bindText(drop, 1, path);
    if (sqlite3_step(drop) == SQLITE_DONE && sqlite3_changes(db_) > 0) ++removed;
    sqlite3_reset(drop);
    bindText(keep, 1, path);
    sqlite3_step(keep);
    sqlite3_reset(keep);
  }
  sqlite3_finalize(drop);
  sqlite3_finalize(keep);
  if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &message) != SQLITE_OK) {
    if (error) *error = message != nullptr ? message : "falha ao confirmar";
    sqlite3_free(message);
    return 0;
  }
  return removed;
}

bool TagCache::clearLibrary(std::string* error) {
  flush();
  if (db_ == nullptr) {
    if (error) *error = "banco fechado";
    return false;
  }
  char* message = nullptr;
  const char* sql =
      "BEGIN;"
      "DELETE FROM tracks;"
      "DELETE FROM roots;"
      "DELETE FROM scan_queue;"
      "DELETE FROM ignored;"
      "COMMIT;";
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    if (error) *error = message != nullptr ? message : "falha ao limpar";
    sqlite3_free(message);
    return false;
  }
  return true;
}

std::vector<std::string> TagCache::ignoredPaths(std::string* error) {
  std::vector<std::string> result;
  if (db_ == nullptr) {
    if (error) *error = "banco fechado";
    return result;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT path FROM ignored;", -1, &statement, nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return result;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(statement, 0);
    if (text != nullptr) result.emplace_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(statement);
  return result;
}

bool TagCache::removeRoot(const std::string& path, std::string* error) {
  if (db_ == nullptr) { if (error) *error = "banco fechado"; return false; }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM roots WHERE path = ?;", -1, &statement,
                         nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return false;
  }
  bindText(statement, 1, path);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (rc != SQLITE_DONE) { if (error) *error = sqlite3_errmsg(db_); return false; }
  return true;
}

std::vector<std::string> TagCache::roots(std::string* error) {
  std::vector<std::string> out;
  if (db_ == nullptr) { if (error) *error = "banco fechado"; return out; }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT path FROM roots ORDER BY path;", -1, &statement,
                         nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return out;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    if (text != nullptr) out.emplace_back(text);
  }
  sqlite3_finalize(statement);
  return out;
}

std::vector<LibraryEntry> TagCache::queryLibrary(LibraryView view, const std::string& filter,
                                                int days, std::string* error) {
  std::vector<LibraryEntry> out;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco fechado";
    return out;
  }

  const int window = days > 0 ? days : 3;
  std::string where;
  switch (view) {
    case LibraryView::RecentlyAdded:
      where = "dateadded > CAST(strftime('%s','now') AS INTEGER) - " +
              std::to_string(window * 86400);
      break;
    case LibraryView::RecentlyModified:
      where = "lastupd > CAST(strftime('%s','now') AS INTEGER) - " +
              std::to_string(window * 86400);
      break;
    case LibraryView::RecentlyPlayed:
      // Duas semanas, como no ML original ("lastplay > [2 weeks ago]").
      where = "lastplay > CAST(strftime('%s','now') AS INTEGER) - 1209600";
      break;
    case LibraryView::NeverPlayed:
      where = "playcount = 0";
      break;
    case LibraryView::MostPlayed:
      where = "playcount > 0";
      break;
    case LibraryView::TopRated:
      where = "rating >= 3";
      break;
    case LibraryView::All:
    case LibraryView::Audio:
      break;
  }

  std::string text = filter;
  const bool hasFilter = !text.empty();
  if (hasFilter) {
    std::string like = "%";
    for (char c : text) {
      if (c == '%' || c == '_') like.push_back('\\');
      like.push_back(c);
    }
    like.push_back('%');
    text = like;
    // `ESCAPE` explicito: o texto ja vem com \ antes de % e _, mas sem a clausula o
    // SQLite trata \ como literal e a busca por esses caracteres devolve tudo.
    const std::string clause =
        "(title LIKE ?1 ESCAPE '\\' OR artist LIKE ?1 ESCAPE '\\'"
        " OR album LIKE ?1 ESCAPE '\\' OR album_artist LIKE ?1 ESCAPE '\\')";
    where = where.empty() ? clause : "(" + where + ") AND " + clause;
  }

  std::string order = "artist COLLATE NOCASE, album COLLATE NOCASE, track_no, title";
  if (view == LibraryView::MostPlayed) order = "playcount DESC, artist COLLATE NOCASE, title";
  if (view == LibraryView::TopRated) order = "rating DESC, artist COLLATE NOCASE, title";
  if (view == LibraryView::RecentlyAdded) order = "dateadded DESC, artist COLLATE NOCASE";
  if (view == LibraryView::RecentlyModified) order = "lastupd DESC, artist COLLATE NOCASE";
  if (view == LibraryView::RecentlyPlayed) order = "lastplay DESC, artist COLLATE NOCASE";

  std::string sql =
      "SELECT path, title, artist, album, album_artist, genre, track_no, year,"
      " duration_ms, sample_rate, channels, bitrate, size, mtime, dateadded, playcount,"
      " rating, lastplay FROM tracks";
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY " + order + ";";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = sqlite3_errmsg(db_);
    return out;
  }
  if (hasFilter) bindText(statement, 1, text);

  while (sqlite3_step(statement) == SQLITE_ROW) {
    LibraryEntry entry;
    const unsigned char* path = sqlite3_column_text(statement, 0);
    if (path != nullptr) entry.info.path = reinterpret_cast<const char*>(path);
    auto textAt = [statement](int column) {
      const unsigned char* value = sqlite3_column_text(statement, column);
      return value != nullptr ? reinterpret_cast<const char*>(value) : "";
    };
    entry.info.title = textAt(1);
    entry.info.artist = textAt(2);
    entry.info.album = textAt(3);
    entry.info.albumArtist = textAt(4);
    entry.info.genre = textAt(5);
    entry.info.trackNo = sqlite3_column_int(statement, 6);
    entry.info.year = sqlite3_column_int(statement, 7);
    entry.info.durationMs = sqlite3_column_int64(statement, 8);
    entry.info.sampleRate = sqlite3_column_int(statement, 9);
    entry.info.channels = sqlite3_column_int(statement, 10);
    entry.info.bitrate = sqlite3_column_int(statement, 11);
    entry.info.fileSize = sqlite3_column_int64(statement, 12);
    entry.info.modifiedMs = sqlite3_column_int64(statement, 13) * 1000;
    entry.dateadded = sqlite3_column_int64(statement, 14);
    entry.playcount = sqlite3_column_int(statement, 15);
    entry.rating = sqlite3_column_int(statement, 16);
    entry.lastplay = sqlite3_column_int64(statement, 17);
    out.push_back(std::move(entry));
  }
  sqlite3_finalize(statement);
  return out;
}

std::vector<std::string> TagCache::allPaths(std::string* error) {
  std::vector<std::string> out;
  if (db_ == nullptr) { if (error) *error = "banco fechado"; return out; }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT path FROM tracks ORDER BY path;", -1, &statement,
                         nullptr) != SQLITE_OK) {
    if (error) *error = sqlite3_errmsg(db_);
    return out;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    if (text != nullptr) out.emplace_back(text);
  }
  sqlite3_finalize(statement);
  return out;
}

namespace {

/// Lê a linha de `tracks` para o caminho. Com validate=true, exige que tamanho e mtime
/// batam com o arquivo em disco.
bool readRow(sqlite3* db, const std::string& path, TrackInfo* info, bool validate) {
  const std::int64_t size = validate ? fileSizeBytes(path) : 0;
  const std::int64_t mtime = validate ? fileModifiedMs(path) : 0;
  if (validate && (size <= 0 || mtime <= 0)) return false;  // sumiu

  const char* sql =
      "SELECT size, mtime, title, artist, album, album_artist, genre, track_no, year,"
      " duration_ms, sample_rate, channels, bitrate FROM tracks WHERE path = ?;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  bindText(statement, 1, path);
  const bool row = sqlite3_step(statement) == SQLITE_ROW;
  const bool valid = row && (!validate || (sqlite3_column_int64(statement, 0) == size &&
                                            sqlite3_column_int64(statement, 1) == mtime));
  bool filled = false;
  if (valid) {
    TrackInfo cached;
    cached.path = path;
    cached.fileSize = sqlite3_column_int64(statement, 0);
    cached.modifiedMs = sqlite3_column_int64(statement, 1);
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
    filled = true;
  }
  sqlite3_finalize(statement);
  return filled;
}

}  // namespace

bool TagCache::lookup(const std::string& path, TrackInfo* info) {
  if (db_ == nullptr || info == nullptr) return false;
  return readRow(db_, path, info, true);
}

bool TagCache::lookupFast(const std::string& path, TrackInfo* info) {
  if (db_ == nullptr || info == nullptr) return false;
  return readRow(db_, path, info, false);
}

void TagCache::store(const TrackInfo& info) {
  if (db_ == nullptr || info.path.empty()) return;

  if (!inTransaction_) {
    sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
    inTransaction_ = true;
  }

  const char* sql =
      "INSERT INTO tracks (path, size, mtime, title, artist, album, album_artist, genre,"
      " track_no, year, duration_ms, sample_rate, channels, bitrate, dateadded, lastupd)"
      " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CAST(strftime('%s','now') AS INTEGER), ?)"
      " ON CONFLICT(path) DO UPDATE SET"
      " size = excluded.size, mtime = excluded.mtime, title = excluded.title,"
      " artist = excluded.artist, album = excluded.album,"
      " album_artist = excluded.album_artist, genre = excluded.genre,"
      " track_no = excluded.track_no, year = excluded.year,"
      " duration_ms = excluded.duration_ms, sample_rate = excluded.sample_rate,"
      " channels = excluded.channels, bitrate = excluded.bitrate,"
      " lastupd = excluded.lastupd;";
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
  const std::int64_t modified =
      (info.modifiedMs > 0 ? info.modifiedMs : fileModifiedMs(info.path)) / 1000;
  sqlite3_bind_int64(statement, 15, modified);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  // Gravou a tag: sai da fila de indexação (mesma transação, então o kill não deixa
  // pendente fantasma nem trabalho perdido).
  sqlite3_stmt* dequeue = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM scan_queue WHERE path = ?;", -1, &dequeue,
                         nullptr) == SQLITE_OK) {
    bindText(dequeue, 1, info.path);
    sqlite3_step(dequeue);
    sqlite3_finalize(dequeue);
  }

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
