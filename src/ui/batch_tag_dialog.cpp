// Implementação do lote do autotag.
//
// Tudo aqui é sequencial e sem threads: o cliente do MusicBrainz já espaça as requisições, e o
// trabalho entre uma consulta e outra é pequeno (ler tags, decidir, gravar). Assim o diálogo
// continua respondendo ao Parar e ao Fechar sem precisar de outro runner.
#include "batch_tag_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

#include <algorithm>

#include "musicbrainz.h"
#include "wamp/tag_cache.h"
#include "wamp/tag_editor.h"

namespace wamp::ui {
namespace {

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

/// Junta o que o arquivo tem com o que o candidato oferece, campo a campo, e devolve o bloco
/// pronto para gravar — só os campos vazios mudam.
wamp::TagFields merged(const wamp::TagFields& current, const wamp::TagFields& incoming,
                       const wamp::TagFill& plan, std::vector<wamp::ui::JournalField>* changes) {
  wamp::TagFields out = current;
  auto change = [changes](const char* name, const QString& before, const QString& after) {
    if (before == after) return;
    wamp::ui::JournalField field;
    field.field = QString::fromLatin1(name);
    field.before = before;
    field.after = after;
    changes->push_back(field);
  };
  if (plan.title) {
    change("title", fromStd(out.title), fromStd(incoming.title));
    out.title = incoming.title;
  }
  if (plan.artist) {
    change("artist", fromStd(out.artist), fromStd(incoming.artist));
    out.artist = incoming.artist;
  }
  if (plan.album) {
    change("album", fromStd(out.album), fromStd(incoming.album));
    out.album = incoming.album;
  }
  if (plan.albumArtist) {
    change("albumartist", fromStd(out.albumArtist), fromStd(incoming.albumArtist));
    out.albumArtist = incoming.albumArtist;
  }
  if (plan.trackNo) {
    change("track", QString::number(out.track), QString::number(incoming.track));
    out.track = incoming.track;
  }
  if (plan.discNo) {
    change("disc", QString::number(out.disc), QString::number(incoming.disc));
    out.disc = incoming.disc;
  }
  if (plan.year) {
    change("year", QString::number(out.year), QString::number(incoming.year));
    out.year = incoming.year;
  }
  return out;
}

}  // namespace

BatchTagDialog::BatchTagDialog(wamp::TagCache* cache, const QStringList& paths, QWidget* parent)
    : QDialog(parent), cache_(cache) {
  setWindowTitle(tr("Buscar tags em lote"));
  setModal(true);
  client_ = new MusicBrainzClient(this);
  connect(client_, &MusicBrainzClient::finished, this,
          [this](std::vector<wamp::TagCandidate> candidates, QString error) {
            handleCandidates(std::move(candidates), error);
          });

  // A fila guardada entra depois da seleção: primeiro o que o usuário acabou de pedir.
  QStringList all = paths;
  if (cache_ != nullptr) {
    std::string error;
    const auto queued = cache_->autotagQueue(kBatchLimit * 4, &error);
    for (const auto& path : queued) {
      const QString text = fromStd(path);
      if (!all.contains(text)) all.append(text);
    }
  }

  const int total = static_cast<int>(all.size());
  // Gancho de teste: o limite pode ser reduzido por variável de ambiente para exercitar o
  // caminho do excedente sem precisar de 251 arquivos.
  const int limit = qEnvironmentVariableIntValue("AMPLIA_BATCH_LIMIT") > 0
                        ? qEnvironmentVariableIntValue("AMPLIA_BATCH_LIMIT")
                        : kBatchLimit;
  const int sliceSize = std::min<int>(limit, total);
  const int restSize = total - sliceSize;
  slice_ = all.mid(0, sliceSize);
  rest_ = all.mid(sliceSize);

  // O excedente já vai para a fila persistida antes da primeira consulta: se o app fechar no
  // meio, a próxima abertura sabe o que falta.
  if (cache_ != nullptr && !rest_.isEmpty()) {
    std::vector<std::string> paths_rest;
    paths_rest.reserve(static_cast<std::size_t>(rest_.size()));
    for (const QString& path : rest_) paths_rest.push_back(path.toStdString());
    cache_->enqueueAutotag(paths_rest);
  }

  buildUi(total, sliceSize, restSize);
}

void BatchTagDialog::buildUi(int total, int sliceSize, int restSize) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 16);
  layout->setSpacing(10);

  auto* title = new QLabel(tr("Buscar tags no MusicBrainz"), this);
  title->setObjectName(QStringLiteral("dynamicsTitle"));
  layout->addWidget(title);

  auto* hint = new QLabel(
      tr("Cada faixa é procurada por artista e título, uma consulta por vez. Nada é gravado sem "
         "confiança: campos vazios podem ser preenchidos, o que já existe fica como está."),
      this);
  hint->setObjectName(QStringLiteral("hint"));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  // A frase combinada com o usuário: quantas entram neste lote e o que acontece com o resto.
  // Duas linhas porque são duas informações; "lote" é a palavra que o resto da janela usa.
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  const QString head = tr("O processamento busca %1 de %2 faixas.").arg(sliceSize).arg(total);
  if (restSize > 0) {
    const QString restLine =
        restSize == 1
            ? tr("A 1 faixa restante será processada no próximo lote.")
            : tr("As %1 faixas restantes serão processadas no próximo lote.").arg(restSize);
    summary_->setText(head + QLatin1Char('\n') + restLine);
  } else {
    summary_->setText(head);
  }
  layout->addWidget(summary_);

  progress_ = new QProgressBar(this);
  progress_->setRange(0, std::max(1, sliceSize));
  progress_->setValue(0);
  progress_->setTextVisible(true);
  layout->addWidget(progress_);

  status_ = new QLabel(tr("Pronto para começar."), this);
  status_->setObjectName(QStringLiteral("hint"));
  status_->setWordWrap(true);
  layout->addWidget(status_);

  rows_ = new QListWidget(this);
  rows_->setMinimumHeight(240);
  layout->addWidget(rows_, 1);

  auto* footer = new QHBoxLayout();
  footer->setSpacing(8);
  startButton_ = new QPushButton(tr("Começar"), this);
  stopButton_ = new QPushButton(tr("Parar"), this);
  undoButton_ = new QPushButton(tr("Desfazer último lote"), this);
  closeButton_ = new QPushButton(tr("Fechar"), this);
  stopButton_->setEnabled(false);
  undoButton_->setEnabled(false);
  footer->addWidget(startButton_);
  footer->addWidget(stopButton_);
  footer->addWidget(undoButton_);
  footer->addStretch(1);
  footer->addWidget(closeButton_);
  layout->addLayout(footer);

  connect(startButton_, &QPushButton::clicked, this, &BatchTagDialog::start);
  connect(stopButton_, &QPushButton::clicked, this, &BatchTagDialog::stop);
  connect(undoButton_, &QPushButton::clicked, this, &BatchTagDialog::undoLast);
  connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);

  resize(720, 560);
}

QString BatchTagDialog::cacheDirectory() const {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(base).filePath(QStringLiteral("autotag"));
}

void BatchTagDialog::start() {
  if (running_ || slice_.isEmpty()) return;
  running_ = true;
  index_ = 0;
  ready_ = 0;
  review_ = 0;
  ignored_ = 0;
  rows_->clear();
  journal_ = JournalBatch{};
  journal_.stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  journal_.source = QStringLiteral("auto");
  startButton_->setEnabled(false);
  stopButton_->setEnabled(true);
  closeButton_->setEnabled(false);
  progress_->setValue(0);
  processNext();
}

void BatchTagDialog::stop() {
  if (!running_) return;
  running_ = false;
  client_->cancel();
  status_->setText(tr("Parado. O que já foi gravado ficou registrado no diário."));
  startButton_->setEnabled(true);
  stopButton_->setEnabled(false);
  closeButton_->setEnabled(true);
}

void BatchTagDialog::processNext() {
  if (!running_) return;
  if (index_ >= static_cast<std::size_t>(slice_.size())) {
    finish();
    return;
  }
  const QString path = slice_.at(static_cast<int>(index_));
  status_->setText(tr("Consultando %1 de %2 — %3")
                       .arg(static_cast<int>(index_) + 1)
                       .arg(slice_.size())
                       .arg(QFileInfo(path).fileName()));

  wamp::TagFields fields;
  wamp::TagFormat format;
  std::string error;
  if (!wamp::TagEditor::read(path.toStdString(), &fields, &format, nullptr, &error)) {
    addRow(path, tr("não deu para ler as tags"));
    ++ignored_;
    ++index_;
    progress_->setValue(static_cast<int>(index_));
    processNext();
    return;
  }

  wamp::TagQuery query;
  query.artist = fields.artist;
  query.album = fields.album;
  query.title = fields.title;
  query.trackNo = fields.track;
  query.discNo = fields.disc;
  query.year = fields.year;
  query.durationMs = format.durationMs;

  if (!wamp::tagWorthSearching(query)) {
    addRow(path, tr("já tem artista e título — não precisa"));
    ++ignored_;
    ++index_;
    progress_->setValue(static_cast<int>(index_));
    processNext();
    return;
  }

  currentPath_ = path;
  client_->searchRecordings(query);
}

void BatchTagDialog::handleCandidates(std::vector<wamp::TagCandidate> candidates,
                                      const QString& error) {
  if (!running_) return;
  const QString path = currentPath_;
  if (!error.isEmpty()) {
    addRow(path, error);
    ++ignored_;
  } else if (candidates.empty()) {
    addRow(path, tr("nada encontrado"));
    ++ignored_;
  } else {
    wamp::TagQuery query;
    wamp::TagFields fields;
    wamp::TagFormat format;
    std::string readError;
    if (wamp::TagEditor::read(path.toStdString(), &fields, &format, nullptr, &readError)) {
      query.artist = fields.artist;
      query.album = fields.album;
      query.title = fields.title;
      query.trackNo = fields.track;
      query.discNo = fields.disc;
      query.year = fields.year;
      query.durationMs = format.durationMs;
    }
    const auto ranked = wamp::rankCandidates(query, candidates);
    if (ranked.empty()) {
      addRow(path, tr("nada encontrado"));
      ++ignored_;
    } else {
      const auto decision = wamp::classifyTagMatch(ranked);
      if (decision == wamp::TagDecision::AutoApply) {
        applyCandidate(path, ranked.front().candidate);
      } else if (decision == wamp::TagDecision::Review) {
        addRow(path,
               tr("revisar — melhor %1 (%2)").arg(fromStd(ranked.front().candidate.album),
                                                  QString::number(ranked.front().score, 'f', 2)));
        ++review_;
      } else {
        addRow(path, tr("sem confiança — nada foi gravado"));
        ++ignored_;
      }
    }
  }
  ++index_;
  progress_->setValue(static_cast<int>(index_));
  processNext();
}

void BatchTagDialog::applyCandidate(const QString& path, const wamp::TagCandidate& candidate) {
  wamp::TagFields fields;
  std::string error;
  if (!wamp::TagEditor::read(path.toStdString(), &fields, nullptr, nullptr, &error)) {
    addRow(path, tr("não deu para ler as tags"));
    ++ignored_;
    return;
  }
  wamp::TagQuery query;
  query.artist = fields.artist;
  query.album = fields.album;
  query.title = fields.title;
  query.trackNo = fields.track;
  query.discNo = fields.disc;
  query.year = fields.year;

  const auto plan = wamp::planTagFill(query, candidate);
  if (!plan.any()) {
    addRow(path, tr("nada a preencher"));
    ++ignored_;
    return;
  }

  wamp::TagFields incoming;
  incoming.artist = candidate.artist;
  incoming.albumArtist = candidate.albumArtist;
  incoming.album = candidate.album;
  incoming.title = candidate.title;
  incoming.track = candidate.trackNo;
  incoming.disc = candidate.discNo;
  incoming.year = candidate.year;

  std::vector<wamp::ui::JournalField> changes;
  const wamp::TagFields mergedFields = merged(fields, incoming, plan, &changes);
  if (changes.empty()) {
    addRow(path, tr("nada a preencher"));
    ++ignored_;
    return;
  }
  if (!wamp::TagEditor::write(path.toStdString(), mergedFields, true, &error)) {
    addRow(path, tr("falhou ao gravar: %1").arg(fromStd(error)));
    ++ignored_;
    return;
  }

  JournalEntry entry;
  entry.path = path;
  entry.fields = std::move(changes);
  journal_.entries.push_back(std::move(entry));
  addRow(path, tr("gravada"));
  ++ready_;
  undoButton_->setEnabled(true);
}

void BatchTagDialog::addRow(const QString& path, const QString& verdict) {
  rows_->addItem(QStringLiteral("%1 — %2").arg(QFileInfo(path).fileName(), verdict));
  rows_->scrollToBottom();
}

void BatchTagDialog::finish() {
  running_ = false;
  startButton_->setEnabled(true);
  stopButton_->setEnabled(false);
  closeButton_->setEnabled(true);

  if (!journal_.entries.empty()) {
    QString error;
    if (!writeJournal(cacheDirectory(), journal_, &error)) {
      status_->setText(tr("Terminou com %1 gravada(s), mas o diário falhou: %2")
                           .arg(ready_)
                           .arg(error));
      return;
    }
    pruneJournals(cacheDirectory(), 5);
    undoButton_->setEnabled(true);
  }

  // O que foi processado sai da fila persistida; o que sobrou para a próxima rodada continua lá.
  if (cache_ != nullptr) {
    std::vector<std::string> done;
    done.reserve(slice_.size());
    for (const QString& path : slice_) done.push_back(path.toStdString());
    cache_->dropAutotagPaths(done);
  }

  status_->setText(tr("Terminou: %1 gravada(s), %2 para revisar, %3 sem resultado.")
                       .arg(ready_)
                       .arg(review_)
                       .arg(ignored_));
}

void BatchTagDialog::undoLast() {
  JournalBatch batch;
  QString error;
  if (!readLatestJournal(cacheDirectory(), &batch, &error)) {
    QMessageBox::information(this, tr("Desfazer"),
                             tr("Não há lote para desfazer: %1").arg(error));
    return;
  }
  if (QMessageBox::question(this, tr("Desfazer último lote"),
                            tr("Devolver as tags de %1 faixa(s) ao que eram antes?")
                                .arg(batch.entries.size())) != QMessageBox::Yes) {
    return;
  }
  int restored = 0;
  int failed = 0;
  for (const auto& entry : batch.entries) {
    wamp::TagFields fields;
    std::string readError;
    if (!wamp::TagEditor::read(entry.path.toStdString(), &fields, nullptr, nullptr, &readError)) {
      ++failed;
      continue;
    }
    for (const auto& change : entry.fields) {
      const QByteArray name = change.field.toLatin1();
      if (name == "title") fields.title = change.before.toStdString();
      else if (name == "artist") fields.artist = change.before.toStdString();
      else if (name == "album") fields.album = change.before.toStdString();
      else if (name == "albumartist") fields.albumArtist = change.before.toStdString();
      else if (name == "track") fields.track = change.before.toInt();
      else if (name == "disc") fields.disc = change.before.toInt();
      else if (name == "year") fields.year = change.before.toInt();
    }
    std::string writeError;
    if (wamp::TagEditor::write(entry.path.toStdString(), fields, true, &writeError)) {
      ++restored;
    } else {
      ++failed;
    }
  }
  if (failed == 0) {
    status_->setText(tr("Desfeito: %1 faixa(s) voltaram ao que eram.").arg(restored));
  } else {
    status_->setText(tr("Desfeito em %1 faixa(s); %2 não puderam ser restauradas.")
                         .arg(restored)
                         .arg(failed));
  }
}

}  // namespace wamp::ui
