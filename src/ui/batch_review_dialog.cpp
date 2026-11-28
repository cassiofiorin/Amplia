// Implementação da revisão do lote.
#include "batch_review_dialog.h"

#include <QCheckBox>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

#include "autotag_json.h"
#include "tag_journal.h"
#include "translator.h"
#include "wamp/tag_cache.h"
#include "wamp/work_key.h"
#include "wamp/tag_editor.h"

namespace wamp::ui {
namespace {

struct FieldDef {
  const char* key;      ///< chave do diário
  const char* label;    ///< rótulo na tela
  bool wamp::TagFill::*fill;  ///< qual bit do plano corresponde
};

const FieldDef kFields[] = {
    {"title", "Título", &wamp::TagFill::title},
    {"artist", "Artista", &wamp::TagFill::artist},
    {"album", "Álbum", &wamp::TagFill::album},
    {"albumartist", "Artista do álbum", &wamp::TagFill::albumArtist},
    {"track", "Faixa", &wamp::TagFill::trackNo},
    {"disc", "Disco", &wamp::TagFill::discNo},
    {"year", "Ano", &wamp::TagFill::year},
};

QString text(const wamp::TagFields& fields, const QString& key) {
  if (key == "title") return QString::fromStdString(fields.title);
  if (key == "artist") return QString::fromStdString(fields.artist);
  if (key == "album") return QString::fromStdString(fields.album);
  if (key == "albumartist") return QString::fromStdString(fields.albumArtist);
  if (key == "track") return fields.track > 0 ? QString::number(fields.track) : QString();
  if (key == "disc") return fields.disc > 0 ? QString::number(fields.disc) : QString();
  if (key == "year") return fields.year > 0 ? QString::number(fields.year) : QString();
  return QString();
}

QString candidateText(const wamp::TagCandidate& candidate, const QString& key) {
  if (key == "title") return QString::fromStdString(candidate.title);
  if (key == "artist") return QString::fromStdString(wamp::asciiDashes(candidate.artist));
  if (key == "album") return QString::fromStdString(candidate.album);
  if (key == "albumartist") return QString::fromStdString(wamp::asciiDashes(candidate.albumArtist));
  if (key == "track") return candidate.trackNo > 0 ? QString::number(candidate.trackNo) : QString();
  if (key == "disc") return candidate.discNo > 0 ? QString::number(candidate.discNo) : QString();
  if (key == "year") return candidate.year > 0 ? QString::number(candidate.year) : QString();
  return QString();
}

}  // namespace

BatchReviewDialog::BatchReviewDialog(wamp::TagCache* cache, QWidget* parent)
    : QDialog(parent), cache_(cache) {
  setWindowTitle(tr("Revisar lote"));
  setModal(true);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 14);
  layout->setSpacing(10);

  auto* title = new QLabel(tr("Revisar lote"), this);
  title->setObjectName(QStringLiteral("dynamicsTitle"));
  layout->addWidget(title);

  warning_ = new QLabel(
      tr("Faixas sem tags terão os dados preenchidos mesmo que não estejam marcadas — apenas "
         "quando a compatibilidade da tag for alta."),
      this);
  warning_->setObjectName(QStringLiteral("hint"));
  warning_->setWordWrap(true);
  layout->addWidget(warning_);

  scroll_ = new QScrollArea(this);
  scroll_->setWidgetResizable(true);
  auto* holder = new QWidget(scroll_);
  cardsLayout_ = new QVBoxLayout(holder);
  cardsLayout_->setContentsMargins(0, 0, 6, 0);
  cardsLayout_->setSpacing(10);
  scroll_->setWidget(holder);
  layout->addWidget(scroll_, 1);

  auto* footer = new QHBoxLayout();
  footer->setSpacing(8);
  prev_ = new QPushButton(tr("Anterior"), this);
  next_ = new QPushButton(tr("Próxima"), this);
  pageLabel_ = new QLabel(this);
  pageLabel_->setObjectName(QStringLiteral("hint"));
  applyButton_ = new QPushButton(tr("Aplicar marcadas"), this);
  saveNext_ = new QPushButton(tr("Salvar e avançar"), this);
  close_ = new QPushButton(tr("Fechar"), this);
  footer->addWidget(prev_);
  footer->addWidget(next_);
  footer->addWidget(pageLabel_);
  footer->addStretch(1);
  footer->addWidget(applyButton_);
  footer->addWidget(saveNext_);
  footer->addWidget(close_);
  layout->addLayout(footer);

  connect(prev_, &QPushButton::clicked, this, [this] { setPage(std::max(0, page_ - 1)); });
  connect(next_, &QPushButton::clicked, this, [this] { setPage(page_ + 1); });
  connect(applyButton_, &QPushButton::clicked, this, [this] { applyPage(false); });
  connect(saveNext_, &QPushButton::clicked, this, [this] { applyPage(true); });
  connect(close_, &QPushButton::clicked, this, &QDialog::accept);

  resize(980, 700);

  QSettings settings(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
  page_ = std::max(0, settings.value(QStringLiteral("autotag/reviewPage"), 0).toInt());
  loadPage();

  // Gancho de teste: marca a primeira faixa da página e grava, para exercitar o caminho de
  // escrita sem depender de clique (o ponteiro desta sessão às vezes fica preso).
  if (qEnvironmentVariableIsSet("AMPLIA_REVIEW_MARK")) {
    QTimer::singleShot(500, this, [this] {
      if (cards_.empty()) return;
      for (auto& card : cards_) card.rowBox->setChecked(true);
      applyPage(false);
      QTimer::singleShot(3000, this, [this] { accept(); });
    });
  }
}

QString BatchReviewDialog::settingsKey() const { return QStringLiteral("autotag/reviewPage"); }

void BatchReviewDialog::loadPage() {
  cards_.clear();
  while (QLayoutItem* item = cardsLayout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) widget->deleteLater();
    delete item;
  }
  if (cache_ == nullptr) return;

  total_ = cache_->reviewCount();
  std::string error;
  const auto items = cache_->reviewItems(kPageSize, page_ * kPageSize, &error);

  for (const auto& item : items) {
    const QString path = QString::fromStdString(item.path);
    const wamp::TagCandidate candidate = candidateFromText(QString::fromStdString(item.candidate));

    wamp::TagFields current;
    std::string readError;
    wamp::TagEditor::read(path.toStdString(), &current, nullptr, nullptr, &readError);

  
    auto* frame = new QFrame(scroll_->widget());
    frame->setObjectName(QStringLiteral("queuePanel"));
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(12, 10, 12, 10);
    frameLayout->setSpacing(8);

    auto* header = new QHBoxLayout();
    header->setSpacing(8);
    auto* rowBox = new QCheckBox(QFileInfo(path).fileName(), frame);
    rowBox->setToolTip(tr("Marcar esta faixa marca todos os campos abaixo."));
    header->addWidget(rowBox);
    auto* verdict = new QLabel(item.state == "marcada" ? tr("marcada") : tr("pendente"), frame);
    verdict->setObjectName(QStringLiteral("hint"));
    header->addWidget(verdict);
    header->addStretch(1);
    frameLayout->addLayout(header);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    auto* leftHead = new QLabel(tr("No arquivo"), frame);
    leftHead->setObjectName(QStringLiteral("hint"));
    auto* rightHead = new QLabel(tr("Encontrado no MusicBrainz"), frame);
    rightHead->setObjectName(QStringLiteral("hint"));
    grid->addWidget(leftHead, 0, 0);
    grid->addWidget(rightHead, 0, 1);
    grid->addWidget(new QLabel(QString(), frame), 0, 2);

    int row = 1;
    Card card;
    card.path = path;
    card.candidate = jsonFromCandidate(candidate);
    card.rowBox = rowBox;
    card.verdict = verdict;

    for (const auto& field : kFields) {
      const QString key = QString::fromLatin1(field.key);
      const QString fromFile = text(current, key);
      const QString fromCatalog = candidateText(candidate, key);
      const bool differs = fromFile != fromCatalog && !fromCatalog.isEmpty();
      if (!differs) continue;

      auto* label = new QLabel(QString::fromUtf8(field.label), frame);
      auto* left = new QLabel(fromFile.isEmpty() ? tr("(vazio)") : fromFile, frame);
      if (fromFile.isEmpty()) left->setObjectName(QStringLiteral("hint"));
      auto* right = new QLabel(fromCatalog, frame);
      auto* box = new QCheckBox(frame);
      // Nada vem marcado: o que era consenso já entrou no lote, antes de chegar aqui.
      box->setChecked(false);
      box->setToolTip(tr("Só grava se você marcar."));

      grid->addWidget(label, row, 0);
      grid->addWidget(left, row, 1);
      grid->addWidget(right, row, 2);
      grid->addWidget(box, row, 3);
      card.fields.push_back({key, box});
      ++row;
    }
    frameLayout->addLayout(grid);
    cardsLayout_->addWidget(frame);
    cards_.push_back(card);

  }

  // Marcar a linha marca todos os campos. Feito depois de a lista estar completa: o
  // ponteiro para o cartão só é estável quando o vetor não cresce mais.
  for (std::size_t i = 0; i < cards_.size(); ++i) {
    connect(cards_[i].rowBox, &QCheckBox::toggled, this, [this, i](bool checked) {
      for (auto& field : cards_[i].fields) field.second->setChecked(checked);
    });
  }

  if (cards_.isEmpty()) {
    auto* empty = new QLabel(tr("Nada para revisar."), scroll_->widget());
    empty->setObjectName(QStringLiteral("hint"));
    cardsLayout_->addWidget(empty);
  }
  cardsLayout_->addStretch(1);

  pageLabel_->setText(tr("página %1 de %2 · %3 faixa(s) em revisão")
                          .arg(page_ + 1)
                          .arg(std::max(1, (total_ + kPageSize - 1) / kPageSize))
                          .arg(total_));
  prev_->setEnabled(page_ > 0);
  next_->setEnabled((page_ + 1) * kPageSize < total_);
  applyButton_->setEnabled(!cards_.isEmpty());
  saveNext_->setEnabled(!cards_.isEmpty());
}

void BatchReviewDialog::setPage(int page) {
  page_ = page;
  QSettings settings(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("autotag/reviewPage"), page_);
  loadPage();
}

void BatchReviewDialog::applyPage(bool advance) {
  if (cache_ == nullptr || cards_.isEmpty()) return;

  JournalBatch batch;
  batch.stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  batch.source = QStringLiteral("user");

  std::vector<std::string> written;
  QStringList failures;
  int count = 0;
  for (const auto& card : cards_) {
    wamp::TagFields current;
    std::string error;
    if (!wamp::TagEditor::read(card.path.toStdString(), &current, nullptr, nullptr, &error)) {
      failures.append(QFileInfo(card.path).fileName());
      continue;
    }
    const wamp::TagCandidate candidate = candidateFromJson(card.candidate);
    wamp::TagFields out = current;
    std::vector<JournalField> changes;
    for (const auto& field : card.fields) {
      if (!field.second->isChecked()) continue;
      const QString key = field.first;
      auto change = [&](const QString& before, const QString& after) {
        if (before == after) return;
        JournalField entry;
        entry.field = key;
        entry.before = before;
        entry.after = after;
        changes.push_back(entry);
      };
      if (key == "title") {
        change(QString::fromStdString(out.title), QString::fromStdString(candidate.title));
        out.title = candidate.title;
      } else if (key == "artist") {
        change(QString::fromStdString(out.artist),
               QString::fromStdString(wamp::asciiDashes(candidate.artist)));
        out.artist = wamp::asciiDashes(candidate.artist);
      } else if (key == "album") {
        change(QString::fromStdString(out.album), QString::fromStdString(candidate.album));
        out.album = candidate.album;
      } else if (key == "albumartist") {
        change(QString::fromStdString(out.albumArtist),
               QString::fromStdString(wamp::asciiDashes(candidate.albumArtist)));
        out.albumArtist = wamp::asciiDashes(candidate.albumArtist);
      } else if (key == "track") {
        change(QString::number(out.track), QString::number(candidate.trackNo));
        out.track = candidate.trackNo;
      } else if (key == "disc") {
        change(QString::number(out.disc), QString::number(candidate.discNo));
        out.disc = candidate.discNo;
      } else if (key == "year") {
        change(QString::number(out.year), QString::number(candidate.year));
        out.year = candidate.year;
      }
    }
    if (changes.empty()) continue;
    std::string writeError;
    if (wamp::TagEditor::write(card.path.toStdString(), out, true, &writeError)) {
      if (cache_ != nullptr) {
        cache_->markTagFixed(card.path.toStdString(),
                             wamp::fileMtimeMs(card.path.toStdString()));
      }
    } else {
      failures.append(QFileInfo(card.path).fileName());
      qInfo("amplia: falha ao gravar %s: %s", qPrintable(QFileInfo(card.path).fileName()),
            writeError.c_str());
      continue;
    }
    JournalEntry entry;
    entry.path = card.path;
    entry.fields = std::move(changes);
    batch.entries.push_back(std::move(entry));
    written.push_back(card.path.toStdString());
    ++count;
  }

  if (!batch.entries.empty()) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString directory = QDir(base).filePath(QStringLiteral("autotag"));
    QString error;
    if (!writeJournal(directory, batch, &error)) {
      warning_->setText(tr("Gravou %1 faixa(s), mas o diário falhou: %2").arg(count).arg(error));
    } else {
      pruneJournals(directory, 5);
      warning_->setText(tr("%1 faixa(s) gravadas.").arg(count));
    }
  } else {
    warning_->setText(tr("Nada marcado nesta página — nada foi gravado."));
  }
  if (!failures.isEmpty()) {
    warning_->setText(warning_->text() + " " +
                      tr("%1 faixa(s) não puderam ser gravadas: %2")
                          .arg(failures.size())
                          .arg(failures.mid(0, 3).join(", ")));
  }

  qInfo("amplia: revisão gravou %d faixa(s), %d falha(s)", count, failures.size());
  if (!written.empty()) cache_->dropReviewItems(written);

  if (advance && (page_ + 1) * kPageSize < total_) {
    setPage(page_ + 1);
  } else {
    loadPage();
  }
}


int BatchReviewDialog::undoLastBatch(wamp::TagCache* cache, QWidget* parent, bool confirm) {
  (void)cache;
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  const QString directory = QDir(base).filePath(QStringLiteral("autotag"));
  JournalBatch batch;
  QString error;
  if (!readLatestJournal(directory, &batch, &error)) {
    if (confirm) {
      QMessageBox::information(parent, tr("Desfazer"),
                               tr("Não há lote para desfazer: %1").arg(error));
    }
    return 0;
  }
  if (confirm && QMessageBox::question(parent, tr("Desfazer último lote"),
                                       tr("Devolver as tags de %1 faixa(s) ao que eram antes?")
                                           .arg(batch.entries.size())) != QMessageBox::Yes) {
    return 0;
  }
  int restored = 0;
  int failed = 0;
  qInfo("amplia: desfazendo lote %s (%d entrada(s))", qPrintable(batch.stamp),
        static_cast<int>(batch.entries.size()));
  for (const auto& entry : batch.entries) {
    wamp::TagFields fields;
    std::string readError;
    if (!wamp::TagEditor::read(entry.path.toStdString(), &fields, nullptr, nullptr, &readError)) {
      qInfo("amplia: desfazer não leu %s: %s", qPrintable(entry.path), readError.c_str());
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
      qInfo("amplia: desfeito %s (%d campo(s))", qPrintable(entry.path),
            static_cast<int>(entry.fields.size()));
    } else {
      ++failed;
      qInfo("amplia: desfazer falhou em %s: %s", qPrintable(entry.path), writeError.c_str());
    }
  }
  if (confirm) {
    QMessageBox::information(parent, tr("Desfazer último lote"),
                             failed == 0 ? tr("Desfeito: %1 faixa(s) voltaram ao que eram.")
                                               .arg(restored)
                                         : tr("Desfeito em %1 faixa(s); %2 não puderam ser "
                                              "restauradas.")
                                               .arg(restored)
                                               .arg(failed));
  }
  return restored;
}

}  // namespace wamp::ui
