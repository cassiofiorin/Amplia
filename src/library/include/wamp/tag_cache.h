#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "wamp/radio.h"
#include "wamp/types.h"

struct sqlite3;

namespace wamp {

/// Visões da Biblioteca, como no ML original: cada uma é uma consulta fixa.
enum class LibraryView {
  All = 0,
  Audio = 1,
  RecentlyAdded = 2,
  RecentlyModified = 3,
  RecentlyPlayed = 4,
  NeverPlayed = 5,
  MostPlayed = 6,
  TopRated = 7,
};

/// Ficha do arquivo no disco, do jeito que a varredura incremental compara: caminho,
/// tamanho e mtime. Quem não mudou desde a última leitura não é reaberto.
struct TrackStamp {
  std::string path;
  std::int64_t size = 0;
  std::int64_t modifiedMs = 0;
};

/// Faixa da Biblioteca com os campos que só o banco guarda (nota, contagem, datas).
struct LibraryEntry {
  TrackInfo info;
  int rating = 0;
  int playcount = 0;
  std::int64_t dateadded = 0;
  std::int64_t lastplay = 0;
};

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

  /// Pastas-raiz da biblioteca (as que o usuário mandou importar).
  bool addRoot(const std::string& path, std::string* error);
  bool removeRoot(const std::string& path, std::string* error);
  /// Tira as faixas da biblioteca (os arquivos ficam) e as marca como ignoradas, para a
  /// varredura não trazer de volta. Devolve quantas linhas realmente saíram.
  std::size_t forgetTracks(const std::vector<std::string>& paths, std::string* error);
  /// Apaga tracks, roots, scan_queue e a lista de ignorados.
  bool clearLibrary(std::string* error);
  std::vector<std::string> ignoredPaths(std::string* error);

  /// Guarda BPM/tonalidade de uma faixa e marca como analisada.
  void saveAnalysis(const std::string& path, float bpm, int keyPc, int keyMajor,
                    int confidence);
  struct AnalysisStats {
    int totalTracks = 0;  ///< faixas na biblioteca
    int tracks = 0;       ///< quantas já foram analisadas
    long long milliseconds = 0;
  };
  AnalysisStats analysisStats(std::string* error);

  /// Constrói a lista de candidatas da rádio (uma consulta só, já com pulos e ouvidas).
  std::vector<RadioCandidate> radioPool(std::string* error);

  /// Últimas faixas tocadas, da mais recente para a mais antiga: é o "clima de agora" que
  /// a semente composta da Rádio local usa. `limit` <= 0 devolve o histórico inteiro.
  std::vector<RadioSeed> recentPlays(int limit, std::string* error);

  /// Registra um pulo/recusa com o contexto em que aconteceu; `kind` 0 = pulou,
  /// 1 = ouviu até o fim, 2 = recusou (removeu da fila sem tocar).
  void recordRadioEvent(const std::string& path, int kind, const RadioContext& context);

  /// Presets de clima salvos pelo usuário, na ordem em que foram criados. Os de fábrica
  /// vivem no código (`factoryHumorPresets()`) e não aparecem aqui.
  std::vector<HumorPreset> humorPresets(std::string* error);
  /// Grava um preset novo e devolve o id; 0 quando o nome já existe ou a gravação falha.
  int addHumorPreset(const HumorPreset& preset, std::string* error);
  bool renameHumorPreset(int id, const std::string& name, std::string* error);
  bool removeHumorPreset(int id, std::string* error);

  /// Valores da análise local de um caminho. Devolve `false` se a faixa não está no banco.
  bool analysisFor(const std::string& path, float* bpm, int* keyPc, int* keyMajor,
                       bool* valid, int* confidence = nullptr) const;
  std::vector<std::string> roots(std::string* error);

  /// Fila de indexação persistida: os pendentes entram aqui antes de a leitura começar e
  /// saem quando a tag é gravada. Se o app morrer no meio, a próxima execução retoma.
  void queueScan(const std::vector<std::string>& paths);
  std::vector<std::string> scanQueue(std::string* error);
  /// Tira um caminho da fila sem gravar tag (arquivo ilegível/sumido): não insiste para sempre.
  void dropFromScanQueue(const std::string& path);

  /// Conta uma reprodução: `playcount + 1` e `lastplay` agora.
  void bumpPlaycount(const std::string& path);

  /// Nota de 0 a 5 (0 = sem classificação).
  void setRating(const std::string& path, int rating);

  /// Consulta uma visão da Biblioteca, opcionalmente filtrando por texto em
  /// título/artista/álbum e por artista/álbum/gênero exatos (vazio = todos). Os filtros
  /// combinam entre si e com o texto. `days` vale para "adicionados/modificados há N dias".
  std::vector<LibraryEntry> queryLibrary(LibraryView view, const std::string& filter,
                                         int days, std::string* error,
                                         const std::string& artist = {},
                                         const std::string& album = {},
                                         const std::string& genre = {});

  /// Valores distintos de `artist`, `album` ou `genre`, em ordem alfabética (sem os
  /// vazios). Serve para encher os filtros da Biblioteca. Outra coluna devolve vazio.
  std::vector<std::string> distinctValues(const std::string& column, std::string* error);

  /// Quais fichas precisam ser (re)lidas — as que não estão no índice e as que mudaram de
  /// tamanho ou mtime. É a varredura incremental: só o que mudou desde a última vez.
  std::vector<std::string> staleFiles(const std::vector<TrackStamp>& files, std::string* error);

  /// Todos os caminhos já indexados, em ordem alfabética.
  std::vector<std::string> allPaths(std::string* error);

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
