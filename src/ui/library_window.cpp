#include "library_window.h"

#include <QCursor>
#include <QHeaderView>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
  tree_->setCurrentItem(local->child(0));
  splitter->addWidget(tree_);

  // Lado direito: busca, tabela e rodapé.
  auto* right = new QWidget(splitter);
  auto* rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(6);

  auto* searchRow = new QHBoxLayout();
  search_ = new QLineEdit(right);
  search_->setPlaceholderText(tr("Buscar por título, artista ou álbum…"));
  search_->setClearButtonEnabled(true);
  auto* clear = new QPushButton(tr("Limpar busca"), right);
  searchRow->addWidget(new QLabel(tr("Buscar:"), right));
  searchRow->addWidget(search_, 1);
  searchRow->addWidget(clear);
  rightLayout->addLayout(searchRow);

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
  rightLayout->addWidget(table_, 1);

  auto* footer = new QHBoxLayout();
  summary_ = new QLabel(right);
  summary_->setObjectName(QStringLiteral("hint"));
  footer->addWidget(summary_, 1);
  playButton_ = new QPushButton(tr("Tocar"), right);
  queueButton_ = new QPushButton(tr("Enfileirar"), right);
  footer->addWidget(playButton_);
  footer->addWidget(queueButton_);
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
            if (current == nullptr || current->data(0, kViewRole).isNull()) return;
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
  connect(playButton_, &QPushButton::clicked, this,
          [this] { emit playSelectionRequested(selectedPaths()); });
  connect(queueButton_, &QPushButton::clicked, this,
          [this] { emit enqueueSelectionRequested(selectedPaths()); });

  updateButtons();
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
  const bool has = !selectedPaths().isEmpty();
  playButton_->setEnabled(has);
  queueButton_->setEnabled(has);
  if (removeAction_ != nullptr) removeAction_->setEnabled(has);
}

bool LibraryWindow::eventFilter(QObject* watched, QEvent* event) {
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
