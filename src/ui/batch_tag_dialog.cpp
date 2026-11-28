// Implementação do lote do autotag.
//
// Tudo aqui é sequencial e sem threads: o cliente do MusicBrainz já espaça as requisições, e o
// trabalho entre uma consulta e outra é pequeno (ler tags, decidir, gravar). Assim o diálogo
// continua respondendo ao Parar e ao Fechar sem precisar de outro runner.
#include "batch_tag_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

#include <algorithm>

#include "autotag_json.h"
#include "batch_review_dialog.h"
#include "musicbrainz.h"
#include "wamp/tag_cache.h"
#include "wamp/tag_editor.h"
#include "wamp/work_key.h"

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

  // Desligado por padrão enquanto a escolha do lançamento não estiver confiável: o caminho de
  // uma faixa por vez já foi validado e não depende de acertar qual edição é a certa.
  albumBox_ = new QCheckBox(tr("Buscar o disco inteiro — uma consulta por pasta (em teste)"), this);
  albumBox_->setChecked(false);
  // Gancho de teste: a caixa nasce desligada, e clique sintético em diálogo modal não chega
  // (o XWayland entrega ao diálogo só com foco real). A variável liga a caixa para o teste.
  if (qEnvironmentVariableIsSet("AMPLIA_BATCH_ALBUM")) albumBox_->setChecked(true);
  albumBox_->setToolTip(tr("Lê o disco uma vez no MusicBrainz e usa as faixas dele para casar "
                          "com as da pasta. Uma consulta por pasta em vez de uma por faixa."));
  layout->addWidget(albumBox_);

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
  reviewButton_ = new QPushButton(tr("Revisar"), this);
  closeButton_ = new QPushButton(tr("Fechar"), this);
  stopButton_->setEnabled(false);
  undoButton_->setEnabled(false);
  footer->addWidget(startButton_);
  footer->addWidget(stopButton_);
  footer->addWidget(reviewButton_);
  footer->addWidget(undoButton_);
  footer->addStretch(1);
  footer->addWidget(closeButton_);
  layout->addLayout(footer);

  connect(startButton_, &QPushButton::clicked, this, &BatchTagDialog::start);
  connect(stopButton_, &QPushButton::clicked, this, &BatchTagDialog::stop);
  connect(undoButton_, &QPushButton::clicked, this, &BatchTagDialog::undoLast);
  connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
  connect(reviewButton_, &QPushButton::clicked, this, [this] {
    BatchReviewDialog dialog(cache_, this);
    dialog.exec();
  });
  if (cache_ != nullptr) {
    const int pending = cache_->reviewCount();
    reviewButton_->setText(tr("Revisar (%1)").arg(pending));
    reviewButton_->setEnabled(pending > 0);
  } else {
    reviewButton_->setEnabled(false);
  }

  resize(720, 560);
}

QString BatchTagDialog::cacheDirectory() const {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return QDir(base).filePath(QStringLiteral("autotag"));
}

void BatchTagDialog::start() {
  if (running_ || slice_.isEmpty()) return;
  // Faixa que o app já corrigiu e que não mudou desde então não volta para a fila: sem isso,
  // rodar o lote duas vezes na mesma seleção gasta as mesmas 250 consultas de novo.
  if (cache_ != nullptr) {
    std::vector<std::string> paths;
    paths.reserve(slice_.size());
    for (const QString& path : slice_) paths.push_back(path.toStdString());
    const std::vector<std::string> eligible = cache_->notFixedByApp(paths);
    fixed_ = static_cast<int>(paths.size() - eligible.size());
    slice_.clear();
    for (const std::string& path : eligible) slice_ << QString::fromStdString(path);
    if (slice_.isEmpty()) {
      status_->setText(tr("Todas as %1 faixa(s) já foram corrigidas pelo app e não mudaram "
                          "desde então. Nada a fazer.")
                           .arg(fixed_));
      return;
    }
  }
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
  folderTracks_.clear();
  albumMode_ = albumBox_ != nullptr && albumBox_->isChecked();
  if (albumMode_) {
    startAlbumPass();
  } else {
    processNext();
  }
}

void BatchTagDialog::stop() {
  if (!running_) return;
  running_ = false;
  albumPass_ = false;
  expectingTracks_ = false;
  client_->cancel();
  status_->setText(tr("Parado. O que já foi gravado ficou registrado no diário."));
  startButton_->setEnabled(true);
  stopButton_->setEnabled(false);
  closeButton_->setEnabled(true);
}

QString BatchTagDialog::folderOf(const QString& path) {
  return QFileInfo(path).absolutePath();
}

// O nome da pasta costuma carregar o álbum ("2005 - Analogue", "Analogue (Deluxe)"), e o arquivo
// sem tag de álbum não tem outro lugar de onde tirar isso. Sem esse palpite, a busca por disco
// vira "todas as gravações do artista" e traz reedição recente no lugar da original.
static QString albumHintFromFolder(const QString& folder) {
  QString name = QFileInfo(folder).fileName().trimmed();
  if (name.isEmpty()) return name;
  // "2005 - Analogue", "2005-Analogue", "2005 Analogue"
  int i = 0;
  while (i < name.size() && name.at(i).isDigit()) ++i;
  if (i == 4) {
    int j = i;
    while (j < name.size() && (name.at(j) == QLatin1Char(' ') || name.at(j) == QLatin1Char('-') ||
                               name.at(j) == QLatin1Char('.'))) {
      ++j;
    }
    if (j > i && j < name.size()) name = name.mid(j);
  }
  // Parênteses de edição no fim: "(Deluxe Edition)", "(Remastered 2011)".
  const int open = name.lastIndexOf(QLatin1Char('('));
  if (open > 0 && name.endsWith(QLatin1Char(')'))) name = name.left(open).trimmed();
  return name;
}

void BatchTagDialog::startAlbumPass() {
  albumFolders_.clear();
  for (const QString& path : slice_) {
    const QString folder = folderOf(path);
    if (!albumFolders_.contains(folder)) albumFolders_.append(folder);
  }
  albumCursor_ = 0;
  albumPass_ = true;
  lookupNextAlbum();
}

void BatchTagDialog::lookupNextAlbum() {
  if (!running_) return;
  while (albumCursor_ < albumFolders_.size()) {
    const QString folder = albumFolders_.at(albumCursor_);
    // A pasta fala pelo arquivo com mais informação: o primeiro que tiver artista e álbum.
    QString artist;
    QString album;
    for (const QString& path : slice_) {
      if (folderOf(path) != folder) continue;
      wamp::TagFields fields;
      std::string readError;
      if (!wamp::TagEditor::read(path.toStdString(), &fields, nullptr, nullptr, &readError)) continue;
      if (!fields.artist.empty() && !fields.album.empty()) {
        artist = fromStd(fields.artist);
        album = fromStd(fields.album);
        break;
      }
    }
    if (album.isEmpty()) album = albumHintFromFolder(folder);
    if (artist.isEmpty() && album.isEmpty()) {
      // Pasta sem artista nem álbum: não há o que consultar; as faixas vão pelo caminho de
      // uma por uma, que ainda acha por título.
      ++albumCursor_;
      continue;
    }
    currentFolder_ = folder;
    folderTitles_.clear();
    for (const QString& path : slice_) {
      if (folderOf(path) != folder) continue;
      wamp::TagFields fields;
      if (!wamp::TagEditor::read(path.toStdString(), &fields, nullptr, nullptr, nullptr)) continue;
      if (!fields.title.empty()) {
        folderTitles_.append(fromStd(wamp::normalizeWorkField(wamp::WorkField::Title, fields.title)));
      }
    }
    albumBestTracks_.clear();
    albumBestScore_ = -1;
    wamp::TagFields representative;
    for (const QString& path : slice_) {
      if (folderOf(path) != folder) continue;
      if (wamp::TagEditor::read(path.toStdString(), &representative, nullptr, nullptr, nullptr)) break;
    }
    currentAlbumArtist_ = fromStd(representative.artist);
    currentAlbumName_ = fromStd(representative.album);
    if (currentAlbumName_.isEmpty()) currentAlbumName_ = albumHintFromFolder(folder);
    currentAlbumYear_ = representative.year;
    status_->setText(tr("Consultando o disco de %1").arg(QFileInfo(folder).fileName()));
    client_->searchReleases(artist, album);
    return;
  }
  finishAlbumPass();
}

void BatchTagDialog::tryNextRelease() {
  if (!running_) return;
  // Quatro tentativas por pasta é o teto: cobre edições comuns sem transformar uma pasta em
  // uma fila de consultas.
  constexpr int kMaxTrials = 4;
  if (albumTrial_ < std::min(kMaxTrials, static_cast<int>(albumReleaseIds_.size()))) {
    const QString id = albumReleaseIds_.at(albumTrial_++);
    expectingTracks_ = true;
    status_->setText(tr("Abrindo o disco %1 (%2)…").arg(currentAlbumName_).arg(albumTrial_));
    client_->lookupRelease(id);
    return;
  }
  commitAlbumTracks();
}

void BatchTagDialog::commitAlbumTracks() {
  if (!albumBestTracks_.empty() && albumBestScore_ > 0) {
    qInfo("amplia: disco de '%s' aceito com %d de %d título(s)",
          qPrintable(currentAlbumName_), albumBestScore_, static_cast<int>(folderTitles_.size()));
    folderTracks_.insert(currentFolder_, albumBestTracks_);
  } else {
    qInfo("amplia: nenhum lançamento casou com a pasta de '%s'", qPrintable(currentAlbumName_));
  }
  albumBestTracks_.clear();
  albumBestScore_ = -1;
  ++albumCursor_;
  lookupNextAlbum();
}

void BatchTagDialog::finishAlbumPass() {
  albumPass_ = false;
  albumFolders_.clear();
  status_->setText(tr("Discos consultados: %1. Agora as faixas.")
                       .arg(static_cast<int>(folderTracks_.size())));
  processNext();
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

  if (albumMode_) {
    const auto found = folderTracks_.constFind(folderOf(path));
    if (found != folderTracks_.constEnd() && !found->empty()) {
      // As faixas do disco já estão em memória: decide como se a busca tivesse voltado agora,
      // e o casamento por título/duração escolhe a certa dentro do álbum.
      status_->setText(tr("Casando %1 de %2 — %3")
                           .arg(static_cast<int>(index_) + 1)
                           .arg(slice_.size())
                           .arg(QFileInfo(path).fileName()));
      handleCandidates(*found, QString());
      return;
    }
  }
  client_->searchRecordings(query);
}

void BatchTagDialog::handleCandidates(std::vector<wamp::TagCandidate> candidates,
                                      const QString& error) {
  if (!running_) return;

  // Duas respostas passam por aqui durante a varredura de discos: a lista de lançamentos e,
  // depois, as faixas do lançamento escolhido.
  if (albumPass_) {
    if (expectingTracks_) {
      expectingTracks_ = false;
      if (error.isEmpty() && !candidates.empty()) {
        std::vector<std::string> titles;
        titles.reserve(candidates.size());
        for (const auto& candidate : candidates) {
          titles.push_back(wamp::normalizeWorkField(wamp::WorkField::Title, candidate.title));
        }
        int matched = 0;
        for (const QString& title : folderTitles_) {
          const std::string wanted = title.toStdString();
          for (const std::string& have : titles) {
            if (!wanted.empty() && have == wanted) {
              ++matched;
              break;
            }
          }
        }
        if (albumTrial_ == 1) {
          qInfo("amplia: pasta=[%s] disco=[%s]",
                qPrintable(folderTitles_.join(QStringLiteral(" | "))),
                qPrintable(fromStd(candidates.front().title)));
        }
        qInfo("amplia: disco '%s' tem %d faixa(s), %d de %d título(s) da pasta casaram",
              qPrintable(currentAlbumName_), static_cast<int>(candidates.size()), matched,
              static_cast<int>(folderTitles_.size()));
        if (matched > albumBestScore_) {
          albumBestScore_ = matched;
          albumBestTracks_ = candidates;
        }
        if (matched > 0 && matched >= folderTitles_.size()) {
          commitAlbumTracks();
          return;
        }
      }
      tryNextRelease();
      return;
    }
    if (!error.isEmpty() || candidates.empty()) {
      ++albumCursor_;
      lookupNextAlbum();
      return;
    }
    // Abrir um palpite só não basta: o serviço devolve edições, remasters e até disco homônimo de
    // outro artista, e o "primeiro" ou o "mais antigo" erram com facilidade. A escolha é feita
    // **comparando as faixas**: abre até quatro candidatos de mesmo álbum e fica com o que mais
    // casar com os títulos da pasta.
    const std::string wantedAlbum =
        wamp::normalizeWorkField(wamp::WorkField::Album, currentAlbumName_.toStdString());
    // A ordem importa: abrir até quatro lançamentos só serve se os prováveis vierem primeiro.
    // O tamanho do disco é o melhor palpite barato — uma pasta de 13 arquivos é o lançamento de
    // 13 faixas, não o single de 2 nem a edição de 15 com bônus.
    struct Try {
      QString id;
      int trackCount;
    };
    std::vector<Try> tries;
    for (const auto& candidate : candidates) {
      if (candidate.releaseId.empty()) continue;
      if (!wantedAlbum.empty() &&
          wamp::normalizeWorkField(wamp::WorkField::Album, candidate.album) != wantedAlbum) {
        continue;
      }
      tries.push_back({fromStd(candidate.releaseId), candidate.trackCount});
    }
    const int wantedSize = static_cast<int>(folderTitles_.size());
    std::stable_sort(tries.begin(), tries.end(), [wantedSize](const Try& a, const Try& b) {
      const int da = a.trackCount > 0 ? std::abs(a.trackCount - wantedSize) : 1000;
      const int db = b.trackCount > 0 ? std::abs(b.trackCount - wantedSize) : 1000;
      return da < db;
    });
    albumReleaseIds_.clear();
    for (const Try& attempt : tries) albumReleaseIds_.append(attempt.id);
    if (albumReleaseIds_.isEmpty()) {
      for (const auto& candidate : candidates) {
        if (!candidate.releaseId.empty()) albumReleaseIds_.append(fromStd(candidate.releaseId));
      }
    }
    if (albumReleaseIds_.isEmpty()) {
      ++albumCursor_;
      lookupNextAlbum();
      return;
    }
    albumTrial_ = 0;
    tryNextRelease();
    return;
  }

  const QString path = currentPath_;
  if (!error.isEmpty()) {
    // Falha de serviço ou de rede não é resposta: a faixa volta para a fila e é tentada de
    // novo na próxima abertura — jogá-la em "sem resultado" perderia o trabalho.
    addRow(path, tr("%1 — fica para a próxima").arg(error));
    if (cache_ != nullptr) cache_->enqueueAutotag({path.toStdString()});
    deferredPaths_.insert(path);
    ++deferred_;
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
        // Faixa em dúvida: o campo vazio que todas as edições concordam entra sozinho, o resto
        // fica guardado para a revisão humana.
        std::vector<wamp::TagCandidate> ordered;
        ordered.reserve(ranked.size());
        for (const auto& scored : ranked) ordered.push_back(scored.candidate);
        const auto consensus = wamp::consensusFill(query, ordered, ranked.front().score);
        QStringList filled;
        if (consensus.any()) {
          wamp::TagFields incoming;
          // O catálogo usa hífen tipográfico no nome do artista; o arquivo usa o comum.
          incoming.artist = wamp::asciiDashes(ranked.front().candidate.artist);
          incoming.albumArtist = wamp::asciiDashes(ranked.front().candidate.albumArtist);
          incoming.album = ranked.front().candidate.album;
          incoming.title = ranked.front().candidate.title;
          incoming.track = ranked.front().candidate.trackNo;
          incoming.disc = ranked.front().candidate.discNo;
          incoming.year = ranked.front().candidate.year;
          std::vector<wamp::ui::JournalField> changes;
          const wamp::TagFields mergedFields = merged(fields, incoming, consensus, &changes);
          std::string writeError;
          if (!changes.empty() &&
              wamp::TagEditor::write(path.toStdString(), mergedFields, true, &writeError)) {
            if (cache_ != nullptr) {
              cache_->markTagFixed(path.toStdString(),
                                   wamp::fileMtimeMs(path.toStdString()));
            }
            JournalEntry entry;
            entry.path = path;
            entry.fields = std::move(changes);
            journal_.entries.push_back(std::move(entry));
            for (const auto& field : entry.fields) filled.append(field.field);
          }
        }
        if (cache_ != nullptr) {
          wamp::TagCache::ReviewItem item;
          item.path = path.toStdString();
          item.batch = journal_.stamp.toStdString();
          item.candidate = wamp::ui::candidateToText(ranked.front().candidate).toStdString();
          item.state = filled.isEmpty() ? "pendente" : "pendente";
          cache_->saveReviewItems({item});
        }
        addRow(path,
               filled.isEmpty()
                   ? tr("revisar — melhor %1 (%2)")
                         .arg(fromStd(ranked.front().candidate.album),
                              QString::number(ranked.front().score, 'f', 2))
                   : tr("revisar — melhor %1 (%2) · preenchido por consenso: %3")
                         .arg(fromStd(ranked.front().candidate.album),
                              QString::number(ranked.front().score, 'f', 2), filled.join(", ")));
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
  incoming.artist = wamp::asciiDashes(candidate.artist);
  incoming.albumArtist = wamp::asciiDashes(candidate.albumArtist);
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
  if (cache_ != nullptr) {
    cache_->markTagFixed(path.toStdString(), wamp::fileMtimeMs(path.toStdString()));
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

  // Só sai da fila o que foi de fato resolvido: o que ficou para depois continua guardado.
  if (cache_ != nullptr) {
    std::vector<std::string> done;
    done.reserve(slice_.size());
    for (const QString& path : slice_) {
      if (deferredPaths_.contains(path)) continue;
      done.push_back(path.toStdString());
    }
    cache_->dropAutotagPaths(done);
  }

  QString done = tr("Terminou: %1 gravada(s), %2 para revisar, %3 sem resultado, %4 para "
                    "depois.")
                    .arg(ready_)
                    .arg(review_)
                    .arg(ignored_)
                    .arg(deferred_);
  if (fixed_ > 0) {
    done += QStringLiteral(" ");
    done += tr("%1 ignorada(s) (tag corrigida pelo app).").arg(fixed_);
  }
  status_->setText(done);
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
      if (cache_ != nullptr) {
        cache_->markTagFixed(entry.path.toStdString(),
                             wamp::fileMtimeMs(entry.path.toStdString()));
      }
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
