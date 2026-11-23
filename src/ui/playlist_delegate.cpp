#include "playlist_delegate.h"

#include "theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

#include <algorithm>

PlaylistDelegate::PlaylistDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void PlaylistDelegate::setTrackCount(int count) { count_ = count; }

QSize PlaylistDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  QSize size = QStyledItemDelegate::sizeHint(option, index);
  size.setHeight(std::max(size.height(), option.fontMetrics.height() + 6));
  return size;
}

void PlaylistDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
  QStyleOptionViewItem opt(option);
  initStyleOption(&opt, index);
  const QString label = opt.text;

  // Só fundo/seleção: `QStyledItemDelegate::paint` re-extrai o texto do modelo e
  // desenharia a legenda fora de lugar, então o controle é chamado direto no estilo.
  opt.text.clear();
  QStyle* style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

  const QFontMetrics fm(opt.font);
  const int digit = fm.horizontalAdvance(QLatin1Char('0'));
  const wamp::ui::Theme& th = wamp::ui::activeTheme();
  const QColor numberColorOpt(th.listNumber);
  const QColor numberColorSelected(th.listSelectedText);
  const QColor queueColorOpt(th.accent);
  const QColor queueColorSelected(th.accent.lighter(115));

  const int digits = static_cast<int>(std::max<qsizetype>(1, QString::number(std::max(count_, 1)).size()));
  const int gutter = digit * digits + 12;
  const int queueWidth = digit * 3 + 4;
  const int durationWidth = fm.horizontalAdvance(QStringLiteral("00:00:00")) + 10;

  painter->save();
  painter->setClipRect(opt.rect);

  const bool selected = opt.state & QStyle::State_Selected;

  // Número da faixa, à direita do gutter para as unidades ficarem alinhadas.
  const QRect numberRect(opt.rect.left() + 4, opt.rect.top(), gutter - 12, opt.rect.height());
  painter->setPen(selected ? numberColorSelected : numberColorOpt);
  painter->drawText(numberRect, Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(index.row() + 1));

  // Rótulo: elide no meio para preservar "Artista" e o fim do título.
  const QRect labelRect = opt.rect.adjusted(gutter, 0, -(queueWidth + durationWidth + 6), 0);
  // Texto da linha selecionada na cor do estilo: com stylesheet a paleta não acompanha
  // o fundo desenhado, e HighlightedText sairia escuro sobre o destaque.
  // "Missing file": o aviso já vem no texto (⚠), aqui só damos a cor de alerta.
  const bool missing = index.data(kMissingRole).toInt() != 0;
  const QColor labelColor = selected ? th.listSelectedText : (missing ? th.accent : th.listText);
  painter->setPen(labelColor);
  painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(label, Qt::ElideMiddle, labelRect.width()));

  // Duração, alinhada à direita da sua coluna: some quando a tag ainda não chegou.
  const QString duration = index.data(kDurationRole).toString();
  if (!duration.isEmpty()) {
    const QRect durationRect(opt.rect.right() - queueWidth - durationWidth + 2, opt.rect.top(),
                             durationWidth - 8, opt.rect.height());
    painter->setPen(selected ? numberColorSelected : numberColorOpt);
    painter->drawText(durationRect, Qt::AlignRight | Qt::AlignVCenter, duration);
  }

  // A posição na fila não é mais desenhada aqui: a fila virou lista própria (D-117).

  painter->restore();
}
