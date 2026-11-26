#include "round_button.h"

#include "theme.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPaintEvent>

#include <cmath>

namespace wamp::ui {
namespace {

}  // namespace

RoundButton::RoundButton(QWidget* parent) : QAbstractButton(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::TabFocus);
}

QSize RoundButton::sizeHint() const { return QSize(30, 30); }

void RoundButton::setGlyph(GlyphPainter painter) {
  glyph_ = std::move(painter);
  update();
}

void RoundButton::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const double diameter = std::min(width(), height()) - 1.0;
  const QRectF circle((width() - diameter) / 2.0, (height() - diameter) / 2.0, diameter,
                      diameter);

  const Theme& th = activeTheme();
  QColor fill(th.controlBg);
  if (isChecked()) {
    fill = th.controlCheckedBg;
  } else if (isDown()) {
    fill = th.controlPressed;
  } else if (underMouse()) {
    fill = th.controlHover;
  }

  // Sem anel: o relevo vem de um gradiente radial sutil na própria cor de preenchimento —
  // o anel de 1 px encostava no fundo e o botão deixava de parecer pressionável.
  QRadialGradient shade(circle.center() - QPointF(0.0, diameter * 0.30), diameter * 1.20);
  shade.setColorAt(0.0, fill.lighter(122));
  shade.setColorAt(0.62, fill);
  shade.setColorAt(1.0, fill.darker(124));
  painter.setPen(Qt::NoPen);
  painter.setBrush(shade);
  painter.drawEllipse(circle);

  // O glifo é pintado direto no quadrado centrado no círculo: passar por QIcon custava
  // um deslocamento de meio pixel que aparecia em 28 px.
  const double side = diameter * 0.62;
  const QRectF target(circle.center().x() - side / 2.0, circle.center().y() - side / 2.0, side,
                      side);
  if (glyph_) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    glyph_(painter, target, isChecked() ? th.accent : th.windowText);
    painter.restore();
  } else if (!text().isEmpty()) {
    QFont font = this->font();
    font.setBold(true);
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.0));
    painter.setFont(font);
    painter.setPen(isChecked() ? th.accent : th.windowText);
    painter.drawText(circle, Qt::AlignCenter, text());
  }
}

}  // namespace wamp::ui
