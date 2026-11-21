#include "icons.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>
#include <functional>

namespace wamp::ui {
namespace {

constexpr double kBox = 20.0;

/// Pixmap com o dobro da resolução: em tela HiDPI o ícone continua nítido.
QPixmap render(const std::function<void(QPainter&)>& draw) {
  const int scale = 2;
  QPixmap pixmap(static_cast<int>(kBox) * scale, static_cast<int>(kBox) * scale);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(scale, scale);
  draw(painter);
  painter.end();
  pixmap.setDevicePixelRatio(scale);
  return pixmap;
}

QIcon from(const std::function<void(QPainter&)>& draw, const QColor& color) {
  return QIcon(render([&draw, color](QPainter& p) {
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    draw(p);
  }));
}

QPainterPath arrowHead(const QPointF& tip, double angleDeg) {
  QPainterPath path;
  const double size = 3.6;
  const double a = angleDeg * M_PI / 180.0;
  path.moveTo(tip);
  path.lineTo(tip + QPointF(std::cos(a + M_PI * 0.75) * size, std::sin(a + M_PI * 0.75) * size));
  path.lineTo(tip + QPointF(std::cos(a - M_PI * 0.75) * size, std::sin(a - M_PI * 0.75) * size));
  path.closeSubpath();
  return path;
}

QPainterPath repeatLoop() {
  // Laço com um vão em cima, onde entra a seta: lê como "repete" mesmo a 18 px.
  const QRectF box(4.4, 5.2, 11.2, 9.4);
  QPainterPath path;
  path.arcMoveTo(box, 108.0);
  path.arcTo(box, 108.0, -300.0);
  return path;
}

QPainterPath repeatArrow() {
  QPainterPath arrow;
  arrow.moveTo(9.4, 3.6);
  arrow.lineTo(11.8, 5.4);
  arrow.lineTo(9.4, 7.2);
  arrow.closeSubpath();
  return arrow;
}

/// Escala o desenho (feito em 20x20) para a área pedida.
void inBox(QPainter& painter, const QRectF& box, const std::function<void(QPainter&)>& draw) {
  painter.save();
  painter.translate(box.topLeft());
  painter.scale(box.width() / kBox, box.height() / kBox);
  draw(painter);
  painter.restore();
}

}  // namespace

void paintPlay(QPainter& p, const QRectF& box, const QColor& color) {
  inBox(p, box, [&color](QPainter& q) {
    QPainterPath path;
    path.moveTo(5.6, 4.5);
    path.lineTo(14.6, 10.0);
    path.lineTo(5.6, 15.5);
    path.closeSubpath();
    q.fillPath(path, color);
  });
}

void paintPause(QPainter& p, const QRectF& box, const QColor& color) {
  inBox(p, box, [&color](QPainter& q) {
    q.setPen(Qt::NoPen);
    q.setBrush(color);
    q.drawRoundedRect(QRectF(6.2, 4.6, 2.6, 10.8), 1.0, 1.0);
    q.drawRoundedRect(QRectF(11.2, 4.6, 2.6, 10.8), 1.0, 1.0);
  });
}

void paintStop(QPainter& p, const QRectF& box, const QColor& color) {
  inBox(p, box, [&color](QPainter& q) {
    q.setPen(Qt::NoPen);
    q.setBrush(color);
    q.drawRoundedRect(QRectF(6.0, 6.0, 8.0, 8.0), 1.4, 1.4);
  });
}

void paintPrevious(QPainter& p, const QRectF& box, const QColor& color) {
  inBox(p, box, [&color](QPainter& q) {
    q.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
    q.drawLine(QPointF(6.0, 5.0), QPointF(6.0, 15.0));
    QPainterPath path;
    path.moveTo(14.5, 5.2);
    path.lineTo(8.6, 10.0);
    path.lineTo(14.5, 14.8);
    path.closeSubpath();
    q.fillPath(path, color);
  });
}

void paintNext(QPainter& p, const QRectF& box, const QColor& color) {
  inBox(p, box, [&color](QPainter& q) {
    q.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
    q.drawLine(QPointF(14.0, 5.0), QPointF(14.0, 15.0));
    QPainterPath path;
    path.moveTo(5.5, 5.2);
    path.lineTo(11.4, 10.0);
    path.lineTo(5.5, 14.8);
    path.closeSubpath();
    q.fillPath(path, color);
  });
}

void paintShuffle(QPainter& p, const QRectF& box, const QColor& color) {
  // 1, 2 e 3 em sequência com a seta cruzando. Dígitos em matriz 3x5 — texto vira borrão
  // nesse tamanho — e a seta embaixo, que é o que dá a leitura de "fora de ordem".
  inBox(p, box, [&color](QPainter& q) {
    static const unsigned char kOne[5] = {0b010, 0b110, 0b010, 0b010, 0b111};
    static const unsigned char kTwo[5] = {0b111, 0b001, 0b111, 0b100, 0b111};
    static const unsigned char kThree[5] = {0b111, 0b001, 0b011, 0b001, 0b111};
    const unsigned char* digits[3] = {kOne, kTwo, kThree};
    const double px = 1.35;
    const double gap = 1.3;
    double x = 2.5;
    q.setPen(Qt::NoPen);
    q.setBrush(color);
    for (int d = 0; d < 3; ++d) {
      const double y = 4.2;
      for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
          if ((digits[d][row] >> (2 - col)) & 1) {
            q.drawRect(QRectF(x + col * px, y + row * px, px + 0.1, px + 0.1));
          }
        }
      }
      x += 3 * px + gap;
    }

    // Seta reta por baixo dos dígitos: uma curva simétrica aqui virava um "sorriso".
    const double arrowY = 16.2;
    q.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
    q.drawLine(QPointF(2.4, arrowY), QPointF(16.0, arrowY));
    q.fillPath(arrowHead(QPointF(17.4, arrowY), 0.0), color);
  });
}

void paintRepeat(QPainter& p, const QRectF& box, const QColor& color, bool one) {
  inBox(p, box, [&color, one](QPainter& q) {
    q.setPen(QPen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    q.setBrush(Qt::NoBrush);
    q.drawPath(repeatLoop());
    q.fillPath(repeatArrow(), color);
    if (one) {
      QFont font = q.font();
      font.setBold(true);
      font.setPointSizeF(6.0);
      q.setFont(font);
      q.setPen(color);
      q.drawText(QRectF(6.0, 6.6, 8.0, 7.0), Qt::AlignCenter, QStringLiteral("1"));
    }
  });
}

void paintVolume(QPainter& p, const QRectF& box, const QColor& color, bool muted) {
  inBox(p, box, [&color, muted](QPainter& q) {
    QPainterPath speaker;
    speaker.moveTo(4.5, 8.0);
    speaker.lineTo(7.0, 8.0);
    speaker.lineTo(10.0, 5.2);
    speaker.lineTo(10.0, 14.8);
    speaker.lineTo(7.0, 12.0);
    speaker.lineTo(4.5, 12.0);
    speaker.closeSubpath();
    q.fillPath(speaker, color);
    q.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    if (muted) {
      q.drawLine(QPointF(12.6, 8.0), QPointF(16.4, 12.0));
      q.drawLine(QPointF(16.4, 8.0), QPointF(12.6, 12.0));
    } else {
      q.setBrush(Qt::NoBrush);
      q.drawArc(QRectF(9.6, 7.0, 5.0, 6.0), -60.0 * 16, 120.0 * 16);
      q.drawArc(QRectF(9.6, 5.2, 8.0, 9.6), -55.0 * 16, 110.0 * 16);
    }
  });
}

QIcon playIcon() { return from([](QPainter& p) { paintPlay(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon pauseIcon() { return from([](QPainter& p) { paintPause(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon stopIcon() { return from([](QPainter& p) { paintStop(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon previousIcon() { return from([](QPainter& p) { paintPrevious(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon nextIcon() { return from([](QPainter& p) { paintNext(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon shuffleIcon() { return from([](QPainter& p) { paintShuffle(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8)); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon repeatIcon() { return from([](QPainter& p) { paintRepeat(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8), false); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon repeatOneIcon() { return from([](QPainter& p) { paintRepeat(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8), true); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon volumeIcon() { return from([](QPainter& p) { paintVolume(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8), false); }, QColor(0xe8, 0xe8, 0xe8)); }
QIcon mutedIcon() { return from([](QPainter& p) { paintVolume(p, QRectF(0, 0, kBox, kBox), QColor(0xe8, 0xe8, 0xe8), true); }, QColor(0xe8, 0xe8, 0xe8)); }

}  // namespace wamp::ui
