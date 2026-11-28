#include "library_window.h"

#include "translator.h"

#include "cover_cache.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QHeaderView>
#include <QEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace wamp::ui {

namespace {

/// Papel do item da árvore: guarda o `wamp::LibraryView` da visão.
constexpr int kViewRole = Qt::UserRole;
/// Índice do humor no vetor de presets (valor guardado em cada item do combo).
constexpr int kPresetRole = Qt::UserRole + 1;
constexpr int kPresetNameRole = Qt::UserRole + 2;

/// Os nomes dos humores de fábrica nascem no motor (`radio.cpp`) como **dado**, não como texto
/// de interface. Aqui eles ganham tradução na hora de exibir; o nome original fica guardado no
/// item, porque é ele que identifica o preset (léxico da estação, "Salvar como…", banco).
const char* const kFactoryHumorNames[] = {
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Treino"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Alegre"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Dia de sol"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Trabalho duro"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Dia frio"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Pra baixo"),
    QT_TRANSLATE_NOOP("wamp::ui::HumorPresets", "Foco"),
};

QString humorLabel(const wamp::HumorPreset& preset) {
  if (!preset.factory) return QString::fromStdString(preset.name);
  return QCoreApplication::translate("wamp::ui::HumorPresets", preset.name.c_str());
}

/// A tabela da biblioteca arrastável: o arrasto carrega as **URLs** das faixas, que é o que
/// a playlist (e o gerenciador de arquivos) sabem receber. O QTableWidget padrão arrasta os
/// próprios itens, e a playlist os recusava em silêncio — daí "arrastar não faz nada".
class LibraryTable final : public QTableWidget {
 public:
  using QTableWidget::QTableWidget;

 protected:
  QMimeData* mimeData(const QList<QTableWidgetItem*>& items) const override {
    QStringList paths;
    for (const QTableWidgetItem* item : items) {
      if (item == nullptr || item->column() != 0) continue;
      const QString path = item->data(Qt::UserRole).toString();
      if (!path.isEmpty() && !paths.contains(path)) paths << path;
    }
    if (paths.isEmpty()) return nullptr;
    auto* data = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) urls << QUrl::fromLocalFile(path);
    data->setUrls(urls);
    data->setText(paths.join('\n'));
    // A fila lê isto para saber que o arrasto veio da Biblioteca (origem BIBLIOTECA).
    data->setData("application/x-amplia-source", "library");
    return data;
  }
};

struct ViewDef {
  const char* name;
  wamp::LibraryView view;
};

const ViewDef kViews[] = {
    // Rótulos curtos de propósito: a árvore tem 210 px e os nomes longos do original
    // ("Adicionadas recentemente") não cabem. A janela de cada visão vai no parêntese.
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Todas as faixas"), wamp::LibraryView::All},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Adicionadas (3 dias)"), wamp::LibraryView::RecentlyAdded},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Modificadas (3 dias)"), wamp::LibraryView::RecentlyModified},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Tocadas (2 semanas)"), wamp::LibraryView::RecentlyPlayed},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Nunca tocadas"), wamp::LibraryView::NeverPlayed},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Mais tocadas"), wamp::LibraryView::MostPlayed},
    {QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow", "Melhor avaliadas"), wamp::LibraryView::TopRated},
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

/// Chave da ordem padrão: ano (desconhecido por último), álbum sem caixa, faixa, título.
/// O separador \x01 não aparece em título/álbum e mantém os campos na ordem certa.
QString orderKey(const wamp::LibraryEntry& entry) {
  const int year = entry.info.year > 0 ? entry.info.year : 9999;
  return QString::number(year).rightJustified(4, QLatin1Char('0')) + QLatin1Char('\x01') +
         QString::fromStdString(entry.info.album).toCaseFolded() + QLatin1Char('\x01') +
         QString::number(entry.info.trackNo).rightJustified(3, QLatin1Char('0')) +
         QLatin1Char('\x01') + QString::fromStdString(entry.info.title).toCaseFolded();
}

QString starsText(int rating) {
  if (rating <= 0) return QStringLiteral("—");
  return QString(rating, QChar(0x2605));  // ★ repetida; o banco guarda só o número
}

/// Cinco fatias de estrela por célula, com o mesmo passo usado no clique.
constexpr int kStarPad = 4;

// Capa do álbum na lista: 18 px cabem na altura de linha sem mexer no espaçamento.
constexpr int kCoverPixels = 18;
constexpr int kCoverKeyRole = Qt::UserRole + 7;
constexpr int kCoverPathRole = Qt::UserRole + 8;
// Tamanho do tooltip de capa: o suficiente para reconhecer o disco sem inventar detalhe.
constexpr int kTipPixels = 160;
int starSliceWidth(int cellWidth) { return std::max(1, (cellWidth - 2 * kStarPad) / 5); }

/// Desenha sempre as cinco estrelas: acesas até a nota, esmaecidas depois (nota 0 = cinco
/// esmaecidas, que é o "sem preferência"). O texto do item fica vazio; o número mora no
/// `Qt::UserRole`, que é o que a ordenação usa.

namespace {
/// Texto único: rádio e humor aprendem pela mesma conta (radioAcceptance).
const char* const kRadioRules =
    QT_TRANSLATE_NOOP("wamp::ui::LibraryWindow",
                      "Tocar conta a favor. Interromper antes de 30 s conta como recusa, e "
                      "tirar da fila conta na hora. A nota em estrela também pesa: de 1 a 5 é "
                      "preferência; sem nota, a faixa não é penalizada. Cada escolha pesa cada "
                      "vez menos conforme você ouve — nada é definitivo.");
}  // namespace

class StarsDelegate final : public QStyledItemDelegate {
 public:
  explicit StarsDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    const QWidget* widget = opt.widget;
    QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    const int rating = index.data(Qt::UserRole).toInt();
    QColor lit = opt.palette.color(QPalette::Text);
    QColor dim = lit;
    dim.setAlphaF(0.25f);
    const int slice = starSliceWidth(opt.rect.width());
    const QString star(1, QChar(0x2605));
    painter->save();
    painter->setFont(opt.font);
    for (int i = 0; i < 5; ++i) {
      const QRect slot(opt.rect.left() + kStarPad + i * slice, opt.rect.top(), slice,
                       opt.rect.height());
      painter->setPen(i < rating ? lit : dim);
      painter->drawText(slot, Qt::AlignCenter, star);
    }
    painter->restore();
  }
};

/// Capa do álbum à esquerda do título. O pixmap vem do cache (que carrega fora da interface);
/// enquanto não chega, o espaço fica reservado — assim a coluna não "pula" quando a imagem
/// aparece.
class CoverDelegate final : public QStyledItemDelegate {
 public:
  explicit CoverDelegate(CoverCache* covers, QObject* parent)
      : QStyledItemDelegate(parent), covers_(covers) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text = opt.text;

    const QString key = index.data(kCoverKeyRole).toString();
    QPixmap pixmap;
    if (!key.isEmpty() && covers_ != nullptr) {
      pixmap = covers_->cover(key, index.data(kCoverPathRole).toString(), kCoverPixels);
    }

    opt.text.clear();
    const QWidget* widget = opt.widget;
    QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    if (pixmap.isNull() && text.isEmpty()) return;

    painter->save();
    int left = opt.rect.left() + 4;
    if (!pixmap.isNull()) {
      const int top = opt.rect.top() + (opt.rect.height() - kCoverPixels) / 2;
      painter->drawPixmap(left, top, pixmap);
      left += kCoverPixels + 6;
    } else if (!key.isEmpty()) {
      // Ainda carregando: um retângulo discreto marca o lugar da capa.
      QColor placeholder = opt.palette.color(QPalette::Text);
      placeholder.setAlphaF(0.12f);
      const int top = opt.rect.top() + (opt.rect.height() - kCoverPixels) / 2;
      painter->fillRect(QRect(left, top, kCoverPixels, kCoverPixels), placeholder);
      left += kCoverPixels + 6;
    }
    painter->setPen(opt.palette.color(QPalette::Text));
    painter->setFont(opt.font);
    const QRect textRect(left, opt.rect.top(), opt.rect.right() - left, opt.rect.height());
    const QFontMetrics metrics(opt.font);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(text, Qt::ElideRight, textRect.width()));
    painter->restore();
  }

 private:
  CoverCache* covers_ = nullptr;
};

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
  // O nó de topo é um título, não uma visão: clicar nele não filtra nada. Sem
  // ItemIsSelectable ele não recebe seleção, e o texto fica no tom de "rótulo".
  local->setFlags(local->flags() & ~Qt::ItemIsSelectable);
  local->setForeground(0, tree_->palette().brush(QPalette::Disabled, QPalette::Text));
  for (const ViewDef& def : kViews) {
    auto* item = new QTreeWidgetItem(local, QStringList{QCoreApplication::translate("wamp::ui::LibraryWindow", def.name)});
    item->setData(0, kViewRole, static_cast<int>(def.view));
  }
  // Segunda seção da árvore: o nó "Humor". A lista de humores mora no combo do painel —
  // a árvore não cresce a cada "Salvar como…" (ver D-124).
  dynamicsItem_ = new QTreeWidgetItem(tree_, QStringList{tr("Humor")});
  dynamicsItem_->setData(0, kViewRole, -1);
  // A Rádio local é irmã do grupo, não filha: ela não é um humor — parte da faixa que toca e
  // da mistura, sem ponto no plano. Fica logo abaixo, no mesmo nível, porque é a outra
  // alavanca da mesma altura. -2 no papel, para não se confundir com o -1 do grupo.
  stationItem_ = new QTreeWidgetItem(tree_, QStringList{tr("Rádio local")});
  stationItem_->setData(0, kViewRole, -2);
  lastViewItem_ = local->child(0);
  tree_->setCurrentItem(lastViewItem_);
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

  // Filtros combinados: cada caixa restringe a visão atual e conversa com as outras.
  filterBar_ = new QWidget(right);
  auto* filterRow = filterBar_;
  auto* filterLayout = new QHBoxLayout(filterRow);
  filterLayout->setContentsMargins(20, 0, 0, 0);
  filterLayout->setSpacing(6);
  auto makeBox = [filterRow](const QString& label) {
    auto* box = new QComboBox(filterRow);
    box->setMinimumContentsLength(10);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setToolTip(label);
    return box;
  };
  artistBox_ = makeBox(tr("Filtrar por artista"));
  albumBox_ = makeBox(tr("Filtrar por álbum"));
  genreBox_ = makeBox(tr("Filtrar por gênero"));
  filterLayout->addWidget(new QLabel(tr("Artista:"), filterRow));
  filterLayout->addWidget(artistBox_, 1);
  filterLayout->addWidget(new QLabel(tr("Álbum:"), filterRow));
  filterLayout->addWidget(albumBox_, 1);
  filterLayout->addWidget(new QLabel(tr("Gênero:"), filterRow));
  filterLayout->addWidget(genreBox_, 1);
  auto* clearFilters = new QPushButton(tr("Limpar filtros"), filterRow);
  filterLayout->addWidget(clearFilters);
  for (QComboBox* box : {artistBox_, albumBox_, genreBox_}) {
    connect(box, &QComboBox::currentIndexChanged, this, [this](int) { emit filterChanged(); });
  }
  connect(clearFilters, &QPushButton::clicked, this, [this] {
    for (QComboBox* box : {artistBox_, albumBox_, genreBox_}) box->setCurrentIndex(0);
    emit filterChanged();
  });

  table_ = new LibraryTable(right);
  // 0..9 são as colunas visíveis; a 10 é a chave da ordem padrão (Ano, Álbum, Faixa) e fica
  // escondida — uma coluna só ordena por um critério, e a ordem pedida tem três.
  table_->setColumnCount(11);
  table_->setHorizontalHeaderLabels({tr("Artista"), tr("Álbum"), tr("Faixa"), tr("Título"),
                                     tr("Duração"), tr("Gênero"), tr("Nota"), tr("Tocadas"),
                                     tr("Pulos"), tr("Ano"), QString()});
  table_->setItemDelegateForColumn(6, new StarsDelegate(table_));
  covers_ = new CoverCache(this);
  table_->setItemDelegateForColumn(3, new CoverDelegate(covers_, table_));
  connect(covers_, &CoverCache::loaded, table_->viewport(),
          QOverload<>::of(&QWidget::update));
  connect(covers_, &CoverCache::loaded, this, [this] {
    if (tipKey_.isEmpty()) return;
    const QString file = covers_->filePath(tipKey_, kTipPixels);
    if (file.isEmpty()) return;  // ainda não é esta capa
    tipKey_.clear();
    showCoverTip(tipPos_, file);
  });
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // Arrastar para a playlist: a tabela só arrasta (nunca recebe), e a cópia é o padrão.
  table_->setDragEnabled(true);
  table_->setDragDropMode(QAbstractItemView::DragOnly);
  table_->setDefaultDropAction(Qt::CopyAction);
  // Clique direito: menu da faixa (tocar, enfileirar, estação, copiar, pasta, remover).
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QTableWidget::customContextMenuRequested, this,
          &LibraryWindow::showContextMenu);
  table_->setSortingEnabled(true);
  table_->sortByColumn(10, Qt::AscendingOrder);
  table_->setColumnHidden(10, true);
  table_->setAlternatingRowColors(true);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->installEventFilter(this);
  table_->viewport()->installEventFilter(this);
  tree_->viewport()->installEventFilter(this);
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
  stationProgress_ = new QLabel(stationPanel_);
  stationProgress_->setObjectName(QStringLiteral("hint"));
  stationRulesToggle_ = new QToolButton(stationPanel_);
  stationRulesToggle_->setText(tr("Como a rádio aprende"));
  stationRulesToggle_->setCheckable(true);
  stationRulesToggle_->setArrowType(Qt::RightArrow);
  stationRulesToggle_->setAutoRaise(true);
  stationRulesToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  stationRulesText_ = new QLabel(
      QCoreApplication::translate("wamp::ui::LibraryWindow", kRadioRules), stationPanel_);
  stationRulesText_->setObjectName(QStringLiteral("hint"));
  stationRulesText_->setWordWrap(true);
  stationRulesText_->hide();
  stationLayout->addWidget(stationTitle);
  stationLayout->addWidget(stationHint_);
  stationLayout->addWidget(stationProgress_);
  stationLayout->addLayout(blendRow);
  stationLayout->addWidget(stationRulesToggle_);
  stationLayout->addWidget(stationRulesText_);
  stationPanel_->hide();
  connect(blendSlider_, &QSlider::valueChanged, this,
          [this](int value) { emit seedBlendChanged(value); });

  // Humor: um combo escolhe o preset, e os quatro sliders ficam aqui. A prévia (a tabela)
  // anda junto com o rascunho; a fila da estação só nasce no Tocar.
  humorPanel_ = new QWidget(right);
  auto* humorLayout = new QVBoxLayout(humorPanel_);
  humorLayout->setContentsMargins(20, 0, 0, 0);
  humorLayout->setSpacing(4);
  humorTitle_ = new QLabel(tr("Humor"), humorPanel_);
  humorTitle_->setObjectName(QStringLiteral("dynamicsTitle"));
  // A lista de humores é um combo, como a das curvas de EQ: escolher em vez de abrir nós.
  humorBox_ = new QComboBox(humorPanel_);
  humorBox_->setToolTip(tr("Humores de fábrica e os que você salvou."));
  connect(humorBox_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (!humorActive_) return;
    // Trocar de humor é escolher outro ponto de partida — não é modificação pendente.
    applyHumorDraftFromPreset();
    emit humorPointChanged(false);
  });
  auto* humorHead = new QHBoxLayout();
  humorHead->setSpacing(8);
  humorHead->addWidget(humorTitle_);
  // O combo no tamanho do nome: quem diz qual humor está aberto é ele, não o título.
  humorHead->addWidget(humorBox_);
  humorHead->addStretch(1);
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

  humorProgress_ = new QLabel(humorPanel_);
  humorProgress_->setObjectName(QStringLiteral("hint"));
  humorRulesToggle_ = new QToolButton(humorPanel_);
  humorRulesToggle_->setText(tr("Como a rádio aprende"));
  humorRulesToggle_->setCheckable(true);
  humorRulesToggle_->setArrowType(Qt::RightArrow);
  humorRulesToggle_->setAutoRaise(true);
  humorRulesToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  humorRulesText_ = new QLabel(
      QCoreApplication::translate("wamp::ui::LibraryWindow", kRadioRules), humorPanel_);
  humorRulesText_->setObjectName(QStringLiteral("hint"));
  humorRulesText_->setWordWrap(true);
  humorRulesText_->hide();
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
  // Reverter fica ao lado do aviso, de quem ele desfaz: volta ao ponto do humor escolhido.
  humorRevertButton_ = new QPushButton(tr("Reverter"), humorPanel_);
  humorRevertButton_->setToolTip(tr("Volta os sliders ao humor do combo e descarta o rascunho."));
  humorRevertButton_->setEnabled(false);
  connect(humorRevertButton_, &QPushButton::clicked, this, [this] { revertHumorDraft(); });
  humorFoot->addWidget(humorRevertButton_);
  humorFoot->addWidget(humorSaveButton_);
  humorFoot->addWidget(humorRemoveButton_);

  humorLayout->addLayout(humorHead);
  humorLayout->addWidget(humorHint);
  humorLayout->addWidget(humorProgress_);
  humorLayout->addLayout(humorGrid);
  humorLayout->addLayout(humorFoot);
  humorLayout->addWidget(humorRulesToggle_);
  humorLayout->addWidget(humorRulesText_);
  humorPanel_->hide();

  rightLayout->addWidget(stationPanel_);
  rightLayout->addWidget(humorPanel_);
  rightLayout->addWidget(searchBar_);
  rightLayout->addWidget(filterBar_);
  rightLayout->addWidget(table_, 1);

  auto* footer = new QHBoxLayout();
  summary_ = new QLabel(right);
  summary_->setObjectName(QStringLiteral("hint"));
  footer->addSpacing(20);
  footer->addWidget(summary_, 1);
  playButton_ = new QPushButton(tr("Tocar"), right);
  queueButton_ = new QPushButton(tr("Enfileirar"), right);
  // Sem isto, pôr faixas na playlist só existia por arrasto (item de acessibilidade).
  addButton_ = new QPushButton(tr("Adicionar à playlist"), right);
  addButton_->setToolTip(tr("Acrescenta as faixas escolhidas ao fim da playlist, sem tocar."));
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
  footer->addWidget(addButton_);
  footer->addWidget(stationButton_);
  rightLayout->addLayout(footer);

  // QoL 28: botão que não encolhe abaixo do próprio texto. Sem isso, janela estreita corta
  // o rótulo ("onar à pl") em vez de o layout ceder espaço — e a janela tem tamanho mínimo
  // que comporta o rodapé inteiro, então não existe estado em que o texto suma.
  for (QPushButton* button : {playButton_, queueButton_, addButton_, stationButton_}) {
    button->setMinimumWidth(button->sizeHint().width());
  }
  setMinimumWidth(std::max(880, footer->sizeHint().width() + 60));

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
            // Clicar no título ("Biblioteca local") não é escolher uma visão: volta para a
            // última escolhida em vez de deixar a árvore sem seleção.
            if (current == nullptr) {
              if (lastViewItem_ != nullptr) tree_->setCurrentItem(lastViewItem_);
              return;
            }
            if (current->data(0, kViewRole).isValid() || current->childCount() == 0) {
              lastViewItem_ = current;
            }
            const QVariant value = current->data(0, kViewRole);
            const int role = value.isNull() ? 0 : value.toInt();
            humorActive_ = role == -1;
            stationActive_ = role == -2;
            if (searchBar_ != nullptr) {
              // A busca filtra a biblioteca; nas prévias ela não teria efeito.
              searchBar_->setVisible(!stationActive_ && !humorActive_);
              if (filterBar_ != nullptr) {
                filterBar_->setVisible(!stationActive_ && !humorActive_);
              }
            }
            if (stationPanel_ != nullptr) stationPanel_->setVisible(stationActive_);
            if (humorPanel_ != nullptr) humorPanel_->setVisible(humorActive_);
            updateButtons();
            if (humorActive_) {
              // O nó do humor refaz a prévia pela MainWindow (o léxico e a mistura são dela).
              applyHumorDraftFromPreset();
              emit humorPointChanged(false);
              return;
            }
            if (value.isNull()) {
              // Título pelo teclado: o Qt limpa a seleção depois deste sinal, então o
              // conserto espera o próximo ciclo do laço de eventos.
              QTimer::singleShot(0, this, [this] {
                if (lastViewItem_ != nullptr) tree_->setCurrentItem(lastViewItem_);
              });
              return;
            }
            emit queryChanged();
          });
  connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
    if (item == nullptr) return;
    const QString path = table_->item(item->row(), 0)->data(Qt::UserRole).toString();
    if (!path.isEmpty()) emit playRequested(path);
  });
  connect(table_, &QTableWidget::itemSelectionChanged, this, &LibraryWindow::updateButtons);
  // Reconsulta da visão por tocada: só depois de um respiro, porque cada reconsulta
  // percorre a biblioteca inteira.
  playViewTimer_ = new QTimer(this);
  playViewTimer_->setSingleShot(true);
  playViewTimer_->setInterval(500);
  connect(playViewTimer_, &QTimer::timeout, this, [this] { emit queryChanged(); });
  // Nota: clicar na estrela N grava N; clicar de novo na mesma nota limpa (0). A coluna
  // guarda o número em UserRole, então dá para saber a nota atual sem olhar o texto.
  connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
    if (column != 6 || row < 0) return;
    QTableWidgetItem* pathItem = table_->item(row, 0);
    QTableWidgetItem* ratingItem = table_->item(row, 6);
    if (pathItem == nullptr || ratingItem == nullptr) return;
    const QPoint pos = table_->viewport()->mapFromGlobal(QCursor::pos());
    const QRect cell = table_->visualRect(table_->model()->index(row, 6));
    // A fatia é a mesma que o StarsDelegate desenha, senão o clique cai na estrela errada.
    const int slice = starSliceWidth(cell.width());
    int rating = (pos.x() - cell.left() - kStarPad) / slice + 1;
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
  connect(addButton_, &QPushButton::clicked, this, [this] {
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty()) emit addToPlaylistRequested(paths);
  });



  // O bloco de regras é o mesmo nos dois painéis (rádio e humor aprendem pela mesma conta)
  // e o estado é um só, guardado na INI.
  auto toggleRules = [this](bool open) {
    if (stationRulesText_ != nullptr) stationRulesText_->setVisible(open);
    if (humorRulesText_ != nullptr) humorRulesText_->setVisible(open);
    const auto sync = [open](QToolButton* button) {
      if (button == nullptr) return;
      const QSignalBlocker blocker(button);
      button->setChecked(open);
      button->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    };
    sync(stationRulesToggle_);
    sync(humorRulesToggle_);
    QSettings settings(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/radioRules"), open);
  };
  for (QToolButton* toggle : {stationRulesToggle_, humorRulesToggle_}) {
    if (toggle == nullptr) continue;
    connect(toggle, &QToolButton::toggled, this, [toggleRules](bool open) { toggleRules(open); });
  }
  {
    QSettings settings(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
    const bool open = settings.value(QStringLiteral("ui/radioRules"), false).toBool();
    if (open) toggleRules(true);
  }
  updateButtons();
}

void LibraryWindow::selectDynamics() {
  // Nome mantido pelo hook de teste (AMPLIA_LIBRARY_DYNAMICS): é o mesmo nó do Humor.
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

namespace {
QString currentFilterText(const QComboBox* box) {
  if (box == nullptr || box->currentIndex() <= 0) return QString();
  return box->currentData().toString();
}

/// Enche a caixa mantendo a escolha: o "todos" é o item 0, o resto vem do banco.
void fillFilterBox(QComboBox* box, const QString& allLabel,
                   const std::vector<std::string>& values) {
  if (box == nullptr) return;
  const QString keep = currentFilterText(box);
  const QSignalBlocker blocker(box);
  box->clear();
  box->addItem(allLabel, QString());
  for (const std::string& value : values) {
    const QString text = QString::fromStdString(value);
    box->addItem(text, text);
  }
  const int at = keep.isEmpty() ? 0 : box->findData(keep);
  box->setCurrentIndex(at >= 0 ? at : 0);
}
}  // namespace

QString LibraryWindow::artistFilter() const { return currentFilterText(artistBox_); }
QString LibraryWindow::albumFilter() const { return currentFilterText(albumBox_); }
QString LibraryWindow::genreFilter() const { return currentFilterText(genreBox_); }

void LibraryWindow::setAnalysisProgress(int measured, int total) {
  const QString text =
      total <= 0 ? QString()
                 : tr("%1 de %2 faixas medidas — a análise acontece ao tocar").arg(measured).arg(total);
  if (stationProgress_ != nullptr) stationProgress_->setText(text);
  if (humorProgress_ != nullptr) humorProgress_->setText(text);
}

void LibraryWindow::setFilterOptions(const std::vector<std::string>& artists,
                                     const std::vector<std::string>& albums,
                                     const std::vector<std::string>& genres) {
  fillFilterBox(artistBox_, tr("Todos os artistas"), artists);
  fillFilterBox(albumBox_, tr("Todos os álbuns"), albums);
  fillFilterBox(genreBox_, tr("Todos os gêneros"), genres);
}

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
    QTableWidgetItem* titleItem = make(3, QString::fromStdString(entry.info.title));
    // A capa é por álbum; a chave vem da identidade da obra, e o caminho é só a amostra
    // para a extração (embutida ou `folder.jpg`).
    if (!entry.info.album.empty()) {
      const QString key = CoverCache::albumKey(QString::fromStdString(entry.info.artist),
                                               QString::fromStdString(entry.info.album));
      titleItem->setData(kCoverKeyRole, key);
      titleItem->setData(kCoverPathRole, QString::fromStdString(entry.info.path));
    }
    const QString duration = durationText(entry.info.durationMs);
    durationWidth = std::max(durationWidth, metrics.horizontalAdvance(duration));
    make(4, duration);
    make(5, QString::fromStdString(entry.info.genre));
    // O texto das estrelas fica vazio: quem desenha é o StarsDelegate, lendo o número.
    make(6, QString())->setData(Qt::UserRole, static_cast<qlonglong>(entry.rating));
    auto* plays = make(7, entry.playcount > 0 ? QString::number(entry.playcount) : QString());
    plays->setData(Qt::UserRole, static_cast<qlonglong>(entry.playcount));
    auto* skips = make(8, entry.skips > 0 ? QString::number(entry.skips) : QString());
    skips->setData(Qt::UserRole, static_cast<qlonglong>(entry.skips));
    auto* year = make(9, entry.info.year > 0 ? QString::number(entry.info.year) : QString());
    year->setData(Qt::UserRole, static_cast<qlonglong>(entry.info.year));
    if (!duration.isEmpty()) {
      table_->item(row, 4)->setData(Qt::UserRole, static_cast<qlonglong>(entry.info.durationMs));
    }
    table_->setItem(row, 10, new QTableWidgetItem(orderKey(entry)));
    // O motivo vem como chave de eixo; a tradução é daqui (o banco não sabe idioma).
    if (!entry.reason.empty()) {
      QString why;
      if (entry.reason == "album") {
        why = tr("mesmo álbum da semente");
      } else if (entry.reason == "artist") {
        why = tr("mesmo artista da semente");
      } else if (entry.reason == "genre") {
        why = tr("mesmo gênero");
      } else if (entry.reason == "tempo") {
        why = tr("BPM parecido");
      } else if (entry.reason == "key") {
        why = tr("tom compatível");
      } else if (entry.reason == "year") {
        why = tr("época próxima");
      }
      if (!why.isEmpty() && titleItem != nullptr) titleItem->setToolTip(why);
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
  table_->setColumnWidth(8, 60);    // pulos
  table_->setColumnWidth(9, 55);    // ano

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

void LibraryWindow::touchLiveRow(int column, const QString& path, int value) {
  if (path.isEmpty() || column < 0) return;
  for (int row = 0; row < table_->rowCount(); ++row) {
    QTableWidgetItem* first = table_->item(row, 0);
    if (first == nullptr || first->data(Qt::UserRole).toString() != path) continue;
    QTableWidgetItem* cell = table_->item(row, column);
    if (cell == nullptr) return;
    cell->setText(value > 0 ? QString::number(value) : QString());
    cell->setData(Qt::UserRole, static_cast<qlonglong>(value));
    return;
  }
}

void LibraryWindow::notePlay(const QString& path, int playcount) {
  if (playcount <= 0) return;
  touchLiveRow(7, path, playcount);  // "Tocadas" muda na hora, sem reconsulta
  schedulePlayViewRefresh();
}

void LibraryWindow::noteSkip(const QString& path, int skips) {
  if (skips <= 0) return;
  touchLiveRow(8, path, skips);  // "Pulos"
}

void LibraryWindow::schedulePlayViewRefresh() {
  const wamp::LibraryView current = view();
  // Nas visões por data/contagem a linha pode precisar entrar, sair ou mudar de lugar;
  // nas demais, atualizar a célula basta.
  if (current != wamp::LibraryView::RecentlyPlayed &&
      current != wamp::LibraryView::MostPlayed) {
    return;
  }
  if (playViewTimer_ != nullptr) playViewTimer_->start();
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
  if (addButton_ != nullptr) addButton_->setEnabled(selected);
  // "Tocar estação dessa música" parte da faixa tocando; no humor trocaria o clima pelo
  // histórico, então fica desligado.
  if (stationButton_ != nullptr) stationButton_->setEnabled(!humorActive_);
  if (removeAction_ != nullptr) removeAction_->setEnabled(!humorActive_ && selected);
}

void LibraryWindow::setHumorPresets(const std::vector<wamp::HumorPreset>& presets) {
  const QString keep = humorPresetName();
  {
    // Reencher o combo mexe no índice atual; os sinais só valem depois, na reseleção.
    const QSignalBlocker blocker(humorBox_);
    humorPresets_ = presets;
    humorBox_->clear();
    for (std::size_t i = 0; i < humorPresets_.size(); ++i) {
      const wamp::HumorPreset& preset = humorPresets_[i];
      humorBox_->addItem(humorLabel(preset));
      const int item = static_cast<int>(i);
      humorBox_->setItemData(item, static_cast<int>(i), kPresetRole);
      humorBox_->setItemData(item, QString::fromStdString(preset.name), kPresetNameRole);
      // Os salvos vêm depois dos de fábrica; o tooltip diz de onde cada um veio.
      humorBox_->setItemData(item,
                             preset.factory
                                 ? tr("Humor de fábrica: os sliders partem daqui, o preset não muda.")
                                 : tr("Humor salvo por você."),
                             Qt::ToolTipRole);
    }
  }
  if (!keep.isEmpty()) {
    // Procura pelo nome original, não pelo rótulo: o rótulo muda com o idioma.
    for (int at = 0; at < humorBox_->count(); ++at) {
      if (humorBox_->itemData(at, kPresetNameRole).toString() == keep) {
        humorBox_->setCurrentIndex(at);
        break;
      }
    }
  }
  if (humorActive_) applyHumorDraftFromPreset();
}

void LibraryWindow::selectHumor(const QString& name) {
  // Primeiro o nó (o painel só existe com o Humor aberto), depois o combo.
  if (dynamicsItem_ != nullptr) tree_->setCurrentItem(dynamicsItem_);
  for (int at = 0; at < humorBox_->count(); ++at) {
    if (humorBox_->itemData(at, kPresetNameRole).toString() == name) {
      humorBox_->setCurrentIndex(at);
      return;
    }
  }
}

QString LibraryWindow::humorPresetName() const {
  if (humorBox_ == nullptr || humorBox_->currentIndex() < 0) return QString();
  // O combo mostra o rótulo traduzido; quem identifica o preset é o nome original.
  const QVariant stored = humorBox_->itemData(humorBox_->currentIndex(), kPresetNameRole);
  return stored.isValid() ? stored.toString() : humorBox_->currentText();
}

int LibraryWindow::humorPresetId() const {
  if (humorBox_ == nullptr || humorBox_->currentIndex() < 0) return -1;
  const int at = humorBox_->currentIndex();
  if (at >= static_cast<int>(humorPresets_.size())) return -1;
  return humorPresets_[static_cast<std::size_t>(at)].id;
}

void LibraryWindow::applyHumorDraftFromPreset() {
  if (humorBox_ == nullptr) return;
  const int at = humorBox_->currentIndex();
  if (at < 0 || at >= static_cast<int>(humorPresets_.size())) return;
  const wamp::HumorPreset& preset = humorPresets_[static_cast<std::size_t>(at)];
  humorTitle_->setText(tr("Humor"));
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
  if (humorRevertButton_ != nullptr) humorRevertButton_->setEnabled(pending);
}

void LibraryWindow::revertHumorDraft() {
  applyHumorDraftFromPreset();
  emit humorPointChanged(false);
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

void LibraryWindow::showContextMenu(const QPoint& position) {
  const QModelIndex index = table_->indexAt(position);
  if (!index.isValid()) return;

  // Clique direito fora da seleção aponta a faixa: selecionar antes de abrir o menu é o
  // comportamento que se espera de lista no desktop.
  if (!table_->selectionModel()->isRowSelected(index.row(), QModelIndex())) {
    table_->clearSelection();
    table_->selectRow(index.row());
  }

  const QStringList paths = selectedPaths();
  if (paths.isEmpty()) return;

  QTableWidgetItem* anchor = table_->item(index.row(), 0);
  const QString seed = anchor != nullptr ? anchor->data(Qt::UserRole).toString() : paths.front();

  QMenu menu(this);
  QAction* play = menu.addAction(tr("Tocar"));
  QAction* enqueue = menu.addAction(tr("Enfileirar"));
  menu.addSeparator();
  QAction* station = menu.addAction(tr("Tocar estação dessa música"));
  menu.addSeparator();
  QAction* tagInfo = menu.addAction(tr("Ver tag (Alt+3)"));
  QAction* tagRefresh = menu.addAction(paths.size() == 1 ? tr("Atualizar tag")
                                                         : tr("Atualizar tags"));
  QAction* tagSearch =
      menu.addAction(paths.size() == 1 ? tr("Buscar tags no MusicBrainz…")
                                       : tr("Buscar tags no MusicBrainz em lote…"));
  menu.addSeparator();
  QAction* copy = menu.addAction(paths.size() == 1 ? tr("Copiar caminho")
                                                   : tr("Copiar caminhos selecionados"));
  QAction* folder = menu.addAction(tr("Abrir pasta"));
  menu.addSeparator();
  QAction* remove = menu.addAction(tr("Remover da biblioteca"));

  QAction* chosen = menu.exec(table_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) return;
  if (chosen == play) {
    emit playSelectionRequested(paths);
  } else if (chosen == enqueue) {
    emit enqueueSelectionRequested(paths);
  } else if (chosen == station) {
    emit stationRequested(seed);
  } else if (chosen == tagInfo) {
    emit tagInfoRequested(seed);
  } else if (chosen == tagRefresh) {
    emit tagRefreshRequested(paths);
  } else if (chosen == tagSearch) {
    emit batchTagRequested(paths);
  } else if (chosen == copy) {
    QGuiApplication::clipboard()->setText(paths.join('\n'));
    setStatus(paths.size() == 1 ? tr("Caminho copiado.") : tr("%1 caminhos copiados.").arg(paths.size()));
  } else if (chosen == folder) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(paths.front()).absolutePath()));
  } else if (chosen == remove) {
    emit removeRequested(paths);
  }
}

void LibraryWindow::showCoverTip(const QPoint& globalPos, const QString& file) const {
  const QString html =
      QStringLiteral("<img src=\"%1\">").arg(QUrl::fromLocalFile(file).toString());
  QToolTip::showText(globalPos, html, const_cast<LibraryWindow*>(this));
}

bool LibraryWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == tree_->viewport() && event->type() == QEvent::MouseButtonPress) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    QTreeWidgetItem* item = tree_->itemAt(mouse->position().toPoint());
    // "Biblioteca local" é rótulo, não visão: clicar nele não troca a lista nem apaga a
    // seleção. Engolir o press evita que o Qt limpe a seleção antes do nosso conserto.
    if (item != nullptr && item == tree_->topLevelItem(0)) {
      // A setinha de recolher fica à esquerda do retângulo do item: nela o clique vale.
      const QRect rect = tree_->visualItemRect(item);
      if (mouse->position().x() >= rect.left()) {
        if (lastViewItem_ != nullptr) tree_->setCurrentItem(lastViewItem_);
        return true;
      }
    }
  }
  if (watched == table_->viewport() && event->type() == QEvent::ToolTip) {
    // A miniatura de 18 px é uma pista; o tooltip mostra a capa em tamanho de ver. Só a
    // variante de 160 px, e o CoverCache nunca amplia: arte pequena sai no tamanho dela.
    auto* help = static_cast<QHelpEvent*>(event);
    const QModelIndex index = table_->indexAt(help->pos());
    QTableWidgetItem* title = index.isValid() ? table_->item(index.row(), 3) : nullptr;
    const QRect cell = index.isValid() ? table_->visualRect(index) : QRect();
    const bool overCover =
        title != nullptr && help->pos().x() >= cell.left() && help->pos().x() <= cell.left() + kCoverPixels + 4;
    const QString key = overCover ? title->data(kCoverKeyRole).toString() : QString();
    if (!key.isEmpty()) {
      const QString file = covers_->filePath(key, kTipPixels);
      if (!file.isEmpty()) {
        showCoverTip(help->globalPos(), file);
      } else {
        // Primeira vez: extrai agora e mostra quando ficar pronto (não amplia nada).
        tipKey_ = key;
        tipPos_ = help->globalPos();
        covers_->cover(key, title->data(kCoverPathRole).toString(), kTipPixels);
        QToolTip::showText(help->globalPos(), tr("Carregando a capa…"), table_);
      }
      return true;
    }
    tipKey_.clear();
    QToolTip::hideText();
    return true;
  }
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
