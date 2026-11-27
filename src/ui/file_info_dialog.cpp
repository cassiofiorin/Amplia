#include "file_info_dialog.h"

#include "tag_search_dialog.h"
#include "wamp/autotag.h"

#include <QCheckBox>
#include <QFile>
#include <QKeySequence>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMessageBox>

#include <algorithm>

namespace wamp::ui {
namespace {

QLineEdit* makeEdit(const QString& placeholder = QString()) {
  auto* edit = new QLineEdit();
  if (!placeholder.isEmpty()) edit->setPlaceholderText(placeholder);
  return edit;
}

QString toQ(const std::string& text) { return QString::fromStdString(text); }

std::string toS(const QString& text) { return text.toStdString(); }

QString formatDuration(std::int64_t ms) {
  if (ms <= 0) return QStringLiteral("--:--");
  const std::int64_t seconds = ms / 1000;
  return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

FileInfoDialog::FileInfoDialog(std::vector<std::string> paths, std::size_t index, bool safeSave,
                               QWidget* parent)
    : QDialog(parent), paths_(std::move(paths)), index_(index) {
  setWindowTitle(tr("Informações do arquivo"));
  resize(760, 520);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  // Cabeçalho: caminho do arquivo e navegação entre as faixas da playlist.
  auto* header = new QHBoxLayout();
  pathLabel_ = new QLabel(this);
  pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  pathLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  header->addWidget(pathLabel_, 1);

  auto* previous = new QPushButton(QStringLiteral("<<"), this);
  previous->setFixedWidth(34);
  connect(previous, &QPushButton::clicked, this, [this] { goTo(-1); });
  header->addWidget(previous);

  auto* next = new QPushButton(QStringLiteral(">>"), this);
  next->setFixedWidth(34);
  connect(next, &QPushButton::clicked, this, [this] { goTo(1); });
  header->addWidget(next);
  layout->addLayout(header);

  auto* tabs = new QTabWidget(this);

  // --- Basic Info -----------------------------------------------------------
  auto* basic = new QWidget(tabs);
  auto* basicLayout = new QHBoxLayout(basic);
  basicLayout->setSpacing(10);

  auto* fieldsBox = new QGroupBox(QStringLiteral("Metadados"), basic);
  auto* form = new QFormLayout(fieldsBox);
  form->setLabelAlignment(Qt::AlignRight);

  auto* numbers = new QHBoxLayout();
  trackEdit_ = makeEdit(QStringLiteral("faixa"));
  trackEdit_->setFixedWidth(60);
  discEdit_ = makeEdit(QStringLiteral("disco"));
  discEdit_->setFixedWidth(60);
  bpmEdit_ = makeEdit(QStringLiteral("BPM"));
  bpmEdit_->setFixedWidth(70);
  numbers->addWidget(trackEdit_);
  numbers->addSpacing(8);
  numbers->addWidget(discEdit_);
  numbers->addSpacing(8);
  numbers->addWidget(bpmEdit_);
  numbers->addStretch(1);
  form->addRow(QStringLiteral("Track"), numbers);

  titleEdit_ = makeEdit();
  artistEdit_ = makeEdit();
  albumEdit_ = makeEdit();
  albumArtistEdit_ = makeEdit();
  genreEdit_ = makeEdit();
  commentEdit_ = makeEdit();
  composerEdit_ = makeEdit();
  publisherEdit_ = makeEdit();

  auto* yearGenre = new QHBoxLayout();
  yearEdit_ = makeEdit();
  yearEdit_->setFixedWidth(80);
  yearGenre->addWidget(yearEdit_);
  yearGenre->addSpacing(8);
  yearGenre->addWidget(genreEdit_, 1);

  form->addRow(tr("Título"), titleEdit_);
  form->addRow(tr("Artista"), artistEdit_);
  form->addRow(tr("Álbum"), albumEdit_);
  form->addRow(tr("Artista do álbum"), albumArtistEdit_);
  form->addRow(tr("Ano / Gênero"), yearGenre);
  form->addRow(tr("Comentário"), commentEdit_);
  form->addRow(tr("Compositor"), composerEdit_);
  form->addRow(QStringLiteral("Gravadora"), publisherEdit_);
  basicLayout->addWidget(fieldsBox, 1);

  auto* infoBox = new QGroupBox(tr("Informação do formato"), basic);
  auto* infoLayout = new QVBoxLayout(infoBox);
  formatLabel_ = new QLabel(infoBox);
  formatLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoLayout->addWidget(formatLabel_);
  infoLayout->addSpacing(10);
  auto* gainBox = new QGroupBox(tr("Replay Gain"), infoBox);
  auto* gainLayout = new QVBoxLayout(gainBox);
  replayGainLabel_ = new QLabel(gainBox);
  gainLayout->addWidget(replayGainLabel_);
  infoLayout->addWidget(gainBox);
  // Análise local: leitura apenas. Fica separada do BPM do cabeçalho (que é a tag TBPM,
  // editável) para ninguém achar que o número estimado é gravado no arquivo. As duas
  // primeiras linhas são o que está gravado (TBPM e TKEY); as duas últimas, o estimado.
  auto* analysisBox = new QGroupBox(tr("Análise local"), infoBox);
  auto* analysisLayout = new QVBoxLayout(analysisBox);
  analysisTagBpmLabel_ = new QLabel(analysisBox);
  analysisTagKeyLabel_ = new QLabel(analysisBox);
  analysisBpmLabel_ = new QLabel(analysisBox);
  analysisKeyLabel_ = new QLabel(analysisBox);
  for (QLabel* label : {analysisTagBpmLabel_, analysisTagKeyLabel_, analysisBpmLabel_,
                        analysisKeyLabel_}) {
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  analysisLayout->addWidget(analysisTagBpmLabel_);
  analysisLayout->addWidget(analysisTagKeyLabel_);
  analysisLayout->addSpacing(6);
  analysisLayout->addWidget(analysisBpmLabel_);
  analysisLayout->addWidget(analysisKeyLabel_);
  auto* analysisHint =
      new QLabel(tr("“da tag” vem do arquivo; “percebido” é estimado ao tocar e não é "
                    "gravado nas tags"),
                 analysisBox);
  analysisHint->setObjectName(QStringLiteral("hint"));
  analysisHint->setWordWrap(true);
  analysisLayout->addWidget(analysisHint);
  infoLayout->addWidget(analysisBox);
  infoLayout->addStretch(1);
  basicLayout->addWidget(infoBox, 0);

  tabs->addTab(basic, tr("Basic Info"));

  // --- Artwork --------------------------------------------------------------
  auto* artwork = new QWidget(tabs);
  auto* artworkLayout = new QHBoxLayout(artwork);
  auto* left = new QVBoxLayout();
  pictureList_ = new QListWidget(artwork);
  pictureList_->setFixedWidth(150);
  left->addWidget(pictureList_, 1);
  artworkLayout->addLayout(left);

  auto* center = new QVBoxLayout();
  picturePreview_ = new QLabel(artwork);
  picturePreview_->setAlignment(Qt::AlignCenter);
  picturePreview_->setMinimumSize(260, 260);
  picturePreview_->setText(tr("Sem imagem"));
  center->addWidget(picturePreview_, 1);
  pictureInfo_ = new QLabel(artwork);
  pictureInfo_->setEnabled(false);
  center->addWidget(pictureInfo_);
  artworkLayout->addLayout(center, 1);

  auto* right = new QVBoxLayout();
  auto* change = new QPushButton(tr("Trocar…"), artwork);
  connect(change, &QPushButton::clicked, this, &FileInfoDialog::choosePicture);
  right->addWidget(change);
  auto* saveAs = new QPushButton(tr("Salvar como…"), artwork);
  connect(saveAs, &QPushButton::clicked, this, &FileInfoDialog::savePictureAs);
  right->addWidget(saveAs);
  auto* remove = new QPushButton(tr("Remover"), artwork);
  connect(remove, &QPushButton::clicked, this, &FileInfoDialog::removePicture);
  right->addWidget(remove);
  right->addStretch(1);
  artworkLayout->addLayout(right);

  tabs->addTab(artwork, QStringLiteral("Artwork"));

  setAnalysis(false, 0, -1, 0);
  setTagAnalysis(false, 0, false, -1, 0);
  layout->addWidget(tabs, 1);

  // --- Rodapé ---------------------------------------------------------------
  warningLabel_ = new QLabel(
      QStringLiteral("Atenção: interromper o Amplia ou desligar o computador durante a gravação "
                     "pode corromper as tags do arquivo. Aguarde o fim da gravação."),
      this);
  warningLabel_->setWordWrap(true);
  warningLabel_->setEnabled(false);
  layout->addWidget(warningLabel_);

  auto* footer = new QHBoxLayout();
  safeSaveBox_ = new QCheckBox(tr("Gravação segura (cópia temporária)"), this);
  safeSaveBox_->setChecked(safeSave);
  safeSaveBox_->setToolTip(
      tr("Grava numa cópia, confere o resultado e só então troca o arquivo."));
  footer->addWidget(safeSaveBox_);

  searchButton_ = new QPushButton(tr("Buscar tags…"), this);
  searchButton_->setToolTip(
      tr("Procura esta faixa no MusicBrainz e preenche só os campos que estão vazios."));
  // Ctrl+F é o atalho de busca em qualquer lugar; aqui ele abre o MusicBrainz pela tag.
  searchButton_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
  connect(searchButton_, &QPushButton::clicked, this, &FileInfoDialog::searchTags);
  footer->addWidget(searchButton_);
  footer->addStretch(1);

  auto* cancel = new QPushButton(tr("Cancelar"), this);
  connect(cancel, &QPushButton::clicked, this, &FileInfoDialog::reject);
  footer->addWidget(cancel);

  okButton_ = new QPushButton(QStringLiteral("OK"), this);
  okButton_->setDefault(true);
  connect(okButton_, &QPushButton::clicked, this, [this] {
    if (saveCurrent()) accept();
  });
  footer->addWidget(okButton_);
  layout->addLayout(footer);

  // Aviso de pasta/arquivo somente leitura: fica logo acima do rodapé, para não brigar
  // com o aviso de corrupção, e o OK desabilita enquanto não houver como gravar.
  readonlyLabel_ = new QLabel(this);
  readonlyLabel_->setObjectName(QStringLiteral("readonlyWarning"));
  readonlyLabel_->setWordWrap(true);
  readonlyLabel_->setVisible(false);
  layout->addWidget(readonlyLabel_);

  connect(safeSaveBox_, &QCheckBox::toggled, this, [this] { updateSaveability(); });

  for (QLineEdit* edit : {titleEdit_, artistEdit_, albumEdit_, albumArtistEdit_, yearEdit_,
                          genreEdit_, commentEdit_, composerEdit_, publisherEdit_, trackEdit_,
                          discEdit_, bpmEdit_}) {
    connect(edit, &QLineEdit::textEdited, this, [this] { markDirty(); });
  }
  connect(pictureList_, &QListWidget::currentRowChanged, this, [this](int row) {
    pictureIndex_ = row;
    updatePictures();
  });

  loadCurrent();
  updateSaveability();
}

QString FileInfoDialog::formatKey(int keyPitchClass, int keyMajor) const {
  // Nomes em português e o código Camelot entre parênteses: é o que o pessoal de
  // playlist usa na prática ("Lá menor (8A)").
  static const QStringList names = {
      QStringLiteral("Dó"),   QStringLiteral("Dó♯"),  QStringLiteral("Ré"),
      QStringLiteral("Ré♯"),  QStringLiteral("Mi"),   QStringLiteral("Fá"),
      QStringLiteral("Fá♯"),  QStringLiteral("Sol"),  QStringLiteral("Sol♯"),
      QStringLiteral("Lá"),   QStringLiteral("Lá♯"),  QStringLiteral("Si")};
  static const int kMajorCode[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
  static const int kMinorCode[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
  if (keyPitchClass < 0 || keyPitchClass >= 12) return QStringLiteral("—");
  const int code = keyMajor != 0 ? kMajorCode[keyPitchClass] : kMinorCode[keyPitchClass];
  return tr("%1 %2 (%3%4)")
      .arg(names.at(keyPitchClass), keyMajor != 0 ? tr("maior") : tr("menor"))
      .arg(code)
      .arg(keyMajor != 0 ? QStringLiteral("B") : QStringLiteral("A"));
}

void FileInfoDialog::setAnalysis(bool valid, int bpm, int keyPitchClass, int keyMajor,
                                 int confidence) {
  const bool hasBpm = valid && bpm > 0;
  const QString bpmText = hasBpm ? QString::number(bpm) : QStringLiteral("—");
  analysisBpmLabel_->setText(
      confidence > 0 ? tr("BPM percebido: %1 (confiança %2%)").arg(bpmText).arg(confidence)
                     : tr("BPM percebido: %1").arg(bpmText));
  analysisKeyLabel_->setText(
      tr("Tom percebido: %1").arg(valid ? formatKey(keyPitchClass, keyMajor) : QStringLiteral("—")));
}

void FileInfoDialog::setTagAnalysis(bool hasBpm, int bpm, bool hasKey, int keyPitchClass,
                                    int keyMajor) {
  analysisTagBpmLabel_->setText(
      tr("BPM da tag: %1").arg(hasBpm && bpm > 0 ? QString::number(bpm) : QStringLiteral("—")));
  analysisTagKeyLabel_->setText(
      tr("Tom da tag: %1").arg(hasKey ? formatKey(keyPitchClass, keyMajor) : QStringLiteral("—")));
}

bool FileInfoDialog::safeSaveEnabled() const { return safeSaveBox_->isChecked(); }

bool FileInfoDialog::savingAllowed() const { return okButton_ != nullptr && okButton_->isEnabled(); }

void FileInfoDialog::updateSaveability() {
  if (readonlyLabel_ == nullptr || okButton_ == nullptr || paths_.empty()) return;
  std::string reason;
  const bool allowed = wamp::TagEditor::canSave(paths_.at(index_), safeSaveBox_->isChecked(), &reason);
  okButton_->setEnabled(allowed);
  readonlyLabel_->setText(allowed ? QString()
                                  : QStringLiteral("Somente leitura — %1. As tags deste arquivo "
                                                   "não podem ser salvas.")
                                        .arg(QString::fromStdString(reason)));
  readonlyLabel_->setVisible(!allowed);
}

void FileInfoDialog::markDirty() {
  if (loading_) return;
  dirty_ = true;
  setWindowTitle(tr("Informações do arquivo *"));
}

void FileInfoDialog::loadCurrent() {
  loading_ = true;
  const std::string& path = paths_.at(index_);
  pathLabel_->setText(QString::fromStdString(path));
  pictureChanged_ = false;
  pictureRemoved_ = false;
  newPicture_ = wamp::TagPicture{};

  std::string error;
  const bool ok = wamp::TagEditor::read(path, &fields_, &format_, &pictures_, &error);
  if (!ok) {
    QString message = tr("Não foi possível ler este arquivo: %1")
                          .arg(QString::fromStdString(error));
    formatLabel_->setText(message);
    replayGainLabel_->setText(QStringLiteral("—"));
    loading_ = false;
    return;
  }

  titleEdit_->setText(toQ(fields_.title));
  artistEdit_->setText(toQ(fields_.artist));
  albumEdit_->setText(toQ(fields_.album));
  albumArtistEdit_->setText(toQ(fields_.albumArtist));
  genreEdit_->setText(toQ(fields_.genre));
  commentEdit_->setText(toQ(fields_.comment));
  composerEdit_->setText(toQ(fields_.composer));
  publisherEdit_->setText(toQ(fields_.publisher));
  yearEdit_->setText(fields_.year > 0 ? QString::number(fields_.year) : QString());
  trackEdit_->setText(fields_.track > 0 ? QString::number(fields_.track) : QString());
  discEdit_->setText(fields_.disc > 0 ? QString::number(fields_.disc) : QString());
  bpmEdit_->setText(fields_.bpm > 0 ? QString::number(fields_.bpm) : QString());

  updateFormat();
  updatePictures();
  updateSaveability();

  dirty_ = false;
  setWindowTitle(tr("Informações do arquivo"));
  loading_ = false;
  emit trackChanged(QString::fromStdString(path));
}

void FileInfoDialog::updateFormat() {
  formatLabel_->setText(tr("%1\n%2 kbps • %3 Hz • %4 canal(is)\n%5")
                            .arg(toQ(format_.codec))
                            .arg(format_.bitrate)
                            .arg(format_.sampleRate)
                            .arg(format_.channels)
                            .arg(formatDuration(format_.durationMs)));

  const QString track = format_.replayGainTrack.empty()
                            ? tr("não presente")
                            : toQ(format_.replayGainTrack);
  const QString album = format_.replayGainAlbum.empty()
                            ? tr("não presente")
                            : toQ(format_.replayGainAlbum);
  // Pico junto do ganho: é o que explica por que o ReplayGain não estoura.
  const auto withPeak = [](const QString& gain, const std::string& peak) {
    return peak.empty() ? gain : gain + tr(" (pico %1)").arg(toQ(peak));
  };
  replayGainLabel_->setText(tr("Ganho da faixa: %1\nGanho do álbum: %2")
                                .arg(withPeak(track, format_.replayGainTrackPeak),
                                     withPeak(album, format_.replayGainAlbumPeak)));
}

void FileInfoDialog::updatePictures() {
  pictureList_->clear();
  for (std::size_t i = 0; i < pictures_.size(); ++i) {
    pictureList_->addItem(tr("Imagem %1").arg(i + 1));
  }
  if (pictureRemoved_ && pictures_.empty()) {
    picturePreview_->setText(tr("Sem imagem"));
    picturePreview_->setPixmap(QPixmap());
    pictureInfo_->setText(QString());
    return;
  }

  const wamp::TagPicture* chosen = nullptr;
  if (pictureChanged_) {
    chosen = &newPicture_;
  } else if (pictureIndex_ >= 0 && static_cast<std::size_t>(pictureIndex_) < pictures_.size()) {
    chosen = &pictures_[static_cast<std::size_t>(pictureIndex_)];
  }

  if (chosen == nullptr || chosen->data.empty()) {
    picturePreview_->setPixmap(QPixmap());
    picturePreview_->setText(tr("Sem imagem"));
    pictureInfo_->setText(QString());
    return;
  }

  QImage image;
  image.loadFromData(reinterpret_cast<const uchar*>(chosen->data.data()),
                     static_cast<int>(chosen->data.size()));
  if (image.isNull()) {
    picturePreview_->setPixmap(QPixmap());
    picturePreview_->setText(tr("Imagem embutida (prévia indisponível)"));
  } else {
    picturePreview_->setPixmap(QPixmap::fromImage(image).scaled(
        picturePreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pictureInfo_->setText(
        tr("%1×%2 • %3 • %4 KB")
            .arg(image.width())
            .arg(image.height())
            .arg(toQ(chosen->mime.empty() ? std::string("image") : chosen->mime))
            .arg(chosen->data.size() / 1024));
  }
}

void FileInfoDialog::searchTags() {
  if (paths_.empty()) return;

  wamp::TagQuery query;
  query.artist = artistEdit_->text().toStdString();
  query.album = albumEdit_->text().toStdString();
  query.title = titleEdit_->text().toStdString();
  query.trackNo = trackEdit_->text().toInt();
  query.discNo = discEdit_->text().toInt();
  query.year = yearEdit_->text().toInt();
  query.durationMs = format_.durationMs;

  wamp::ui::TagSearchDialog dialog(query, this);
  if (dialog.exec() != QDialog::Accepted || !dialog.hasChoice()) return;

  // Só o que está vazio: o que a pessoa já escreveu é dela.
  const wamp::TagCandidate& candidate = dialog.chosen();
  const wamp::TagFill fill = wamp::planTagFill(query, candidate);
  const auto put = [](QLineEdit* edit, bool wanted, const std::string& value) {
    if (wanted && !value.empty() && edit->text().trimmed().isEmpty()) {
      edit->setText(QString::fromStdString(value));
    }
  };
  put(artistEdit_, fill.artist, candidate.artist);
  put(albumEdit_, fill.album, candidate.album);
  put(albumArtistEdit_, fill.albumArtist, candidate.albumArtist);
  put(titleEdit_, fill.title, candidate.title);
  if (fill.year && candidate.year > 0 && yearEdit_->text().trimmed().isEmpty()) {
    yearEdit_->setText(QString::number(candidate.year));
  }
  if (fill.trackNo && candidate.trackNo > 0 && trackEdit_->text().trimmed().isEmpty()) {
    trackEdit_->setText(QString::number(candidate.trackNo));
  }
  if (fill.discNo && candidate.discNo > 0 && discEdit_->text().trimmed().isEmpty()) {
    discEdit_->setText(QString::number(candidate.discNo));
  }
  markDirty();
}

void FileInfoDialog::choosePicture() {
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Escolher capa"), QString(),
      tr("Imagens (*.png *.jpg *.jpeg *.webp);;Todos os arquivos (*)"));
  if (file.isEmpty()) return;

  QFile input(file);
  if (!input.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("File Info"),
                         tr("Não foi possível ler a imagem escolhida."));
    return;
  }
  const QByteArray bytes = input.readAll();

  newPicture_ = wamp::TagPicture{};
  newPicture_.data = std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
  newPicture_.mime = file.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? "image/png"
                                                                               : "image/jpeg";
  newPicture_.type = "cover";
  newPicture_.description = QFileInfo(file).fileName().toStdString();
  pictureChanged_ = true;
  pictureRemoved_ = false;
  markDirty();
  updatePictures();
}

void FileInfoDialog::savePictureAs() {
  if (pictures_.empty()) return;
  const std::size_t position =
      pictureIndex_ >= 0 ? static_cast<std::size_t>(pictureIndex_) : 0;
  if (position >= pictures_.size()) return;

  const wamp::TagPicture& picture = pictures_[position];
  const QString suggested =
      QFileInfo(QString::fromStdString(paths_.at(index_))).completeBaseName() +
      (picture.mime == "image/png" ? QStringLiteral(".png") : QStringLiteral(".jpg"));
  const QString target = QFileDialog::getSaveFileName(this, tr("Salvar imagem"),
                                                      suggested, tr("Imagens (*)"));
  if (target.isEmpty()) return;

  QFile out(target);
  if (!out.open(QIODevice::WriteOnly)) {
    QMessageBox::warning(this, tr("File Info"),
                         tr("Não foi possível escrever a imagem."));
    return;
  }
  out.write(picture.data.data(), static_cast<qint64>(picture.data.size()));
}

void FileInfoDialog::removePicture() {
  if (pictures_.empty()) return;
  pictureRemoved_ = true;
  pictureChanged_ = false;
  pictures_.clear();
  markDirty();
  updatePictures();
}

void FileInfoDialog::goTo(int delta) {
  const int target = static_cast<int>(index_) + delta;
  if (target < 0 || target >= static_cast<int>(paths_.size())) return;
  if (!confirmDiscard()) return;
  index_ = static_cast<std::size_t>(target);
  loadCurrent();
}

bool FileInfoDialog::confirmDiscard() {
  if (!dirty_) return true;
  const auto answer = QMessageBox::question(
      this, tr("Alterações não salvas"),
      tr("Descartar as alterações desta faixa?"),
      QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
  return answer == QMessageBox::Discard;
}

bool FileInfoDialog::saveCurrent() {
  fields_.title = toS(titleEdit_->text());
  fields_.artist = toS(artistEdit_->text());
  fields_.album = toS(albumEdit_->text());
  fields_.albumArtist = toS(albumArtistEdit_->text());
  fields_.genre = toS(genreEdit_->text());
  fields_.comment = toS(commentEdit_->text());
  fields_.composer = toS(composerEdit_->text());
  fields_.publisher = toS(publisherEdit_->text());
  fields_.year = yearEdit_->text().toInt();
  fields_.track = trackEdit_->text().toInt();
  fields_.disc = discEdit_->text().toInt();
  fields_.bpm = bpmEdit_->text().toInt();

  const std::string& path = paths_.at(index_);
  const bool safe = safeSaveBox_->isChecked();

  std::string blocked;
  if (!wamp::TagEditor::canSave(path, safe, &blocked)) {
    QMessageBox::warning(this, tr("File Info"),
                         tr("Não dá para salvar este arquivo: %1.")
                             .arg(QString::fromStdString(blocked)));
    updateSaveability();
    return false;
  }

  std::string error;
  if (!wamp::TagEditor::write(path, fields_, safe, &error)) {
    QMessageBox::critical(this, tr("File Info"),
                          tr("Não foi possível gravar as tags: %1")
                              .arg(QString::fromStdString(error)));
    return false;
  }

  if (pictureRemoved_) {
    if (!wamp::TagEditor::removePicture(path, 0, safe, &error)) {
      QMessageBox::critical(this, tr("File Info"),
                            tr("Tags gravadas, mas a imagem não saiu: %1")
                                .arg(QString::fromStdString(error)));
    }
  } else if (pictureChanged_) {
    if (!wamp::TagEditor::replacePicture(path, 0, newPicture_, safe, &error)) {
      QMessageBox::critical(this, tr("File Info"),
                            tr("Tags gravadas, mas a imagem não entrou: %1")
                                .arg(QString::fromStdString(error)));
    }
  }

  dirty_ = false;
  emit fileSaved(QString::fromStdString(path));
  return true;
}

}  // namespace wamp::ui
