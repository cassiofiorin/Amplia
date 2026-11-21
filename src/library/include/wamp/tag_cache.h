#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "wamp/types.h"

struct sqlite3;

namespace wamp {

/// Cache em disco dos metadados já lidos, para não reabrir toda a playlist a cada
/// inicialização. A chave é o caminho; `size` e `mtime` dizem se o registro ainda vale.
///
/// SQLite em modo WAL: cada faixa lida entra numa transação que é confirmada a cada
/// `kFlushEvery` gravações, e o `flush()` fecha o que faltar.
class TagCache final {
 public:
  explicit TagCache(std::string path);
  ~TagCache();

  TagCache(const TagCache&) = delete;
  TagCache& operator=(const TagCache&) = delete;

  /// Abre (ou cria) o banco. Devolve false e preenche `error` se não der.
  bool open(std::string* error);

  /// Metadados guardados para `path`, se o arquivo não mudou desde a última leitura.
  bool lookup(const std::string& path, TrackInfo* info);

  /// Igual a lookup(), mas **sem** conferir tamanho e mtime: devolve o que estiver gravado.
  /// Usado na abertura, quando validar 400 caminhos de rede custaria segundos.
  bool lookupFast(const std::string& path, TrackInfo* info);

  /// Guarda/atualiza os metadados de uma faixa (usa `path`, `fileSize`, `modifiedMs`).
  void store(const TrackInfo& info);

  /// Confirma a transação pendente. Chamado no fechamento e a cada lote.
  void flush();

  /// Quantas faixas o banco tem (0 se estiver fechado).
  std::size_t count();

  const std::string& path() const { return path_; }
  bool isOpen() const { return db_ != nullptr; }

 private:
  bool ensureSchema(std::string* error);

  std::string path_;
  sqlite3* db_ = nullptr;
  bool inTransaction_ = false;
  int pending_ = 0;
};

}  // namespace wamp
