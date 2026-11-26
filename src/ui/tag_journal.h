// Diário do autotag: o que foi gravado em cada lote, para poder desfazer.
//
// O diário guarda **valores anteriores**, não bytes: restaurar devolve os campos de antes pela
// mesma via que gravou (o `TagEditor`), o que também passa pela cópia temporária. A capa é o
// único caso em que o arquivo da imagem original é guardado ao lado do diário, porque não há
// como reconstruir um JPEG removido a partir das tags.
//
// Um arquivo JSON por lote, em `<CacheLocation>/autotag/<carimbo>/journal.json`, e os cinco
// últimos lotes ficam disponíveis para desfazer.
#pragma once

#include <QString>
#include <vector>

namespace wamp::ui {

/// Um campo que mudou em uma faixa.
struct JournalField {
  QString field;  ///< "artist", "album", "title", "albumartist", "track", "disc", "year"
  QString before;
  QString after;
};

struct JournalEntry {
  QString path;
  std::vector<JournalField> fields;
  QString coverBackup;   ///< nome do arquivo de capa guardado ao lado (vazio = sem capa)
  QString coverBefore;   ///< mime do que havia antes (vazio = não havia capa)
};

struct JournalBatch {
  QString stamp;     ///< "20261126-193000"
  QString source;    ///< "auto" | "user"
  std::vector<JournalEntry> entries;

  bool empty() const { return entries.empty(); }
};

QString journalToJson(const JournalBatch& batch);
bool journalFromJson(const QString& text, JournalBatch* batch);

/// Grava o lote no diretório informado (cria `<stamp>/journal.json` e os backups de capa que
/// o lote trouxer como caminho local em `coverBackup`).
bool writeJournal(const QString& directory, const JournalBatch& batch, QString* error);

/// Lê o lote mais recente abaixo de `directory` (o nome do diretório é o carimbo, então a
/// ordem alfabética é a ordem cronológica).
bool readLatestJournal(const QString& directory, JournalBatch* batch, QString* error);

/// Apaga os lotes mais antigos, mantendo os `keep` mais recentes.
int pruneJournals(const QString& directory, int keep);

}  // namespace wamp::ui
