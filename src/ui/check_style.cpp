// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#include "check_style.h"

#include <QPainter>
#include <QStyleOption>
#include <QPainterPath>

#include "theme.h"

namespace wamp::ui {
namespace {

QColor opacity(const QColor& color, int percent) {
  QColor copy = color;
  copy.setAlpha(percent * 255 / 100);
  return copy;
}

}  // namespace

CheckStyle::CheckStyle(QStyle* base) : QProxyStyle(base) {}

void CheckStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                               QPainter* painter, const QWidget* widget) const {
  const bool isCheck = element == PE_IndicatorCheckBox;
  const bool isRadio = element == PE_IndicatorRadioButton;
  if (!isCheck && !isRadio) {
    QProxyStyle::drawPrimitive(element, option, painter, widget);
    return;
  }

  const Theme& theme = activeTheme();
  const QRect rect = option->rect.adjusted(0, 0, -1, -1);
  const bool on = (option->state & State_On) != 0;
  const bool off = (option->state & State_NoChange) != 0;  // meio-marcado (três estados)
  const bool enabled = (option->state & State_Enabled) != 0;

  const QColor border = enabled ? (on ? theme.accent : theme.border) : opacity(theme.border, 45);
  const QColor fill = on ? theme.accent : theme.controlBg;
  const QColor mark = readableTextOn(fill);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);

  if (isRadio) {
    painter->setPen(QPen(on ? theme.accent : border, enabled ? 1.5 : 1.0));
    painter->setBrush(enabled ? theme.controlBg : opacity(theme.controlBg, 45));
    painter->drawEllipse(rect.adjusted(1, 1, -1, -1));
    if (on) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(enabled ? theme.accent : opacity(theme.accent, 45));
      painter->drawEllipse(rect.adjusted(4, 4, -4, -4));
    }
  } else {
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(enabled ? fill : opacity(fill, 45));
    painter->drawRoundedRect(rect, 3.0, 3.0);
    if (on || off) {
      // ✔ desenhado à mão: independe de fonte/ícone e escala junto com o indicador.
      QPainterPath path;
      const qreal x = rect.left();
      const qreal y = rect.top();
      const qreal w = rect.width();
      const qreal h = rect.height();
      if (off) {
        painter->setPen(QPen(enabled ? mark : opacity(mark, 45), 2.0));
        painter->drawLine(QPointF(x + w * 0.25, y + h * 0.5), QPointF(x + w * 0.75, y + h * 0.5));
      } else {
        path.moveTo(x + w * 0.22, y + h * 0.52);
        path.lineTo(x + w * 0.42, y + h * 0.72);
        path.lineTo(x + w * 0.80, y + h * 0.28);
        painter->setPen(QPen(enabled ? mark : opacity(mark, 45), 2.0, Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->drawPath(path);
      }
    }
  }
  painter->restore();
}

}  // namespace wamp::ui
