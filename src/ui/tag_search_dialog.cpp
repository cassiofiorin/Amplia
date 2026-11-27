#include "tag_search_dialog.h"

#include "musicbrainz.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace wamp::ui {
namespace {

QString durationText(std::int64_t ms) {
  if (ms <= 0) return QStringLiteral("—");
  const qint64 seconds = ms / 1000;
  return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

TagSearchDialog::TagSearchDialog(const TagQuery& query, QWidget* parent)
    : QDialog(parent), query_(query) {
  setWindowTitle(tr("Buscar tags — Amplia"));
  setModal(true);
  resize(620, 460);

  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(8);

  auto* intro = new QLabel(
      tr("Procure por texto no MusicBrainz. Nada é gravado aqui: a escolha volta para o Alt+3, "
         "que preenche só os campos vazios e grava do jeito de sempre."),
      this);
  intro->setObjectName(QStringLiteral("hint"));
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto* form = new QFormLayout();
  form->setSpacing(6);
  artistEdit_ = new QLineEdit(QString::fromStdString(query_.artist), this);
  titleEdit_ = new QLineEdit(QString::fromStdString(query_.title), this);
  albumEdit_ = new QLineEdit(QString::fromStdString(query_.album), this);
  form->addRow(tr("Artista:"), artistEdit_);
  form->addRow(tr("Título:"), titleEdit_);
  form->addRow(tr("Álbum:"), albumEdit_);
  layout->addLayout(form);

  auto* searchRow = new QHBoxLayout();
  searchButton_ = new QPushButton(tr("Buscar"), this);
  searchButton_->setDefault(true);
  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName(QStringLiteral("hint"));
  statusLabel_->setWordWrap(true);
  searchRow->addWidget(searchButton_);
  searchRow->addWidget(statusLabel_, 1);
  layout->addLayout(searchRow);

  results_ = new QListWidget(this);
  results_->setUniformItemSizes(true);
  layout->addWidget(results_, 1);

  fields_ = new QTableWidget(this);
  fields_->setColumnCount(3);
  fields_->setHorizontalHeaderLabels({tr("Campo"), tr("No arquivo"), tr("Encontrado")});
  fields_->verticalHeader()->setVisible(false);
  fields_->horizontalHeader()->setStretchLastSection(true);
  fields_->setSelectionMode(QAbstractItemView::NoSelection);
  fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fields_->setMinimumHeight(150);
  layout->addWidget(fields_, 1);

  reasonsLabel_ = new QLabel(this);
  reasonsLabel_->setObjectName(QStringLiteral("hint"));
  reasonsLabel_->setWordWrap(true);
  layout->addWidget(reasonsLabel_);

  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  auto* cancel = new QPushButton(tr("Cancelar"), this);
  useButton_ = new QPushButton(tr("Usar selecionada"), this);
  useButton_->setEnabled(false);
  footer->addWidget(cancel);
  footer->addWidget(useButton_);
  layout->addLayout(footer);

  client_ = new MusicBrainzClient(this);
  connect(client_, &MusicBrainzClient::finished, this,
          [this](std::vector<TagCandidate> candidates, QString error) {
            showCandidates(std::move(candidates), error);
          });
  connect(searchButton_, &QPushButton::clicked, this, [this] { startSearch(); });
  connect(titleEdit_, &QLineEdit::returnPressed, this, [this] { startSearch(); });
  connect(artistEdit_, &QLineEdit::returnPressed, this, [this] { startSearch(); });
  connect(results_, &QListWidget::currentRowChanged, this, [this](int) { updateSelection(); });
  connect(results_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
    if (useButton_->isEnabled()) acceptChoice();
  });
  // Enter na lista confirma a escolha: o diálogo inteiro serve para escolher um candidato.
  connect(results_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
    if (useButton_->isEnabled()) acceptChoice();
  });
  connect(useButton_, &QPushButton::clicked, this, [this] { acceptChoice(); });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

  // Abre já buscando: quem chegou aqui veio atrás do resultado, não de um formulário.
  // A busca só acontece quando o botão é acionado: abrir a janela não gasta requisição
  // do MusicBrainz (a API responde 503 quando recebe pedidos em sequência).
  statusLabel_->setText(tr("Ajuste os campos e clique em Buscar."));
}

int TagSearchDialog::fillableFields(const TagQuery& query, const TagCandidate& candidate) {
  // A conta sai do mesmo plano que preenche: se um dia o plano mudar, o número muda junto.
  const wamp::TagFill fill = wamp::planTagFill(query, candidate);
  return static_cast<int>(fill.title) + static_cast<int>(fill.artist) +
         static_cast<int>(fill.album) + static_cast<int>(fill.albumArtist) +
         static_cast<int>(fill.trackNo) + static_cast<int>(fill.discNo) +
         static_cast<int>(fill.year);
}

void TagSearchDialog::startSearch() {
  TagQuery query = query_;
  query.artist = artistEdit_->text().toStdString();
  query.title = titleEdit_->text().toStdString();
  query.album = albumEdit_->text().toStdString();
  if (query.artist.empty() && query.title.empty()) {
    statusLabel_->setText(tr("Preencha o artista ou o título."));
    return;
  }

  scored_.clear();
  results_->clear();
  useButton_->setEnabled(false);
  reasonsLabel_->clear();
  searchButton_->setEnabled(false);
  statusLabel_->setText(tr("Consultando o MusicBrainz…"));
  client_->searchRecordings(query);
}

void TagSearchDialog::showCandidates(std::vector<TagCandidate> candidates, const QString& error) {
  searchButton_->setEnabled(true);
  if (!error.isEmpty()) {
    statusLabel_->setText(tr("Não deu para consultar: %1").arg(error));
    return;
  }
  if (candidates.empty()) {
    statusLabel_->setText(
        tr("Nada encontrado. Isso é resposta: a faixa fica como está."));
    return;
  }

  TagQuery query = query_;
  query.artist = artistEdit_->text().toStdString();
  query.title = titleEdit_->text().toStdString();
  query.album = albumEdit_->text().toStdString();
  scored_ = rankCandidates(query, candidates);

  results_->blockSignals(true);
  for (const ScoredCandidate& item : scored_) {
    const TagCandidate& candidate = item.candidate;
    QString text = QStringLiteral("%1 — %2").arg(QString::fromStdString(candidate.artist),
                                                QString::fromStdString(candidate.title));
    if (!candidate.album.empty()) {
      text += QStringLiteral("  ·  %1").arg(QString::fromStdString(candidate.album));
    }
    if (candidate.year > 0) text += QStringLiteral("  (%1)").arg(candidate.year);
    text += QStringLiteral("  ·  %1  ·  %2").arg(durationText(candidate.durationMs),
                                                 QString::number(item.score, 'f', 2));
    auto* row = new QListWidgetItem(text, results_);
    row->setToolTip(item.reasons.empty() ? tr("sem motivo isolado")
                                         : QString::fromStdString(item.reasons.front()));
  }
  results_->blockSignals(false);

  const TagDecision decision = classifyTagMatch(scored_);
  QString summary = tr("%1 resultado(s).").arg(static_cast<int>(scored_.size()));
  if (decision == TagDecision::AutoApply) {
    summary += QStringLiteral(" ");
    summary += tr("O primeiro bate o suficiente para preencher sem dúvida.");
  } else if (decision == TagDecision::Review) {
    summary += QStringLiteral(" ");
    summary += tr("Confira antes de usar: há mais de um parecido.");
  }
  statusLabel_->setText(summary);

  results_->setCurrentRow(0);
  updateSelection();
}

void TagSearchDialog::updateSelection() {
  const int row = results_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= scored_.size()) {
    useButton_->setEnabled(false);
    reasonsLabel_->clear();
    return;
  }
  const ScoredCandidate& item = scored_.at(static_cast<std::size_t>(row));
  useButton_->setEnabled(true);

  QStringList reasons;
  for (const std::string& reason : item.reasons) {
    reasons << QString::fromStdString(reason);
  }
  QString detail = tr("Margem sobre o segundo: %1").arg(QString::number(item.margin, 'f', 2));
  if (!reasons.isEmpty()) {
    detail += QStringLiteral("  ·  ");
    detail += reasons.join(QStringLiteral(", "));
  }
  fillFieldTable();
  const int fillable = fillableFields(query_, item.candidate);
  detail += QStringLiteral("  ·  ");
  detail += fillable == 0 ? tr("nenhum campo vazio para preencher")
                          : tr("%1 campo(s) vazio(s) a preencher").arg(fillable);
  reasonsLabel_->setText(detail);
}

void TagSearchDialog::fillFieldTable() {
  const int row = results_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= scored_.size()) {
    fields_->setRowCount(0);
    return;
  }
  const wamp::TagCandidate& candidate = scored_.at(static_cast<std::size_t>(row)).candidate;
  struct Row {
    const char* field;
    QString label;
    QString current;
    QString found;
    bool numeric;
  };
  const auto asText = [](const std::string& value) { return QString::fromStdString(value); };
  const std::vector<Row> rows = {
      {"title", tr("Título"), asText(query_.title), asText(candidate.title), false},
      {"artist", tr("Artista"), asText(query_.artist), asText(candidate.artist), false},
      {"album", tr("Álbum"), asText(query_.album), asText(candidate.album), false},
      {"albumartist", tr("Artista do álbum"), {}, asText(candidate.albumArtist), false},
      {"track", tr("Faixa"), query_.trackNo > 0 ? QString::number(query_.trackNo) : QString(),
       candidate.trackNo > 0 ? QString::number(candidate.trackNo) : QString(), true},
      {"disc", tr("Disco"), query_.discNo > 0 ? QString::number(query_.discNo) : QString(),
       candidate.discNo > 0 ? QString::number(candidate.discNo) : QString(), true},
      {"year", tr("Ano"), query_.year > 0 ? QString::number(query_.year) : QString(),
       candidate.year > 0 ? QString::number(candidate.year) : QString(), true},
  };

  fields_->setRowCount(static_cast<int>(rows.size()));
  int visible = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& rowData = rows[i];
    const int line = static_cast<int>(i);
    auto* check = new QTableWidgetItem(rowData.label);
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    // Marcado por padrão só quando o arquivo não tem nada ali: preencher o vazio é seguro;
    // trocar o que existe é decisão de quem está olhando.
    check->setCheckState(rowData.found.isEmpty() ? Qt::Unchecked
                                                 : (rowData.current.trimmed().isEmpty()
                                                        ? Qt::Checked
                                                        : Qt::Unchecked));
    fields_->setItem(line, 0, check);
    fields_->setItem(line, 1, new QTableWidgetItem(rowData.current));
    fields_->setItem(line, 2, new QTableWidgetItem(rowData.found));
    if (!rowData.found.isEmpty()) ++visible;
  }
  fields_->resizeColumnsToContents();
  Q_UNUSED(visible);
}

void TagSearchDialog::acceptChoice() {
  const int row = results_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= scored_.size()) return;
  chosen_ = scored_.at(static_cast<std::size_t>(row)).candidate;
  hasChoice_ = true;

  // O que sai daqui é a lista de campos marcados, não o bloco inteiro.
  static const char* const kFieldNames[] = {"title",  "artist", "album", "albumartist",
                                            "track", "disc",   "year"};
  const QStringList values = {QString::fromStdString(chosen_.title),
                              QString::fromStdString(chosen_.artist),
                              QString::fromStdString(chosen_.album),
                              QString::fromStdString(chosen_.albumArtist),
                              chosen_.trackNo > 0 ? QString::number(chosen_.trackNo) : QString(),
                              chosen_.discNo > 0 ? QString::number(chosen_.discNo) : QString(),
                              chosen_.year > 0 ? QString::number(chosen_.year) : QString()};
  chosenFields_.clear();
  for (int i = 0; i < fields_->rowCount() && i < 7; ++i) {
    const QTableWidgetItem* item = fields_->item(i, 0);
    if (item == nullptr || item->checkState() != Qt::Checked) continue;
    if (values.at(i).isEmpty()) continue;
    chosenFields_.emplace_back(kFieldNames[i], values.at(i).toStdString());
  }
  accept();
}

}  // namespace wamp::ui
