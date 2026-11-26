// Teste do diário do autotag: ida e volta do JSON, lote mais recente e poda.
//
// O que importa é que desfazer encontre **o último** lote, que o carimbo ordene como tempo e
// que a poda não apague justamente o lote que a pessoa vai desfazer.

#include "tag_journal.h"

#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("  FALHOU: %s\n", what);
    ++failures;
  }
}

wamp::ui::JournalBatch makeBatch(const QString& stamp, int files) {
  wamp::ui::JournalBatch batch;
  batch.stamp = stamp;
  batch.source = QStringLiteral("auto");
  for (int i = 0; i < files; ++i) {
    wamp::ui::JournalEntry entry;
    entry.path = QStringLiteral("/music/faixa-%1.mp3").arg(i);
    wamp::ui::JournalField field;
    field.field = QStringLiteral("title");
    field.before = QString();
    field.after = QStringLiteral("Título %1").arg(i);
    entry.fields.push_back(field);
    batch.entries.push_back(entry);
  }
  return batch;
}

void testJsonRoundTrip() {
  const wamp::ui::JournalBatch batch = makeBatch(QStringLiteral("20261126-193000"), 3);
  const QString text = wamp::ui::journalToJson(batch);

  wamp::ui::JournalBatch back;
  check(wamp::ui::journalFromJson(text, &back), "JSON volta a virar lote");
  check(back.stamp == batch.stamp, "carimbo sobrevive");
  check(back.source == batch.source, "origem sobrevive");
  check(back.entries.size() == 3, "três faixas sobrevivem");
  check(back.entries.at(1).fields.at(0).after == QStringLiteral("Título 1"), "valor depois sobrevive");
  check(back.entries.at(1).fields.at(0).before.isEmpty(), "valor antes vazio continua vazio");
}

void testLatestAndPrune() {
  QTemporaryDir dir;
  check(dir.isValid(), "diretório temporário criado");
  const QString root = dir.path();

  QString error;
  check(wamp::ui::writeJournal(root, makeBatch(QStringLiteral("20261126-193000"), 1), &error),
        "primeiro lote gravado");
  check(wamp::ui::writeJournal(root, makeBatch(QStringLiteral("20261126-194500"), 2), &error),
        "segundo lote gravado");

  wamp::ui::JournalBatch latest;
  check(wamp::ui::readLatestJournal(root, &latest, &error), "lote mais recente encontrado");
  check(latest.stamp == QStringLiteral("20261126-194500"), "o mais recente é o de 19:45");
  check(latest.entries.size() == 2, "o mais recente tem duas faixas");

  // Cinco lotes ficam; os mais antigos saem.
  for (int i = 0; i < 5; ++i) {
    check(wamp::ui::writeJournal(root, makeBatch(QStringLiteral("2026112%1-120000").arg(i), 1), &error),
          "lote extra gravado");
  }
  const int removed = wamp::ui::pruneJournals(root, 5);
  check(removed > 0, "a poda removeu lote(s) antigo(s)");
  const int left = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
  check(left == 5, "sobraram exatamente cinco lotes");
}

void testBrokenJson() {
  wamp::ui::JournalBatch batch;
  check(!wamp::ui::journalFromJson(QStringLiteral("{ nao é json"), &batch), "JSON quebrado é recusado");
}

}  // namespace

int main() {
  std::printf("tag journal: ida e volta, mais recente e poda\n");
  testJsonRoundTrip();
  testLatestAndPrune();
  testBrokenJson();

  if (failures == 0) {
    std::printf("tag journal: todos os casos passaram\n");
    return 0;
  }
  std::printf("tag journal: %d falha(s)\n", failures);
  return 1;
}
