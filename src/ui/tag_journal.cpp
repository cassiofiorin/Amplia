#include "tag_journal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace wamp::ui {
namespace {

QJsonObject fieldToJson(const JournalField& field) {
  QJsonObject object;
  object.insert(QStringLiteral("field"), field.field);
  object.insert(QStringLiteral("before"), field.before);
  object.insert(QStringLiteral("after"), field.after);
  return object;
}

JournalField fieldFromJson(const QJsonObject& object) {
  JournalField field;
  field.field = object.value(QStringLiteral("field")).toString();
  field.before = object.value(QStringLiteral("before")).toString();
  field.after = object.value(QStringLiteral("after")).toString();
  return field;
}

QJsonObject entryToJson(const JournalEntry& entry) {
  QJsonObject object;
  object.insert(QStringLiteral("path"), entry.path);
  QJsonArray fields;
  for (const JournalField& field : entry.fields) fields.append(fieldToJson(field));
  object.insert(QStringLiteral("fields"), fields);
  if (!entry.coverBackup.isEmpty()) object.insert(QStringLiteral("cover_backup"), entry.coverBackup);
  if (!entry.coverBefore.isEmpty()) object.insert(QStringLiteral("cover_before"), entry.coverBefore);
  return object;
}

JournalEntry entryFromJson(const QJsonObject& object) {
  JournalEntry entry;
  entry.path = object.value(QStringLiteral("path")).toString();
  for (const QJsonValue& value : object.value(QStringLiteral("fields")).toArray()) {
    entry.fields.push_back(fieldFromJson(value.toObject()));
  }
  entry.coverBackup = object.value(QStringLiteral("cover_backup")).toString();
  entry.coverBefore = object.value(QStringLiteral("cover_before")).toString();
  return entry;
}

/// Carimbos no formato AAAAMMDD-HHMMSS, que ordena como texto.
QString makeStamp() {
  return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

}  // namespace

QString journalToJson(const JournalBatch& batch) {
  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("stamp"), batch.stamp);
  root.insert(QStringLiteral("source"), batch.source);
  QJsonArray entries;
  for (const JournalEntry& entry : batch.entries) entries.append(entryToJson(entry));
  root.insert(QStringLiteral("entries"), entries);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool journalFromJson(const QString& text, JournalBatch* batch) {
  if (batch == nullptr) return false;
  const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
  if (!document.isObject()) return false;
  const QJsonObject root = document.object();
  batch->stamp = root.value(QStringLiteral("stamp")).toString();
  batch->source = root.value(QStringLiteral("source")).toString();
  batch->entries.clear();
  for (const QJsonValue& value : root.value(QStringLiteral("entries")).toArray()) {
    batch->entries.push_back(entryFromJson(value.toObject()));
  }
  return true;
}

bool writeJournal(const QString& directory, const JournalBatch& batch, QString* error) {
  JournalBatch copy = batch;
  if (copy.stamp.isEmpty()) copy.stamp = makeStamp();

  const QDir root(directory);
  if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
    if (error != nullptr) *error = QStringLiteral("não foi possível criar %1").arg(directory);
    return false;
  }

  const QString folder = root.filePath(copy.stamp);
  if (!QDir().mkpath(folder)) {
    if (error != nullptr) *error = QStringLiteral("não foi possível criar %1").arg(folder);
    return false;
  }

  // Backup de capa: copia para junto do diário, para que desfazer funcione mesmo que o arquivo
  // original tenha sido trocado depois.
  for (int i = 0; i < static_cast<int>(copy.entries.size()); ++i) {
    JournalEntry& entry = copy.entries.at(static_cast<std::size_t>(i));
    if (entry.coverBackup.isEmpty()) continue;
    const QString source = entry.coverBackup;
    const QFileInfo info(source);
    const QString target = QDir(folder).filePath(
        QStringLiteral("%1-cover.%2").arg(i).arg(info.suffix()));
    if (QFile::exists(source) && QFile::copy(source, target)) {
      entry.coverBackup = target;
    } else {
      entry.coverBackup.clear();
    }
  }

  QSaveFile file(QDir(folder).filePath(QStringLiteral("journal.json")));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) *error = QStringLiteral("não foi possível abrir o diário");
    return false;
  }
  file.write(journalToJson(copy).toUtf8());
  if (!file.commit()) {
    if (error != nullptr) *error = QStringLiteral("não foi possível gravar o diário");
    return false;
  }
  return true;
}

bool readLatestJournal(const QString& directory, JournalBatch* batch, QString* error) {
  if (batch == nullptr) return false;
  const QDir root(directory);
  const QStringList folders =
      root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
  for (const QString& folder : folders) {
    const QString path = root.filePath(folder + QStringLiteral("/journal.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    const QString text = QString::fromUtf8(file.readAll());
    if (!journalFromJson(text, batch)) continue;
    if (batch->stamp.isEmpty()) batch->stamp = folder;
    return true;
  }
  if (error != nullptr) *error = QStringLiteral("nenhum lote para desfazer");
  return false;
}

int pruneJournals(const QString& directory, int keep) {
  const QDir root(directory);
  const QStringList folders =
      root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
  int removed = 0;
  for (int i = keep; i < folders.size(); ++i) {
    QDir folder(root.filePath(folders.at(i)));
    if (folder.removeRecursively()) ++removed;
  }
  return removed;
}

}  // namespace wamp::ui
