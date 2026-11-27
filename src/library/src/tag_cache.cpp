#include "wamp/tag_cache.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

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

/// Lê uma coluna contadora de `tracks` para um caminho. -1 quando a faixa não está no banco:
/// o chamador usa isso para saber se a escrita pegou.
int counterFor(sqlite3* db, const char* column, const std::string& path) {
  if (db == nullptr || path.empty()) return -1;
  const std::string sql = std::string("SELECT coalesce(") + column + ", 0) FROM tracks WHERE path = ?;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return -1;
  bindText(statement, 1, path);
  int value = -1;
  if (sqlite3_step(statement) == SQLITE_ROW) value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return value;
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

namespace {
/// Versão do esquema desta biblioteca. Subir de versão = acrescentar um passo em
/// `ensureSchema` que leve um banco antigo até aqui, sem perder linha nenhuma.
/// 1 = tudo o que existia até a 0.9.6 (tracks + raízes + fila + ignorados + presets).
constexpr int kSchemaVersion = 1;

int readUserVersion(sqlite3* db) {
  int version = 0;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) != SQLITE_OK) {
    return 0;
  }
  if (sqlite3_step(statement) == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return version;
}
}  // namespace

bool TagCache::ensureSchema(std::string* error) {
  // Antes de mexer em qualquer coisa: um banco escrito por uma versão mais nova não pode
  // ser rebaixado às cegas (colunas removidas viram "biblioteca sumiu").
  const int version = readUserVersion(db_);
  if (version > kSchemaVersion) {
    if (error != nullptr) {
      *error =
          "a biblioteca foi criada por uma versão mais nova do Amplia — atualize o "
          "aplicativo para abri-la";
    }
    return false;
  }

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
      // Fila do autotag: o que passou do limite da rodada fica aqui e volta na próxima.
      "CREATE TABLE IF NOT EXISTS autotag_queue ("
      " path TEXT PRIMARY KEY, added_at INTEGER NOT NULL"
      ");"
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
      "ALTER TABLE tracks ADD COLUMN bpm_confidence INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN skips INTEGER NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN bpm_tag REAL NOT NULL DEFAULT 0;",
      "ALTER TABLE tracks ADD COLUMN key_pc_tag INTEGER NOT NULL DEFAULT -1;",
      "ALTER TABLE tracks ADD COLUMN key_major_tag INTEGER NOT NULL DEFAULT 0;",
  };
  for (const char* alter : columns) {
    sqlite3_exec(db_, alter, nullptr, nullptr, nullptr);  // erro = coluna já existe
  }
  // Eventos da rádio: o contexto é o que dá sentido ao pulo (ver D-116).
  sqlite3_exec(db_,
               "CREATE TABLE IF NOT EXISTS radio_events ("
               " path TEXT NOT NULL, kind INTEGER NOT NULL,"
               " ctx_bpm INTEGER NOT NULL DEFAULT -1, ctx_key TEXT,"
               " ctx_genre TEXT, ctx_artist TEXT, ts INTEGER NOT NULL);",
               nullptr, nullptr, nullptr);
  // Presets de clima salvos pelo usuário (D-121). Os de fábrica são código, não banco:
  // assim não há como corromper o ponto de partida, só salvar variações com nome próprio.
  sqlite3_exec(db_,
               "CREATE TABLE IF NOT EXISTS humor_presets ("
               " id INTEGER PRIMARY KEY AUTOINCREMENT,"
               " name TEXT NOT NULL,"
               " agitation REAL NOT NULL DEFAULT 50,"
               " tone REAL NOT NULL DEFAULT 50,"
               " stability REAL NOT NULL DEFAULT 50,"
               " blend REAL NOT NULL DEFAULT 0.5,"
               " genres TEXT,"
               " created INTEGER NOT NULL);",
               nullptr, nullptr, nullptr);
  // Nome é o rótulo na árvore: dois iguais deixariam o usuário sem saber qual é qual.
  sqlite3_exec(db_,
               "CREATE UNIQUE INDEX IF NOT EXISTS humor_presets_name"
               " ON humor_presets (name COLLATE NOCASE);",
               nullptr, nullptr, nullptr);

  // Só agora o banco tem a forma da versão corrente: grava a marca. Bancos antigos
  // (user_version = 0) passam pelos CREATE/ALTER acima e saem daqui como versão 1, com as
  // linhas que já tinham — é o caminho de migração, e ele é idempotente.
  if (version != kSchemaVersion) {
    char* message = nullptr;
    const std::string pragma = "PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";";
    if (sqlite3_exec(db_, pragma.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
      if (error != nullptr) *error = message != nullptr ? message : "falha ao marcar a versão";
      if (message != nullptr) sqlite3_free(message);
      return false;
    }
    if (message != nullptr) sqlite3_free(message);
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

void TagCache::enqueueAutotag(const std::vector<std::string>& paths) {
  if (db_ == nullptr || paths.empty()) return;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "INSERT OR IGNORE INTO autotag_queue (path, added_at)"
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

std::vector<std::string> TagCache::autotagQueue(int limit, std::string* error) {
  std::vector<std::string> paths;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "A biblioteca não está aberta.";
    return paths;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT path FROM autotag_queue ORDER BY added_at, rowid LIMIT ?;", -1,
                         &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "Não deu para ler a fila do autotag.";
    return paths;
  }
  sqlite3_bind_int(statement, 1, limit > 0 ? limit : -1);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = sqlite3_column_text(statement, 0);
    if (text != nullptr) paths.emplace_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(statement);
  return paths;
}

void TagCache::dropAutotagPaths(const std::vector<std::string>& paths) {
  if (db_ == nullptr || paths.empty()) return;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM autotag_queue WHERE path = ?;", -1, &statement,
                         nullptr) != SQLITE_OK) {
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

int TagCache::autotagQueueSize() {
  if (db_ == nullptr) return 0;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT count(*) FROM autotag_queue;", -1, &statement, nullptr) !=
      SQLITE_OK) {
    return 0;
  }
  int count = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) count = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return count;
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

int TagCache::bumpPlaycount(const std::string& path) {
  if (db_ == nullptr || path.empty()) return 0;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "UPDATE tracks SET playcount = playcount + 1,"
                         " lastplay = CAST(strftime('%s','now') AS INTEGER)"
                         " WHERE path = ?;",
                         -1, &statement, nullptr) != SQLITE_OK) {
    return 0;
  }
  bindText(statement, 1, path);
  const bool updated = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  if (!updated) return 0;
  const int count = counterFor(db_, "playcount", path);
  return count > 0 ? count : 0;
}

int TagCache::skipsFor(const std::string& path) {
  return counterFor(db_, "skips", path);
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

void TagCache::saveAnalysis(const std::string& path, float bpm, int keyPc, int keyMajor,
                            int confidence) {
  if (db_ == nullptr || path.empty()) return;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "UPDATE tracks SET bpm = ?2, key_pc = ?3, key_major = ?4, "
                    "bpm_confidence = ?5, analyzed = 1 WHERE path = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;
  bindText(statement, 1, path);
  sqlite3_bind_double(statement, 2, static_cast<double>(bpm));
  sqlite3_bind_int(statement, 3, keyPc);
  sqlite3_bind_int(statement, 4, keyMajor);
  sqlite3_bind_int(statement, 5, std::clamp(confidence, 0, 100));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

bool TagCache::analysisFor(const std::string& path, float* bpm, int* keyPc, int* keyMajor,
                           bool* valid, int* confidence) const {
  if (db_ == nullptr || path.empty()) return false;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT bpm, key_pc, key_major, analyzed, bpm_confidence FROM tracks WHERE path = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  bindText(statement, 1, path);
  bool found = false;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    // A linha existe mesmo sem análise: o Alt+3 mostra "—" em vez de esconder o grupo.
    if (bpm != nullptr) *bpm = static_cast<float>(sqlite3_column_double(statement, 0));
    if (keyPc != nullptr) *keyPc = sqlite3_column_int(statement, 1);
    if (keyMajor != nullptr) *keyMajor = sqlite3_column_int(statement, 2);
    if (valid != nullptr) *valid = sqlite3_column_int(statement, 3) != 0;
    if (confidence != nullptr) *confidence = sqlite3_column_int(statement, 4);
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
                                                int days, std::string* error,
                                                const std::string& artist,
                                                const std::string& album,
                                                const std::string& genre) {
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

  // Cada filtro vira uma cláusula com o seu próprio parâmetro; combinar é somar cláusulas.
  std::vector<std::pair<int, std::string>> binds;
  auto addClause = [&where](const std::string& clause) {
    where = where.empty() ? clause : "(" + where + ") AND " + clause;
  };
  const std::string text = filter;
  if (!text.empty()) {
    std::string like = "%";
    for (char c : text) {
      if (c == '%' || c == '_') like.push_back('\\');
      like.push_back(c);
    }
    like.push_back('%');
    // `ESCAPE` explicito: o texto ja vem com \ antes de % e _, mas sem a clausula o
    // SQLite trata \ como literal e a busca por esses caracteres devolve tudo.
    const std::string n = "?" + std::to_string(binds.size() + 1);
    binds.push_back({static_cast<int>(binds.size()) + 1, like});
    addClause("(title LIKE " + n + " ESCAPE '\\' OR artist LIKE " + n + " ESCAPE '\\'" +
              " OR album LIKE " + n + " ESCAPE '\\' OR album_artist LIKE " + n + " ESCAPE '\\')");
  }
  auto addExact = [&](const char* column, const std::string& value) {
    if (value.empty()) return;
    const std::string n = "?" + std::to_string(binds.size() + 1);
    binds.push_back({static_cast<int>(binds.size()) + 1, value});
    addClause(std::string(column) + " = " + n + " COLLATE NOCASE");
  };
  addExact("artist", artist);
  addExact("album", album);
  // Gênero é multivalorado ("Rock / Pop; MPB"): casa por termo inteiro, nunca por
  // substring (um LIKE '%Rock%' traria Post-Rock e Pop-Rock) e sem ligar para caixa,
  // espaço ou qual separador foi usado.
  if (!genre.empty()) {
    std::string needle;
    for (char c : genre) {
      if (c == '%' || c == '_') needle.push_back('\\');
      if (c != ' ') needle.push_back(c);
    }
    const std::string n = "?" + std::to_string(binds.size() + 1);
    binds.push_back({static_cast<int>(binds.size()) + 1, "%;" + needle + ";%"});
    addClause("(';' || replace(replace(replace(replace(genre, ',', ';'), '/', ';'), ' ', '')"
              ", ';;', ';') || ';') LIKE " + n + " ESCAPE '\\'");
  }

  // Ordem padrão da Biblioteca: Ano, Álbum, Faixa (ano desconhecido vai para o fim). As
  // visões por data ou por contagem têm ordem própria, que é o sentido delas.
  std::string order =
      "(year = 0), year, album COLLATE NOCASE, track_no, title COLLATE NOCASE";
  if (view == LibraryView::MostPlayed) order = "playcount DESC, artist COLLATE NOCASE, title";
  if (view == LibraryView::TopRated) order = "rating DESC, artist COLLATE NOCASE, title";
  if (view == LibraryView::RecentlyAdded) order = "dateadded DESC, artist COLLATE NOCASE";
  if (view == LibraryView::RecentlyModified) order = "lastupd DESC, artist COLLATE NOCASE";
  if (view == LibraryView::RecentlyPlayed) order = "lastplay DESC, artist COLLATE NOCASE";

  std::string sql =
      "SELECT path, title, artist, album, album_artist, genre, track_no, year,"
      " duration_ms, sample_rate, channels, bitrate, size, mtime, dateadded, playcount,"
      " rating, lastplay, coalesce(skips, 0) FROM tracks";
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY " + order + ";";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = sqlite3_errmsg(db_);
    return out;
  }
  for (const auto& bind : binds) bindText(statement, bind.first, bind.second);

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
    entry.skips = sqlite3_column_int(statement, 18);
    out.push_back(std::move(entry));
  }
  sqlite3_finalize(statement);
  return out;
}

std::vector<std::string> TagCache::distinctValues(const std::string& column,
                                                  std::string* error) {
  std::vector<std::string> out;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco fechado";
    return out;
  }
  if (column != "artist" && column != "album" && column != "genre") {
    if (error != nullptr) *error = "coluna desconhecida";
    return out;
  }
  // Nome que só muda de caixa é o mesmo nome para quem lê a lista: agrupa por
  // COLLATE NOCASE e fica com a grafia mais usada (empate: a primeira alfabética).
  const std::string sql =
      "SELECT " + column + ", COUNT(*) FROM tracks" +
      " WHERE " + column + " IS NOT NULL AND " + column + " <> ''" +
      " GROUP BY " + column + " COLLATE NOCASE" +
      " ORDER BY COUNT(*) DESC, " + column + " COLLATE NOCASE;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = sqlite3_errmsg(db_);
    return out;
  }
  auto lower = [](const std::string& value) {
    std::string key = value;
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
  };
  // Gênero vem multivalorado ("Alternative Rock / Post-Grunge"): cada termo é um valor
  // de filtro próprio. Conta por grafia para escolher a mais usada de cada termo.
  const bool splitGenres = column == "genre";
  std::map<std::string, std::map<std::string, int>> counts;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* value = sqlite3_column_text(statement, 0);
    if (value == nullptr) continue;
    const int rows = sqlite3_column_int(statement, 1);
    const std::string spelling(reinterpret_cast<const char*>(value));
    if (!splitGenres) {
      counts[lower(spelling)][spelling] += rows;
      continue;
    }
    std::string current;
    auto flush = [&] {
      std::size_t start = current.find_first_not_of(" \t");
      std::size_t end = current.find_last_not_of(" \t");
      if (start != std::string::npos && end != std::string::npos) {
        const std::string term = current.substr(start, end - start + 1);
        counts[lower(term)][term] += rows;
      }
      current.clear();
    };
    for (char c : spelling) {
      if (c == '/' || c == ',' || c == ';') {
        flush();
      } else {
        current.push_back(c);
      }
    }
    flush();
  }
  for (const auto& entry : counts) {
    const std::string* best = nullptr;
    int bestCount = -1;
    for (const auto& spelling : entry.second) {
      if (spelling.second > bestCount ||
          (spelling.second == bestCount && best != nullptr && spelling.first < *best)) {
        best = &spelling.first;
        bestCount = spelling.second;
      }
    }
    if (best != nullptr) out.push_back(*best);
  }
  sqlite3_finalize(statement);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) {
                                          return std::tolower(static_cast<unsigned char>(x)) <
                                                 std::tolower(static_cast<unsigned char>(y));
                                        });
  });
  return out;
}

std::vector<std::string> TagCache::staleFiles(const std::vector<TrackStamp>& files,
                                              std::string* error) {
  std::vector<std::string> out;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco fechado";
    return out;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT size, mtime FROM tracks WHERE path = ?;", -1,
                         &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = sqlite3_errmsg(db_);
    return out;
  }
  for (const TrackStamp& file : files) {
    if (file.path.empty()) continue;
    bindText(statement, 1, file.path);
    const int step = sqlite3_step(statement);
    bool stale = step != SQLITE_ROW;
    if (step == SQLITE_ROW) {
      const std::int64_t size = sqlite3_column_int64(statement, 0);
      const std::int64_t mtime = sqlite3_column_int64(statement, 1);
      stale = size != file.size || mtime != file.modifiedMs;
    }
    sqlite3_reset(statement);
    if (stale) out.push_back(file.path);
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
      " track_no, year, duration_ms, sample_rate, channels, bitrate,"
      " bpm_tag, key_pc_tag, key_major_tag, dateadded, lastupd)"
      " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,"
      " CAST(strftime('%s','now') AS INTEGER), ?)"
      " ON CONFLICT(path) DO UPDATE SET"
      " size = excluded.size, mtime = excluded.mtime, title = excluded.title,"
      " artist = excluded.artist, album = excluded.album,"
      " album_artist = excluded.album_artist, genre = excluded.genre,"
      " track_no = excluded.track_no, year = excluded.year,"
      " duration_ms = excluded.duration_ms, sample_rate = excluded.sample_rate,"
      " channels = excluded.channels, bitrate = excluded.bitrate,"
      " bpm_tag = excluded.bpm_tag, key_pc_tag = excluded.key_pc_tag,"
      " key_major_tag = excluded.key_major_tag,"
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
  sqlite3_bind_double(statement, 15, static_cast<double>(info.bpmTag));
  sqlite3_bind_int(statement, 16, info.keyPcTag);
  sqlite3_bind_int(statement, 17, info.keyMajorTag);
  const std::int64_t modified =
      (info.modifiedMs > 0 ? info.modifiedMs : fileModifiedMs(info.path)) / 1000;
  sqlite3_bind_int64(statement, 18, modified);
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

std::vector<RadioCandidate> TagCache::radioPool(std::string* error) {
  std::vector<RadioCandidate> pool;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco não aberto";
    return pool;
  }
  // BPM/tom percebidos quando existem; senão o da tag. É a mesma precedência da interface.
  const char* sql =
      "SELECT path, coalesce(artist,''), coalesce(album,''), coalesce(genre,''),"
      " coalesce(year,0),"
      " CASE WHEN analyzed = 1 AND bpm > 0 THEN bpm ELSE bpm_tag END,"
      " CASE WHEN analyzed = 1 AND key_pc >= 0 THEN key_pc ELSE key_pc_tag END,"
      " CASE WHEN analyzed = 1 AND key_pc >= 0 THEN key_major ELSE key_major_tag END,"
      " coalesce(skips,0), coalesce(playcount,0), coalesce(rating,0)"
      " FROM tracks;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "consulta da rádio falhou";
    return pool;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    RadioCandidate candidate;
    candidate.path = columnText(statement, 0);
    candidate.artist = columnText(statement, 1);
    candidate.album = columnText(statement, 2);
    candidate.genre = columnText(statement, 3);
    candidate.year = sqlite3_column_int(statement, 4);
    candidate.bpm = sqlite3_column_double(statement, 5);
    candidate.keyPc = sqlite3_column_int(statement, 6);
    candidate.keyMajor = sqlite3_column_int(statement, 7);
    candidate.skips = sqlite3_column_int(statement, 8);
    candidate.plays = sqlite3_column_int(statement, 9);
    candidate.rating = sqlite3_column_int(statement, 10);
    pool.push_back(std::move(candidate));
  }
  sqlite3_finalize(statement);
  return pool;
}

std::vector<RadioSeed> TagCache::recentPlays(int limit, std::string* error) {
  std::vector<RadioSeed> recent;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco não aberto";
    return recent;
  }
  // Mesma precedência de BPM/tom do pool: o percebido manda, o da tag é o plano B.
  const char* sql =
      "SELECT path, coalesce(artist,''), coalesce(album,''), coalesce(genre,''),"
      " coalesce(year,0),"
      " CASE WHEN analyzed = 1 AND bpm > 0 THEN bpm ELSE bpm_tag END,"
      " CASE WHEN analyzed = 1 AND key_pc >= 0 THEN key_pc ELSE key_pc_tag END,"
      " CASE WHEN analyzed = 1 AND key_pc >= 0 THEN key_major ELSE key_major_tag END"
      " FROM tracks WHERE coalesce(lastplay, 0) > 0"
      " ORDER BY lastplay DESC LIMIT ?1;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "consulta do histórico falhou";
    return recent;
  }
  sqlite3_bind_int(statement, 1, limit > 0 ? limit : -1);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    RadioSeed seed;
    seed.path = columnText(statement, 0);
    seed.artist = columnText(statement, 1);
    seed.album = columnText(statement, 2);
    seed.genre = columnText(statement, 3);
    seed.year = sqlite3_column_int(statement, 4);
    seed.bpm = sqlite3_column_double(statement, 5);
    seed.keyPc = sqlite3_column_int(statement, 6);
    seed.keyMajor = sqlite3_column_int(statement, 7);
    recent.push_back(std::move(seed));
  }
  sqlite3_finalize(statement);
  return recent;
}

std::vector<HumorPreset> TagCache::humorPresets(std::string* error) {
  std::vector<HumorPreset> presets;
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco não aberto";
    return presets;
  }
  const char* sql =
      "SELECT id, name, agitation, tone, stability, blend, coalesce(genres,'')"
      " FROM humor_presets ORDER BY id;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "consulta dos presets falhou";
    return presets;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    HumorPreset preset;
    preset.id = sqlite3_column_int(statement, 0);
    preset.factory = false;
    preset.name = columnText(statement, 1);
    preset.point.agitation = sqlite3_column_double(statement, 2);
    preset.point.tone = sqlite3_column_double(statement, 3);
    preset.point.stability = sqlite3_column_double(statement, 4);
    preset.blend = sqlite3_column_double(statement, 5);
    preset.genres = columnText(statement, 6);
    presets.push_back(std::move(preset));
  }
  sqlite3_finalize(statement);
  return presets;
}

int TagCache::addHumorPreset(const HumorPreset& preset, std::string* error) {
  if (db_ == nullptr) {
    if (error != nullptr) *error = "banco não aberto";
    return 0;
  }
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "INSERT INTO humor_presets (name, agitation, tone, stability, blend, genres, created)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, CAST(strftime('%s','now') AS INTEGER));";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "gravação do preset falhou";
    return 0;
  }
  bindText(statement, 1, preset.name);
  sqlite3_bind_double(statement, 2, preset.point.agitation);
  sqlite3_bind_double(statement, 3, preset.point.tone);
  sqlite3_bind_double(statement, 4, preset.point.stability);
  sqlite3_bind_double(statement, 5, preset.blend);
  bindText(statement, 6, preset.genres);
  const bool ok = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  if (!ok) {
    // Nome repetido é o caso comum (o índice único barra): a tela avisa e pede outro nome.
    if (error != nullptr) *error = "já existe um preset com esse nome";
    return 0;
  }
  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool TagCache::renameHumorPreset(int id, const std::string& name, std::string* error) {
  if (db_ == nullptr || id <= 0 || name.empty()) {
    if (error != nullptr) *error = "preset inválido";
    return false;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE humor_presets SET name = ?2 WHERE id = ?1;", -1,
                         &statement, nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "renomeação do preset falhou";
    return false;
  }
  sqlite3_bind_int(statement, 1, id);
  bindText(statement, 2, name);
  const bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_) > 0;
  sqlite3_finalize(statement);
  if (!ok && error != nullptr) *error = "não foi possível renomear o preset";
  return ok;
}

bool TagCache::removeHumorPreset(int id, std::string* error) {
  if (db_ == nullptr || id <= 0) {
    if (error != nullptr) *error = "preset inválido";
    return false;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM humor_presets WHERE id = ?1;", -1, &statement,
                         nullptr) != SQLITE_OK) {
    if (error != nullptr) *error = "remoção do preset falhou";
    return false;
  }
  sqlite3_bind_int(statement, 1, id);
  const bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_) > 0;
  sqlite3_finalize(statement);
  if (!ok && error != nullptr) *error = "não foi possível remover o preset";
  return ok;
}

void TagCache::recordRadioEvent(const std::string& path, int kind,
                                const RadioContext& context) {  if (db_ == nullptr || path.empty()) return;
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "INSERT INTO radio_events (path, kind, ctx_bpm, ctx_key, ctx_genre, ctx_artist, ts)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, CAST(strftime('%s','now') AS INTEGER));";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;
  bindText(statement, 1, path);
  sqlite3_bind_int(statement, 2, kind);
  sqlite3_bind_int(statement, 3, context.bpmBucket);
  bindText(statement, 4, context.keyCamelot);
  bindText(statement, 5, context.genre);
  bindText(statement, 6, context.artist);
  const bool ok = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  if (!ok) return;
  if (kind == 0 || kind == 2) {
    sqlite3_prepare_v2(db_, "UPDATE tracks SET skips = skips + 1 WHERE path = ?1;", -1,
                       &statement, nullptr);
    bindText(statement, 1, path);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
  }
}

}  // namespace wamp
