#include "library_window.h"

#include <QCursor>
#include <QHeaderView>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QProgressBar>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace wamp::ui {

namespace {

/// Papel do item da árvore: guarda o `wamp::LibraryView` da visão.
constexpr int kViewRole = Qt::UserRole;
/// Índice do preset de humor no vetor de presets (filhos do nó "Humor").
constexpr int kPresetRole = Qt::UserRole + 1;

struct ViewDef {
  const char* name;
  wamp::LibraryView view;
};

const ViewDef kViews[] = {
    // Rótulos curtos de propósito: a árvore tem 210 px e os nomes longos do original
    // ("Adicionadas recentemente") não cabem. A janela de cada visão vai no parêntese.
    {"Todas as faixas", wamp::LibraryView::All},
    {"Adicionadas (3 dias)", wamp::LibraryView::RecentlyAdded},
    {"Modificadas (3 dias)", wamp::LibraryView::RecentlyModified},
    {"Tocadas (2 semanas)", wamp::LibraryView::RecentlyPlayed},
    {"Nunca tocadas", wamp::LibraryView::NeverPlayed},
    {"Mais tocadas", wamp::LibraryView::MostPlayed},
    {"Melhor avaliadas", wamp::LibraryView::TopRated},
};

QString durationText(qint64 ms) {
  if (ms <= 0) return QString();
  const qint64 seconds = ms / 1000;
  return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

/// Item que ordena pelo número guardado em `Qt::UserRole` quando existe: sem isso a
/// coluna de estrelas ordenaria pelo texto ("★" < "★★") e a de duração por "3:05" > "10:02".
class NumericItem final : public QTableWidgetItem {
 public:
  explicit NumericItem(const QString& text) : QTableWidgetItem(text) {}

  bool operator<(const QTableWidgetItem& other) const override {
    const QVariant mine = data(Qt::UserRole);
    const QVariant theirs = other.data(Qt::UserRole);
    if (mine.isValid() && theirs.isValid()) return mine.toDouble() < theirs.toDouble();
    return QTableWidgetItem::operator<(other);
  }
};

QString starsText(int rating) {
  if (rating <= 0) return QStringLiteral("—");
  return QString(rating, QChar(0x2605));  // ★ repetida; o banco guarda só o número
}

}  // namespace

LibraryWindow::LibraryWindow(QWidget* parent) : QWidget(parent, Qt::Tool) {
  setWindowTitle(tr("Biblioteca — Amplia"));
  resize(1120, 620);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(6);

  // As ações de manutenção ficam num menu, como na janela principal: quem quer
  // adicionar pasta ou limpar a biblioteca procura no lugar de sempre, e o rodapé deixa
  // de parecer uma fileira de botões pedindo clique.
  auto* menuBar = new QMenuBar(this);
  auto* optionsMenu = menuBar->addMenu(tr("Opções"));
  auto* addFolderAction = optionsMenu->addAction(tr("Adicionar pasta…"));
  auto* refreshAction = optionsMenu->addAction(tr("Atualizar"));
  removeAction_ = optionsMenu->addAction(tr("Remover selecionadas"));
  removeAction_->setShortcut(QKeySequence::Delete);
  removeAction_->setEnabled(false);
  optionsMenu->addSeparator();
  auto* clearAction = optionsMenu->addAction(tr("Limpar biblioteca…"));
  connect(addFolderAction, &QAction::triggered, this, &LibraryWindow::addFolderRequested);
  connect(refreshAction, &QAction::triggered, this, &LibraryWindow::refreshRequested);
  connect(clearAction, &QAction::triggered, this, &LibraryWindow::clearRequested);
  QAction* stationAction = optionsMenu->addAction(tr("Tocar estação local"));
  stationAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
  connect(stationAction, &QAction::triggered, this, [this] {
    // Semente: a faixa selecionada (a tela de rádio não tem seleção, então a primeira).
    QString seed;
    if (table_ != nullptr && table_->currentRow() >= 0) {
      QTableWidgetItem* item = table_->item(table_->currentRow(), 0);
      if (item != nullptr) seed = item->data(Qt::UserRole).toString();
    }
    emit stationRequested(seed);
  });
  connect(removeAction_, &QAction::triggered, this, [this] {
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty()) emit removeRequested(paths);
  });
  root->setMenuBar(menuBar);

  auto* splitter = new QSplitter(Qt::Horizontal, this);

  // Árvore de visões, painel esquerdo do ML.
  tree_ = new QTreeWidget(splitter);
  tree_->setHeaderHidden(true);
  tree_->setRootIsDecorated(true);
  tree_->setUniformRowHeights(true);
  auto* local = new QTreeWidgetItem(tree_, QStringList{tr("Biblioteca local")});
  local->setExpanded(true);
  for (const ViewDef& def : kViews) {
    auto* item = new QTreeWidgetItem(local, QStringList{QString::fromUtf8(def.name)});
    item->setData(0, kViewRole, static_cast<int>(def.view));
  }
  // Segunda seção da árvore: o grupo Humor, com um nó por preset.
  dynamicsItem_ = new QTreeWidgetItem(tree_, QStringList{tr("Humor")});
  dynamicsItem_->setData(0, kViewRole, -1);
  // A Rádio local é irmã do grupo, não filha: ela não é um humor — parte da faixa que toca e
  // da mistura, sem ponto no plano. Fica logo abaixo, no mesmo nível, porque é a outra
  // alavanca da mesma altura. -2 no papel, para não se confundir com o -1 do grupo.
  stationItem_ = new QTreeWidgetItem(tree_, QStringList{tr("Rádio local")});
  stationItem_->setData(0, kViewRole, -2);
  dynamicsItem_->setExpanded(true);
  tree_->setCurrentItem(local->child(0));
  splitter->addWidget(tree_);

  // Lado direito: busca, tabela e rodapé.
  auto* right = new QWidget(splitter);
  auto* rightLayout = new QVBoxLayout(right);
  // A tabela pode encostar na linha do divisor (ela é a lista, e a lista é uma superfície);
  // quem ganha 20px é o texto — título, dica, busca e rodapé. Antes o rodapé e o painel de
  // análise tinham 100px e o resto zero, e o desenho ficava torto.
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(6);

  searchBar_ = new QWidget(right);
  auto* searchRow = new QHBoxLayout(searchBar_);
  searchRow->setContentsMargins(20, 0, 0, 0);
  search_ = new QLineEdit(right);
  search_->setPlaceholderText(tr("Buscar por título, artista ou álbum…"));
  search_->setClearButtonEnabled(true);
  auto* clear = new QPushButton(tr("Limpar busca"), right);
  searchRow->addWidget(new QLabel(tr("Buscar:"), right));
  searchRow->addWidget(search_, 1);
  searchRow->addWidget(clear);

  table_ = new QTableWidget(right);
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels({tr("Artista"), tr("Álbum"), tr("Faixa"), tr("Título"),
                                     tr("Duração"), tr("Gênero"), tr("Nota"), tr("Tocadas")});
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSortingEnabled(true);
  table_->setAlternatingRowColors(true);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->installEventFilter(this);
  table_->viewport()->installEventFilter(this);
  // Rádio local: a prévia da estação fica logo acima da tabela — a tabela é a estação, o
  // painel é a única alavanca (a mistura da semente).
  stationPanel_ = new QWidget(right);
  auto* stationLayout = new QVBoxLayout(stationPanel_);
  stationLayout->setContentsMargins(20, 0, 0, 0);
  stationLayout->setSpacing(4);
  auto* stationTitle = new QLabel(tr("Rádio local"), stationPanel_);
  stationTitle->setObjectName(QStringLiteral("dynamicsTitle"));
  stationHint_ = new QLabel(
      tr("A estação parte da faixa tocando e do que você vem ouvindo; o slider decide quanto "
         "de cada um."),
      stationPanel_);
  stationHint_->setObjectName(QStringLiteral("hint"));
  stationHint_->setWordWrap(true);
  auto* blendRow = new QHBoxLayout();
  auto* blendTrackLabel = new QLabel(tr("esta faixa"), stationPanel_);
  blendTrackLabel->setObjectName(QStringLiteral("hint"));
  blendSlider_ = new QSlider(Qt::Horizontal, stationPanel_);
  blendSlider_->setRange(0, 100);
  blendSlider_->setPageStep(10);
  blendSlider_->setToolTip(
      tr("0 = só esta faixa; 100 = só o que você vem ouvindo (as últimas 5 faixas)."));
  auto* blendHistoryLabel = new QLabel(tr("o que venho ouvindo"), stationPanel_);
  blendHistoryLabel->setObjectName(QStringLiteral("hint"));
  blendRow->addWidget(blendTrackLabel);
  blendRow->addWidget(blendSlider_, 1);
  blendRow->addWidget(blendHistoryLabel);
  stationLayout->addWidget(stationTitle);
  stationLayout->addWidget(stationHint_);
  stationLayout->addLayout(blendRow);
  stationPanel_->hide();
  connect(blendSlider_, &QSlider::valueChanged, this,
          [this](int value) { emit seedBlendChanged(value); });

  // Humor: cada preset é um nó, e os quatro sliders ficam aqui. A prévia (a tabela) anda
  // junto com o rascunho; a fila da estação só nasce no Tocar.
  humorPanel_ = new QWidget(right);
  auto* humorLayout = new QVBoxLayout(humorPanel_);
  humorLayout->setContentsMargins(20, 0, 0, 0);
  humorLayout->setSpacing(4);
  humorTitle_ = new QLabel(tr("Humor"), humorPanel_);
  humorTitle_->setObjectName(QStringLiteral("dynamicsTitle"));
  auto* humorHint = new QLabel(
      tr("Agitação e Tom são o alvo; a estabilidade decide quanto ele abraça. A prévia abaixo "
         "acompanha os sliders — a estação começa no Tocar."),
      humorPanel_);
  humorHint->setObjectName(QStringLiteral("hint"));
  humorHint->setWordWrap(true);

  auto* humorGrid = new QGridLayout();
  humorGrid->setHorizontalSpacing(8);
  humorGrid->setColumnStretch(1, 1);
  auto addHumorSlider = [&](int row, const QString& text, const QString& tooltip, int value,
                            QSlider** slider, QLabel** shown) {
    auto* name = new QLabel(text, humorPanel_);
    name->setToolTip(tooltip);
    auto* handle = new QSlider(Qt::Horizontal, humorPanel_);
    handle->setRange(0, 100);
    handle->setPageStep(10);
    handle->setToolTip(tooltip);
    handle->setValue(value);
    auto* number = new QLabel(humorPanel_);
    number->setObjectName(QStringLiteral("hint"));
    number->setMinimumWidth(28);
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    humorGrid->addWidget(name, row, 0);
    humorGrid->addWidget(handle, row, 1);
    humorGrid->addWidget(number, row, 2);
    *slider = handle;
    *shown = number;
    connect(handle, &QSlider::valueChanged, this, [this] {
      updateHumorValues();
      setHumorPending(true);
      emit humorPointChanged(true);
    });
  };
  addHumorSlider(0, tr("Agitação · lento ↔ rápido"),
                 tr("Mais à direita, mais agitado: o BPM manda, dobrando/dividindo para o "
                    "pulso percebido."),
                 50, &agitationSlider_, &agitationValue_);
  addHumorSlider(1, tr("Tom · frio ↔ quente"),
                 tr("Mais à esquerda, tons menores; mais à direita, maiores. No meio, o modo "
                    "não importa."),
                 50, &toneSlider_, &toneValue_);
  addHumorSlider(2, tr("Estabilidade · varia pouco ↔ varia muito"),
                 tr("Largura do alvo: perto de 0, só o que fica muito perto do ponto; perto "
                    "de 100, o alvo abraça faixas mais distantes."),
                 50, &stabilitySlider_, &stabilityValue_);
  addHumorSlider(3, tr("Mistura · humor ↔ o que venho ouvindo"),
                 tr("0 = só o humor; 100 = só o histórico das últimas 5 faixas."),
                 50, &humorBlendSlider_, &humorBlendValue_);

  humorPending_ = new QLabel(tr("modificações não aplicadas, clique em tocar para aplicar"),
                             humorPanel_);
  humorPending_->setStyleSheet(QStringLiteral("color: #e69138;"));
  humorPending_->hide();
  humorSaveButton_ = new QPushButton(tr("Salvar como…"), humorPanel_);
  humorSaveButton_->setToolTip(tr("Guarda os sliders como um humor com nome próprio. Os de "
                                  "fábrica não mudam."));
  connect(humorSaveButton_, &QPushButton::clicked, this, [this] { emit humorSaveRequested(); });
  // Remover só faz sentido para humor salvo: os de fábrica são o ponto de partida.
  humorRemoveButton_ = new QPushButton(tr("Remover"), humorPanel_);
  humorRemoveButton_->setToolTip(tr("Remove este humor salvo. Os de fábrica não saem."));
  connect(humorRemoveButton_, &QPushButton::clicked, this,
          [this] { emit humorDeleteRequested(humorPresetId()); });
  auto* humorFoot = new QHBoxLayout();
  humorFoot->addWidget(humorPending_);
  // O esticador segura os botões no tamanho natural: sem ele, o aviso oculto fazia os dois
  // botões se dividirem a linha inteira.
  humorFoot->addStretch(1);
  humorFoot->addWidget(humorSaveButton_);
  humorFoot->addWidget(humorRemoveButton_);

  humorLayout->addWidget(humorTitle_);
  humorLayout->addWidget(humorHint);
  humorLayout->addLayout(humorGrid);
  humorLayout->addLayout(humorFoot);
  humorPanel_->hide();

  rightLayout->addWidget(stationPanel_);
  rightLayout->addWidget(humorPanel_);
  rightLayout->addWidget(searchBar_);
  rightLayout->addWidget(table_, 1);

  dynamicsPanel_ = new QWidget(right);
  auto* dynamicsLayout = new QVBoxLayout(dynamicsPanel_);
  dynamicsLayout->setContentsMargins(20, 24, 0, 0);
  dynamicsTitle_ = new QLabel(tr("Analisando suas músicas"), dynamicsPanel_);
  dynamicsTitle_->setObjectName(QStringLiteral("dynamicsTitle"));
  dynamicsCount_ = new QLabel(dynamicsPanel_);
  dynamicsProgress_ = new QProgressBar(dynamicsPanel_);
  dynamicsProgress_->setRange(0, 100);
  dynamicsProgress_->setTextVisible(false);
  dynamicsHint_ = new QLabel(dynamicsPanel_);
  dynamicsHint_->setObjectName(QStringLiteral("hint"));
  dynamicsHint_->setWordWrap(true);
  dynamicsLayout->addWidget(dynamicsTitle_);
  dynamicsLayout->addWidget(dynamicsCount_);
  dynamicsLayout->addWidget(dynamicsProgress_);
  dynamicsLayout->addSpacing(8);
  dynamicsLayout->addWidget(dynamicsHint_);
  dynamicsLayout->addStretch(1);
  dynamicsPanel_->hide();
  rightLayout->addWidget(dynamicsPanel_, 1);

  auto* footer = new QHBoxLayout();
  summary_ = new QLabel(right);
  summary_->setObjectName(QStringLiteral("hint"));
  footer->addSpacing(20);
  footer->addWidget(summary_, 1);
  playButton_ = new QPushButton(tr("Tocar"), right);
  queueButton_ = new QPushButton(tr("Enfileirar"), right);
  // A estação é uma coisa viva: o botão gera as próximas a partir da faixa tocando (ou da
  // selecionada), em vez de tocar só a lista visível.
  stationButton_ = new QPushButton(tr("Tocar estação dessa música"), right);
  stationButton_->setToolTip(tr("Enfileira uma estação a partir desta música (ou da que está tocando)."));
  connect(stationButton_, &QPushButton::clicked, this, [this] {
    QString selected;
    const int row = table_->currentRow();
    if (row >= 0) {
      if (QTableWidgetItem* item = table_->item(row, 0)) {
        selected = item->data(Qt::UserRole).toString();
      }
    }
    emit stationRequested(selected);
  });
  footer->addWidget(playButton_);
  footer->addWidget(queueButton_);
  footer->addWidget(stationButton_);
  rightLayout->addLayout(footer);

  status_ = new QLabel(right);
  status_->setObjectName(QStringLiteral("hint"));
  status_->hide();
  rightLayout->addWidget(status_);

  splitter->addWidget(right);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({200, 900});
  root->addWidget(splitter, 1);

  // Busca com respiro: uma tecla por vez não deve disparar consulta no banco.
  auto* debounce = new QTimer(this);
  debounce->setSingleShot(true);
  debounce->setInterval(250);
  connect(search_, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });
  connect(debounce, &QTimer::timeout, this, &LibraryWindow::queryChanged);
  connect(clear, &QPushButton::clicked, search_, &QLineEdit::clear);

  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current == nullptr) return;
            const QVariant value = current->data(0, kViewRole);
            const int role = value.isNull() ? 0 : value.toInt();
            dynamicsActive_ = role == -1;
            stationActive_ = role == -2;
            humorActive_ = role == -3;
            table_->setVisible(!dynamicsActive_);
            if (searchBar_ != nullptr) {
              // A busca filtra a biblioteca; nas prévias ela não teria efeito.
              searchBar_->setVisible(!dynamicsActive_ && !stationActive_ && !humorActive_);
            }
            if (stationPanel_ != nullptr) stationPanel_->setVisible(stationActive_);
            if (humorPanel_ != nullptr) humorPanel_->setVisible(humorActive_);
            if (humorActive_) applyHumorDraftFromPreset();
            dynamicsPanel_->setVisible(dynamicsActive_);
            updateButtons();
            // O nó do humor refaz a prévia pela MainWindow (o léxico e a mistura são dela).
            if (humorActive_) {
              emit humorPointChanged(false);
              return;
            }
            if (dynamicsActive_ || value.isNull()) return;
            emit queryChanged();
          });
  connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
    if (item == nullptr) return;
    const QString path = table_->item(item->row(), 0)->data(Qt::UserRole).toString();
    if (!path.isEmpty()) emit playRequested(path);
  });
  connect(table_, &QTableWidget::itemSelectionChanged, this, &LibraryWindow::updateButtons);
  // Nota: clicar na estrela N grava N; clicar de novo na mesma nota limpa (0). A coluna
  // guarda o número em UserRole, então dá para saber a nota atual sem olhar o texto.
  connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
    if (column != 6 || row < 0) return;
    QTableWidgetItem* pathItem = table_->item(row, 0);
    QTableWidgetItem* ratingItem = table_->item(row, 6);
    if (pathItem == nullptr || ratingItem == nullptr) return;
    const int width = table_->columnWidth(6);
    const QPoint pos = table_->viewport()->mapFromGlobal(QCursor::pos());
    const QRect cell = table_->visualRect(table_->model()->index(row, 6));
    const int slice = std::max(1, width / 5);
    int rating = (pos.x() - cell.left()) / slice + 1;
    rating = std::clamp(rating, 1, 5);
    const int current = ratingItem->data(Qt::UserRole).toInt();
    if (current == rating) rating = 0;
    emit ratingRequested(pathItem->data(Qt::UserRole).toString(), rating);
  });
  connect(playButton_, &QPushButton::clicked, this, [this] {
    // Com um humor aberto, Tocar aplica o rascunho e começa a estação.
    if (humorActive_) {
      emit humorApplyRequested();
      return;
    }
    emit playSelectionRequested(selectedPaths());
  });
  connect(queueButton_, &QPushButton::clicked, this, [this] {
    if (humorActive_) {
      emit humorEnqueueRequested();
      return;
    }
    emit enqueueSelectionRequested(selectedPaths());
  });

  updateButtons();
}

void LibraryWindow::selectDynamics() {
  if (dynamicsItem_ != nullptr) tree_->setCurrentItem(dynamicsItem_);
}

void LibraryWindow::selectStation() {
  if (stationItem_ != nullptr) tree_->setCurrentItem(stationItem_);
}

void LibraryWindow::setSeedBlend(int blend) {
  if (blendSlider_ == nullptr) return;
  QSignalBlocker block(blendSlider_);
  blendSlider_->setValue(std::clamp(blend, 0, 100));
}

int LibraryWindow::seedBlend() const {
  return blendSlider_ != nullptr ? blendSlider_->value() : 0;
}

void LibraryWindow::selectView(int row) {
  if (tree_->topLevelItemCount() == 0) return;
  QTreeWidgetItem* local = tree_->topLevelItem(0);
  if (local != nullptr && row >= 0 && row < local->childCount()) {
    tree_->setCurrentItem(local->child(row));
  }
}

wamp::LibraryView LibraryWindow::view() const {
  QTreeWidgetItem* item = tree_->currentItem();
  if (item == nullptr) return wamp::LibraryView::All;
  const QVariant value = item->data(0, kViewRole);
  // Nós que não são biblioteca (composições dinâmicas, rádio) caem em "todas".
  if (!value.isNull() && value.toInt() < 0) return wamp::LibraryView::All;
  if (value.isNull()) {
    // Cabeçalho da árvore selecionado: cai em "todas".
    if (item->childCount() > 0) return wamp::LibraryView::All;
    return wamp::LibraryView::All;
  }
  return static_cast<wamp::LibraryView>(value.toInt());
}

QString LibraryWindow::filter() const { return search_->text().trimmed(); }

void LibraryWindow::setRows(const std::vector<wamp::LibraryEntry>& rows) {
  const QString selected =
      table_->currentRow() >= 0 && table_->item(table_->currentRow(), 0) != nullptr
          ? table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toString()
          : QString();

  const bool sorting = table_->isSortingEnabled();
  table_->setSortingEnabled(false);
  table_->setUpdatesEnabled(false);
  table_->clearContents();
  table_->setRowCount(static_cast<int>(rows.size()));

  const QFontMetrics metrics(table_->font());
  int durationWidth = 0;
  for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
    const wamp::LibraryEntry& entry = rows[static_cast<std::size_t>(row)];
    auto make = [this, row](int column, const QString& text) {
      auto* item = new NumericItem(text);
      table_->setItem(row, column, item);
      return item;
    };
    make(0, QString::fromStdString(entry.info.artist))->setData(Qt::UserRole,
                                                                QString::fromStdString(entry.info.path));
    make(1, QString::fromStdString(entry.info.album));
    make(2, entry.info.trackNo > 0 ? QString::number(entry.info.trackNo) : QString());
    make(3, QString::fromStdString(entry.info.title));
    const QString duration = durationText(entry.info.durationMs);
    durationWidth = std::max(durationWidth, metrics.horizontalAdvance(duration));
    make(4, duration);
    make(5, QString::fromStdString(entry.info.genre));
    make(6, starsText(entry.rating))->setData(Qt::UserRole, static_cast<qlonglong>(entry.rating));
    auto* plays = make(7, entry.playcount > 0 ? QString::number(entry.playcount) : QString());
    plays->setData(Qt::UserRole, static_cast<qlonglong>(entry.playcount));
    if (!duration.isEmpty()) {
      table_->item(row, 4)->setData(Qt::UserRole, static_cast<qlonglong>(entry.info.durationMs));
    }
  }

  table_->setSortingEnabled(sorting);
  table_->setUpdatesEnabled(true);
  table_->setColumnWidth(0, 160);   // artista
  table_->setColumnWidth(1, 200);   // álbum
  table_->setColumnWidth(2, 55);    // faixa
  table_->setColumnWidth(4, std::max(70, durationWidth + 18));
  table_->setColumnWidth(5, 90);    // gênero
  table_->setColumnWidth(6, 86);    // nota (5 estrelas não podem ser elididas)
  table_->setColumnWidth(7, 70);    // tocadas

  if (!selected.isEmpty()) {
    for (int row = 0; row < table_->rowCount(); ++row) {
      QTableWidgetItem* item = table_->item(row, 0);
      if (item != nullptr && item->data(Qt::UserRole).toString() == selected) {
        table_->selectRow(row);
        break;
      }
    }
  }
  updateButtons();
}

void LibraryWindow::setStatus(const QString& text) {
  status_->setText(text);
  status_->setVisible(!text.isEmpty());
}

void LibraryWindow::setSummary(const QString& text) { summary_->setText(text); }

void LibraryWindow::setAnalysisProgress(int analysed, int total, qint64 milliseconds) {
  const int percent = total > 0 ? static_cast<int>(100.0 * analysed / total) : 0;
  dynamicsProgress_->setValue(percent);
  const qint64 seconds = milliseconds / 1000;
  dynamicsCount_->setText(tr("%1 de %2 analisadas (%3%) — %4h %5min de música")
                              .arg(analysed)
                              .arg(total)
                              .arg(percent)
                              .arg(seconds / 3600)
                              .arg((seconds % 3600) / 60));
  // A Rádio local precisa de um mínimo de faixas com BPM e tom para não repetir sempre
  // as mesmas cinco músicas.
  constexpr int kRadioMinimum = 40;
  if (analysed >= kRadioMinimum) {
    dynamicsTitle_->setText(tr("Base pronta para as composições dinâmicas"));
    dynamicsHint_->setText(
        tr("A Rádio local já está no nó acima; os climas entram na próxima etapa."));
  } else {
    dynamicsTitle_->setText(tr("Analisando suas músicas"));
    dynamicsHint_->setText(tr("Faltam %1 faixa(s) analisada(s) para a rádio acertar BPM e "
                             "tom. A análise acompanha o que você ouve.")
                               .arg(kRadioMinimum - analysed));
  }
}

QStringList LibraryWindow::selectedPaths() const {
  QStringList paths;
  const auto rows = table_->selectionModel() != nullptr
                        ? table_->selectionModel()->selectedRows()
                        : QModelIndexList();
  for (const QModelIndex& index : rows) {
    QTableWidgetItem* item = table_->item(index.row(), 0);
    if (item != nullptr) paths << item->data(Qt::UserRole).toString();
  }
  return paths;
}

void LibraryWindow::updateButtons() {
  const bool selected = !selectedPaths().isEmpty();
  // No humor não há seleção para tocar: os botões valem para o rascunho dos sliders.
  playButton_->setEnabled(humorActive_ || selected);
  queueButton_->setEnabled(humorActive_ || selected);
  // "Tocar estação dessa música" parte da faixa tocando; no humor trocaria o clima pelo
  // histórico, então fica desligado.
  if (stationButton_ != nullptr) stationButton_->setEnabled(!humorActive_);
  if (removeAction_ != nullptr) removeAction_->setEnabled(!humorActive_ && selected);
}

void LibraryWindow::setHumorPresets(const std::vector<wamp::HumorPreset>& presets) {
  const QString keep = humorPresetName();
  {
    // Reconstruir a árvore mexe na seleção; os sinais só valem depois, na hora de reselecionar.
    const QSignalBlocker blocker(tree_);
    for (QTreeWidgetItem* item : humorItems_) delete item;
    humorItems_.clear();
    humorPresets_ = presets;
    // Todos os presets entram como filhos do grupo, na ordem do banco.
    for (std::size_t i = 0; i < humorPresets_.size(); ++i) {
      auto* item = new QTreeWidgetItem(QStringList{QString::fromStdString(humorPresets_[i].name)});
      item->setData(0, kViewRole, -3);
      item->setData(0, kPresetRole, static_cast<int>(i));
      item->setToolTip(0,
                       humorPresets_[i].factory
                           ? tr("Humor de fábrica: os sliders partem daqui, o preset não muda.")
                           : tr("Humor salvo por você."));
      if (dynamicsItem_ != nullptr) dynamicsItem_->insertChild(static_cast<int>(i), item);
      humorItems_.push_back(item);
    }
  }
  if (dynamicsItem_ != nullptr) dynamicsItem_->setExpanded(true);
  for (QTreeWidgetItem* item : humorItems_) {
    if (item->text(0) == keep) {
      tree_->setCurrentItem(item);
      return;
    }
  }
}

void LibraryWindow::selectHumor(const QString& name) {
  for (QTreeWidgetItem* item : humorItems_) {
    if (item->text(0) == name) {
      tree_->setCurrentItem(item);
      return;
    }
  }
}

QTreeWidgetItem* LibraryWindow::humorSelectedItem() const {
  QTreeWidgetItem* item = tree_->currentItem();
  if (item == nullptr) return nullptr;
  const QVariant role = item->data(0, kViewRole);
  if (role.isNull() || role.toInt() != -3) return nullptr;
  return item;
}

QString LibraryWindow::humorPresetName() const {
  QTreeWidgetItem* item = humorSelectedItem();
  return item != nullptr ? item->text(0) : QString();
}

int LibraryWindow::humorPresetId() const {
  QTreeWidgetItem* item = humorSelectedItem();
  if (item == nullptr) return -1;
  const QVariant index = item->data(0, kPresetRole);
  if (index.isNull()) return -1;
  const int at = index.toInt();
  if (at < 0 || at >= static_cast<int>(humorPresets_.size())) return -1;
  return humorPresets_[static_cast<std::size_t>(at)].id;
}

void LibraryWindow::applyHumorDraftFromPreset() {
  QTreeWidgetItem* item = humorSelectedItem();
  if (item == nullptr) return;
  const QVariant index = item->data(0, kPresetRole);
  if (index.isNull()) return;
  const int at = index.toInt();
  if (at < 0 || at >= static_cast<int>(humorPresets_.size())) return;
  const wamp::HumorPreset& preset = humorPresets_[static_cast<std::size_t>(at)];
  humorTitle_->setText(tr("Humor · %1").arg(QString::fromStdString(preset.name)));
  // Sem sinal: escolher o nó não é modificação pendente.
  const QSignalBlocker blockAgitation(agitationSlider_);
  const QSignalBlocker blockTone(toneSlider_);
  const QSignalBlocker blockStability(stabilitySlider_);
  const QSignalBlocker blockBlend(humorBlendSlider_);
  agitationSlider_->setValue(std::clamp(static_cast<int>(preset.point.agitation + 0.5), 0, 100));
  toneSlider_->setValue(std::clamp(static_cast<int>(preset.point.tone + 0.5), 0, 100));
  stabilitySlider_->setValue(std::clamp(static_cast<int>(preset.point.stability + 0.5), 0, 100));
  humorBlendSlider_->setValue(std::clamp(static_cast<int>(preset.blend * 100.0 + 0.5), 0, 100));
  updateHumorValues();
  setHumorPending(false);
  if (humorRemoveButton_ != nullptr) humorRemoveButton_->setEnabled(humorPresetId() > 0);
}

wamp::HumorPoint LibraryWindow::humorDraft() const {
  wamp::HumorPoint point;
  if (agitationSlider_ != nullptr) point.agitation = agitationSlider_->value();
  if (toneSlider_ != nullptr) point.tone = toneSlider_->value();
  if (stabilitySlider_ != nullptr) point.stability = stabilitySlider_->value();
  return point;
}

double LibraryWindow::humorBlendDraft() const {
  return humorBlendSlider_ != nullptr ? humorBlendSlider_->value() / 100.0 : 0.5;
}

void LibraryWindow::setHumorDraft(double agitation, double tone, double stability, double blend) {
  if (agitationSlider_ == nullptr) return;
  agitationSlider_->setValue(std::clamp(static_cast<int>(agitation + 0.5), 0, 100));
  toneSlider_->setValue(std::clamp(static_cast<int>(tone + 0.5), 0, 100));
  stabilitySlider_->setValue(std::clamp(static_cast<int>(stability + 0.5), 0, 100));
  humorBlendSlider_->setValue(std::clamp(static_cast<int>(blend * 100.0 + 0.5), 0, 100));
}

void LibraryWindow::setHumorPending(bool pending) {
  if (humorPending_ != nullptr) humorPending_->setVisible(pending);
}

void LibraryWindow::updateHumorValues() {
  auto setText = [](QLabel* label, QSlider* slider) {
    if (label != nullptr && slider != nullptr) label->setText(QString::number(slider->value()));
  };
  setText(agitationValue_, agitationSlider_);
  setText(toneValue_, toneSlider_);
  setText(stabilityValue_, stabilitySlider_);
  setText(humorBlendValue_, humorBlendSlider_);
}

bool LibraryWindow::eventFilter(QObject* watched, QEvent* event) {
  const bool fromTable =
      watched == table_ || watched == table_->viewport();
  if (fromTable && event->type() == QEvent::MouseButtonPress) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    // Alt+clique enfileira a faixa sob o cursor, como na playlist — sem mexer na seleção.
    if (mouse->button() == Qt::LeftButton && mouse->modifiers().testFlag(Qt::AltModifier)) {
      const QModelIndex index = table_->indexAt(mouse->position().toPoint());
      if (index.isValid()) {
        if (QTableWidgetItem* item = table_->item(index.row(), 0)) {
          emit enqueueSelectionRequested({item->data(Qt::UserRole).toString()});
        }
      }
      return true;
    }
  }
  if (watched == table_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    // Ctrl+A seleciona tudo: antes o atalho estava preso ao "Sempre no topo" na janela
    // principal e a lista nunca recebia a tecla.
    if (key->key() == Qt::Key_A && key->modifiers().testFlag(Qt::ControlModifier)) {
      table_->selectAll();
      return true;
    }
    if (key->key() == Qt::Key_Delete) {
      const QStringList paths = selectedPaths();
      if (!paths.isEmpty()) emit removeRequested(paths);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace wamp::ui
