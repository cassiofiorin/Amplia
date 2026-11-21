#include "jump_dialog.h"

#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>

namespace wamp::ui {
namespace {

constexpr int kHtmlRole = Qt::UserRole + 1;

/// Minúsculas, sem acento e sem pontuação: "Árvore" e "arvore" precisam casar.
QString normalize(const QString& text) {
  const QString decomposed = text.normalized(QString::NormalizationForm_KD).toLower();
  QString out;
  out.reserve(decomposed.size());
  for (const QChar ch : decomposed) {
    if (ch.isMark()) continue;
    if (ch.isLetterOrNumber() || ch.isSpace()) {
      out.append(ch);
    } else {
      out.append(QLatin1Char(' '));
    }
  }
  return out.simplified();
}

QString escapeHtml(QString text) {
  text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
  text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
  text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
  return text;
}

/// Marca no rótulo os trechos que casaram, sem deixar o HTML do usuário passar.
QString highlight(const QString& label, const QStringList& tokens) {
  const QString flat = normalize(label);
  QVector<bool> marked(flat.size(), false);
  for (const QString& token : tokens) {
    int from = 0;
    while (true) {
      const int at = flat.indexOf(token, from);
      if (at < 0) break;
      for (int i = at; i < at + token.size() && i < marked.size(); ++i) marked[i] = true;
      from = at + token.size();
    }
  }

  QString html;
  bool inside = false;
  for (int i = 0; i < label.size(); ++i) {
    const bool want = i < marked.size() && marked[i];
    if (want != inside) {
      html += want ? QStringLiteral("<b><span style='color:#e0b050'>")
                   : QStringLiteral("</span></b>");
      inside = want;
    }
    html += escapeHtml(QString(label[i]));
  }
  if (inside) html += QStringLiteral("</span></b>");
  return html;
}

class JumpDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    QStyle* style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QString html = index.data(kHtmlRole).toString();
    if (html.isEmpty()) return;

    painter->save();
    painter->setClipRect(opt.rect);
    painter->setPen(opt.state & QStyle::State_Selected ? QColor(0xf3, 0xea, 0xd0)
                                                       : QColor(0xd8, 0xd8, 0xd8));
    QTextDocument doc;
    doc.setDefaultFont(opt.font);
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.setTextWidth(opt.rect.width() - 10);
    painter->translate(opt.rect.left() + 5, opt.rect.top() + 2);
    doc.drawContents(painter);
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QSize base = QStyledItemDelegate::sizeHint(option, index);
    base.setHeight(std::max(base.height(), option.fontMetrics.height() + 8));
    return base;
  }
};

}  // namespace

JumpDialog::JumpDialog(QVector<JumpEntry> entries, QWidget* parent)
    : QDialog(parent), entries_(std::move(entries)) {
  setWindowTitle(tr("Ir para a faixa"));
  setModal(false);
  resize(520, 420);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(6);

  search_ = new QLineEdit(this);
  search_->setPlaceholderText(tr("Digite para filtrar (Enter toca, Ctrl+Enter enfileira)"));
  search_->installEventFilter(this);
  layout->addWidget(search_);

  counter_ = new QLabel(this);
  counter_->setEnabled(false);
  layout->addWidget(counter_);

  list_ = new QListWidget(this);
  list_->setItemDelegate(new JumpDelegate(list_));
  list_->setUniformItemSizes(false);
  list_->setAlternatingRowColors(false);
  layout->addWidget(list_, 1);

  list_->installEventFilter(this);
  list_->viewport()->installEventFilter(this);

  connect(search_, &QLineEdit::textChanged, this, &JumpDialog::applyFilter);
  connect(search_, &QLineEdit::returnPressed, this, [this] { activateCurrent(false); });
  connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
    activateCurrent(false);
  });

  applyFilter();
  search_->setFocus();
}

void JumpDialog::setQuery(const QString& text) {
  search_->setText(text);
  applyFilter();
}

int JumpDialog::resultCount() const { return list_->count(); }

QString JumpDialog::currentPath() const {
  QListWidgetItem* item = list_->currentItem();
  return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

bool JumpDialog::eventFilter(QObject* watched, QEvent* event) {
  // Alt+clique esquerdo enfileira, igual à lista principal.
  if (watched == list_->viewport() && event->type() == QEvent::MouseButtonPress) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton && (mouse->modifiers() & Qt::AltModifier) != 0) {
      if (QListWidgetItem* item = list_->itemAt(mouse->position().toPoint()); item != nullptr) {
        list_->setCurrentItem(item);
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) emit queueRequested(path);
      }
      return true;
    }
  }

  if (event->type() != QEvent::KeyPress) return QDialog::eventFilter(watched, event);
  auto* key = static_cast<QKeyEvent*>(event);
  const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
  const bool control = (key->modifiers() & Qt::ControlModifier) != 0;

  // Com o foco no campo de busca, as setas mudam a seleção da lista em vez de mover o
  // cursor do texto — é o comportamento do Jump to File original: digita e navega sem
  // trocar de campo.
  if (watched == search_ && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
    const int count = list_->count();
    if (count == 0) return true;
    int row = list_->currentRow();
    row = key->key() == Qt::Key_Down ? row + 1 : row - 1;
    row = std::max(0, std::min(row, count - 1));
    list_->setCurrentRow(row);
    if (QListWidgetItem* item = list_->currentItem(); item != nullptr) {
      list_->scrollToItem(item);
    }
    return true;
  }

  // Enter toca e fecha; Ctrl+Enter enfileira e deixa a janela aberta para enfileirar mais.
  if (enter && (watched == search_ || watched == list_)) {
    activateCurrent(control);
    return true;
  }
  return QDialog::eventFilter(watched, event);
}

void JumpDialog::updateCounter(int shown) {
  counter_->setText(tr("Encontradas %1 de %2").arg(shown).arg(entries_.size()));
}

void JumpDialog::applyFilter() {
  const QStringList tokens = normalize(search_->text()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  list_->clear();

  struct Match {
    int score = 0;
    QString html;
    QString path;
  };
  QVector<Match> matches;

  for (const JumpEntry& entry : entries_) {
    const QString title = normalize(entry.title);
    const QString artist = normalize(entry.artist);
    const QString album = normalize(entry.album);
    const QString haystack = title + QLatin1Char(' ') + artist + QLatin1Char(' ') + album +
                             QLatin1Char(' ') + normalize(entry.path);

    bool all = true;
    int score = 0;
    for (const QString& token : tokens) {
      if (!haystack.contains(token)) {
        all = false;
        break;
      }
      if (title.contains(token)) {
        score += 4;
      } else if (artist.contains(token)) {
        score += 3;
      } else if (album.contains(token)) {
        score += 2;
      } else {
        score += 1;
      }
    }
    if (!all) continue;

    QString label = entry.title;
    if (!entry.artist.isEmpty()) {
      label = label.isEmpty() ? entry.artist : entry.artist + tr(" - ") + label;
    }
    if (label.isEmpty()) label = entry.path;

    matches.push_back(Match{score, highlight(label, tokens), entry.path});
  }

  std::stable_sort(matches.begin(), matches.end(),
                   [](const Match& a, const Match& b) { return a.score > b.score; });

  // Só desenha as primeiras centenas: uma playlist de 10 mil faixas não precisa
  // montar 10 mil itens para a pessoa escolher uma.
  const int limit = 400;
  const int shown = std::min(static_cast<int>(matches.size()), limit);
  for (int i = 0; i < shown; ++i) {
    auto* item = new QListWidgetItem(list_);
    item->setData(kHtmlRole, matches[static_cast<std::size_t>(i)].html);
    item->setData(Qt::UserRole, matches[static_cast<std::size_t>(i)].path);
    item->setToolTip(matches[static_cast<std::size_t>(i)].path);
  }
  if (list_->count() > 0) list_->setCurrentRow(0);
  updateCounter(shown);
}

void JumpDialog::activateCurrent(bool queue) {
  QListWidgetItem* item = list_->currentItem();
  if (item == nullptr) return;
  const QString path = item->data(Qt::UserRole).toString();
  if (path.isEmpty()) return;
  if (queue) {
    emit queueRequested(path);
    return;  // segue aberto: dá para enfileirar várias sem reabrir a busca
  }
  emit playRequested(path);
  close();
}

}  // namespace wamp::ui
