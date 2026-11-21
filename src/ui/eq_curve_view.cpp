#include "eq_curve_view.h"

#include "theme.h"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "wamp/equalizer.h"

namespace wamp::ui {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinFrequency = 20.0;
constexpr double kMaxFrequency = 20000.0;
constexpr double kDbRange = 15.0;  // topo e base da área útil
constexpr double kSampleRate = 48000.0;

const Theme& th() { return activeTheme(); }

}  // namespace

EqCurveView::EqCurveView(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(112);
  setMouseTracking(false);
  setCursor(Qt::ArrowCursor);
  setToolTip(tr("Arraste um ponto para ajustar a banda"));
}

QSize EqCurveView::sizeHint() const { return QSize(360, 112); }

void EqCurveView::setBands(const std::array<double, 10>& bandsDb) {
  bands_ = bandsDb;
  update();
}

QRectF EqCurveView::plotRect() const {
  return QRectF(34.0, 8.0, std::max(10.0, width() - 44.0), std::max(10.0, height() - 26.0));
}

double EqCurveView::frequencyToX(double frequencyHz) const {
  const QRectF r = plotRect();
  const double f = std::clamp(frequencyHz, kMinFrequency, kMaxFrequency);
  const double t = std::log(f / kMinFrequency) / std::log(kMaxFrequency / kMinFrequency);
  return r.left() + t * r.width();
}

double EqCurveView::xToFrequency(double x) const {
  const QRectF r = plotRect();
  const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
  return kMinFrequency * std::pow(kMaxFrequency / kMinFrequency, t);
}

double EqCurveView::dbToY(double db) const {
  const QRectF r = plotRect();
  const double t = (kDbRange - std::clamp(db, -kDbRange, kDbRange)) / (2.0 * kDbRange);
  return r.top() + t * r.height();
}

double EqCurveView::yToDb(double y) const {
  const QRectF r = plotRect();
  const double t = std::clamp((y - r.top()) / r.height(), 0.0, 1.0);
  return kDbRange - t * 2.0 * kDbRange;
}

QPointF EqCurveView::handlePosition(int band) const {
  if (band < 0 || band >= wamp::Equalizer::kBands) return {};
  const double f = wamp::Equalizer::kFrequencies[static_cast<std::size_t>(band)];
  return QPointF(frequencyToX(f), dbToY(bands_[static_cast<std::size_t>(band)]));
}

int EqCurveView::bandAt(const QPointF& position) const {
  int best = -1;
  double bestDistance = 14.0;  // raio de pega do ponto
  for (int band = 0; band < wamp::Equalizer::kBands; ++band) {
    const QPointF handle = handlePosition(band);
    const double distance = std::hypot(handle.x() - position.x(), handle.y() - position.y());
    if (distance < bestDistance) {
      bestDistance = distance;
      best = band;
    }
  }
  return best;
}

void EqCurveView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const QRectF r = plotRect();

  // Grade vertical: frequências "redondas" em escala log.
  const double verticalLines[] = {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
  const QFont small = font();
  QFont tiny = small;
  tiny.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
  painter.setFont(tiny);

  for (double f : verticalLines) {
    const double x = frequencyToX(f);
    painter.setPen(QPen(th().displayGrid, 1));
    painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
  }

  // Grade horizontal: 0, ±6, ±12 dB.
  for (double db : {-12.0, -6.0, 0.0, 6.0, 12.0}) {
    const double y = dbToY(db);
    painter.setPen(QPen(db == 0.0 ? th().border : th().displayGrid, 1));
    painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
  }

  // Rótulos: alguns Hz embaixo e +12/0/−12 à esquerda.
  painter.setPen(th().textMuted);
  const struct {
    double frequency;
    const char* text;
  } labels[] = {{100.0, "100"}, {1000.0, "1k"}, {10000.0, "10k"}};
  for (const auto& entry : labels) {
    const double x = frequencyToX(entry.frequency);
    painter.drawText(QRectF(x - 20.0, r.bottom() + 2.0, 40.0, height() - r.bottom() - 2.0),
                     Qt::AlignHCenter | Qt::AlignTop, QString::fromLatin1(entry.text));
  }
  const QFontMetrics metrics(tiny);
  for (double db : {12.0, 0.0, -12.0}) {
    const QString text = QStringLiteral("%1%2").arg(db > 0.0 ? QStringLiteral("+") : QString())
                             .arg(static_cast<int>(db));
    painter.drawText(QRectF(0.0, dbToY(db) - metrics.height() / 2.0, r.left() - 6.0,
                            metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter, text);
  }

  // A curva: resposta calculada com os mesmos coeficientes usados no áudio.
  const int steps = 220;
  QPainterPath curve;
  for (int step = 0; step <= steps; ++step) {
    const double x = r.left() + r.width() * static_cast<double>(step) / steps;
    const double f = xToFrequency(x);
    const double db = wamp::Equalizer::responseDb(bands_, kSampleRate, f);
    const QPointF point(x, dbToY(db));
    if (step == 0) {
      curve.moveTo(point);
    } else {
      curve.lineTo(point);
    }
  }

  QPainterPath area = curve;
  area.lineTo(r.right(), dbToY(0.0));
  area.lineTo(r.left(), dbToY(0.0));
  area.closeSubpath();
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(th().accent.red(), th().accent.green(), th().accent.blue(), 40));
  painter.drawPath(area);

  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(th().accent, 1.8));
  painter.drawPath(curve);

  // Pontos das bandas — a posição de cada uma é onde você pega para arrastar.
  painter.setPen(QPen(th().windowBg, 1.0));
  painter.setBrush(th().accent.lighter(115));
  for (int band = 0; band < wamp::Equalizer::kBands; ++band) {
    const QPointF handle = handlePosition(band);
    if (handle.x() >= r.left() - 1.0 && handle.x() <= r.right() + 1.0) {
      painter.drawEllipse(handle, 4.0, 4.0);
    }
  }
}

void EqCurveView::mousePressEvent(QMouseEvent* event) {
  const int band = bandAt(event->position());
  if (band < 0) {
    QWidget::mousePressEvent(event);
    return;
  }
  dragging_ = band;
  setCursor(Qt::SizeVerCursor);
  event->accept();
}

void EqCurveView::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ < 0) {
    setCursor(bandAt(event->position()) >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
    return;
  }
  const double db = std::clamp(std::round(yToDb(event->position().y())),
                               wamp::Equalizer::kMinGainDb, wamp::Equalizer::kMaxGainDb);
  emit bandMoved(dragging_, db);
  event->accept();
}

void EqCurveView::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_ >= 0) {
    dragging_ = -1;
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

}  // namespace wamp::ui
