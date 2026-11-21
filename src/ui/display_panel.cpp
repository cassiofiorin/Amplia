#include "display_panel.h"

#include "theme.h"

#include "icons.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>

namespace wamp::ui {
namespace {

// Cores do display: todas vêm do esquema ativo (theme.h), então trocar a paleta troca o
// painel sem nenhum estado local.
const wamp::ui::Theme& th() { return wamp::ui::activeTheme(); }

/// Bits dos sete segmentos: a (topo), b (sup. dir.), c (inf. dir.), d (base),
/// e (inf. esq.), f (sup. esq.), g (meio).
constexpr std::uint8_t kDigitMask[10] = {
    0x3F,  // 0
    0x06,  // 1
    0x5B,  // 2
    0x4F,  // 3
    0x66,  // 4
    0x6D,  // 5
    0x7D,  // 6
    0x07,  // 7
    0x7F,  // 8
    0x6F,  // 9
};

/// Barra com as pontas chanfradas a 45° — a forma clássica de um segmento de LED.
QPolygonF chamferedBar(const QRectF& r) {
  const double c = std::min(r.width(), r.height()) * 0.5;
  const double l = r.left();
  const double t = r.top();
  const double rr = r.right();
  const double b = r.bottom();
  QPolygonF p;
  p << QPointF(l + c, t) << QPointF(rr - c, t) << QPointF(rr, t + c) << QPointF(rr, b - c)
    << QPointF(rr - c, b) << QPointF(l + c, b) << QPointF(l, b - c) << QPointF(l, t + c);
  return p;
}

/// Cada segmento é um elemento separado, com um fio de vão até os vizinhos: encostar
/// as barras nos cantos fazia o topo parecer um bloco só.
QPolygonF segmentPolygon(const QRectF& cell, int segment) {
  const double w = cell.width();
  const double h = cell.height();
  const double t = std::max(2.0, std::min(w, h) * 0.18);  // espessura
  const double s = std::max(1.0, t * 0.22);               // vão entre segmentos
  const double x0 = cell.left();
  const double y0 = cell.top();
  const double x1 = cell.right();
  const double y1 = cell.bottom();
  const double midY = (y0 + y1) / 2.0;
  // As barras de cima e de baixo não passam da coluna dos segmentos verticais.
  const double barX = x0 + t * 0.9;
  const double barWidth = w - t * 1.8;
  const double bandTop = y0 + t + s;
  const double bandBottom = midY - s;
  const double bandHeight = bandBottom - bandTop;
  const double lowTop = midY + s;
  const double lowBottom = y1 - t - s;
  const double lowHeight = lowBottom - lowTop;

  switch (segment) {
    case 0:  // a
      return chamferedBar(QRectF(barX, y0, barWidth, t));
    case 1:  // b
      return chamferedBar(QRectF(x1 - t, bandTop, t, bandHeight));
    case 2:  // c
      return chamferedBar(QRectF(x1 - t, lowTop, t, lowHeight));
    case 3:  // d
      return chamferedBar(QRectF(barX, y1 - t, barWidth, t));
    case 4:  // e
      return chamferedBar(QRectF(x0, lowTop, t, lowHeight));
    case 5:  // f
      return chamferedBar(QRectF(x0, bandTop, t, bandHeight));
    default:  // g
      return chamferedBar(QRectF(barX, midY - t / 2.0, barWidth, t));
  }
}

QString formatClock(qint64 ms) {
  if (ms < 0) ms = 0;
  const qint64 totalSeconds = ms / 1000;
  const qint64 minutes = totalSeconds / 60;
  const qint64 seconds = totalSeconds % 60;
  const QString tail = QLatin1Char(':') + QStringLiteral("%1").arg(seconds, 2, 10, QLatin1Char('0'));
  // A casa dos minutos existe desde o começo (apagada), então nada se desloca quando o
  // tempo passa de 9:59 e o dígito das dezenas acende.
  if (minutes < 10) return QLatin1Char(' ') + QString::number(minutes) + tail;
  return QString::number(minutes) + tail;
}

}  // namespace

DisplayPanel::DisplayPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("displayPanel"));
  setMinimumSize(320, 86);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // Letreiro: quando o nome da faixa não cabe, ele corre da direita para a esquerda
  // como no original, com um vão antes de reaparecer.
  marqueeTimer_ = new QTimer(this);
  marqueeTimer_->setInterval(33);
  connect(marqueeTimer_, &QTimer::timeout, this, [this] {
    for (int i = 0; i < 2; ++i) {
      Line& line = lines_[i];
      if (!line.scrolling) continue;
      const int span = line.width + 40;
      line.offset += 1.5;
      if (span > 0 && line.offset >= span) line.offset -= span;
    }
    update();
  });
}


void DisplayPanel::setState(wamp::PlaybackState state) {
  if (state_ == state) return;
  state_ = state;
  update();
}

void DisplayPanel::setPositionMs(qint64 positionMs) {
  if (positionMs_ == positionMs) return;
  positionMs_ = positionMs;
  update();
}

void DisplayPanel::setDurationMs(qint64 durationMs) {
  if (durationMs_ == durationMs) return;
  durationMs_ = durationMs;
  update();
}

void DisplayPanel::setTrackInfo(const QString& artist, const QString& title) {
  if (artistText_ == artist && trackText_ == title) return;
  artistText_ = artist;
  trackText_ = title;
  updateMarquees();
  update();
}

void DisplayPanel::setTrackText(const QString& text) {
  setTrackInfo(QString(), text);
}

void DisplayPanel::setFormat(int kbps, int sampleRate, int channels) {
  if (kbps_ == kbps && sampleRate_ == sampleRate && channels_ == channels) return;
  kbps_ = kbps;
  sampleRate_ = sampleRate;
  channels_ = channels;
  update();
}

void DisplayPanel::setEqualizerActive(bool active) {
  if (equalizer_ == active) return;
  equalizer_ = active;
  update();
}

void DisplayPanel::setRepeatMode(int mode) {
  if (repeatMode_ == mode) return;
  repeatMode_ = mode;
  update();
}

void DisplayPanel::setShuffleActive(bool active) {
  if (shuffle_ == active) return;
  shuffle_ = active;
  update();
}

void DisplayPanel::setVisual(const wamp::AudioAnalyzer::Frame& frame) {
  visual_ = frame;
  update();
}

void DisplayPanel::setShowRemaining(bool remaining) {
  if (remaining_ == remaining) return;
  remaining_ = remaining;
  update();
}

QRect DisplayPanel::barRect() const {
  // A duração fica pinada à direita, então a barra termina antes dela.
  const QRect info = durationRect();
  return QRect(10, height() - 15, info.left() - 10 - 8, 4);
}

QRect DisplayPanel::durationRect() const { return QRect(width() - 16 - 40, height() - 22, 40, 16); }

QRect DisplayPanel::lineRect(int index) const {
  // Coluna da esquerda: o texto acompanha a largura do relógio, deixando a direita
  // livre para o visualizador. As duas linhas formam um bloco, sem vão entre elas.
  return QRect(10, 94 + index * 18, 168, 18);
}

QRect DisplayPanel::spectrumRect() const {
  // Coluna da direita, da altura do relógio até a última linha de texto.
  return QRect(206, 38, width() - 206 - 16, 94);
}

QRect DisplayPanel::clockRect() const { return QRect(36, 12, 160, 44); }

QRect DisplayPanel::tagRect(int index) const {
  if (index < 0 || index > 2) return QRect();
  return tagRect_[index];
}

void DisplayPanel::seekTo(const QPoint& pos) {
  if (durationMs_ <= 0) return;
  const QRect bar = barRect();
  const double fraction = std::clamp(static_cast<double>(pos.x() - bar.left()) / std::max(1, bar.width()), 0.0, 1.0);
  emit seekRequested(static_cast<qint64>(fraction * durationMs_));
}

void DisplayPanel::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  for (int i = 0; i < 3; ++i) {
    if (tagRect_[i].adjusted(-3, -3, 3, 3).contains(event->pos())) {
      if (i == 0) emit equalizerClicked();
      else if (i == 1) emit repeatClicked();
      else emit shuffleClicked();
      return;
    }
  }
  if (clockRect().contains(event->pos()) || barRect().adjusted(-4, -6, 4, 6).contains(event->pos())) {
    if (clockRect().contains(event->pos())) {
      emit timeClicked();
      return;
    }
    dragging_ = true;
    seekTo(event->pos());
    return;
  }
  QWidget::mousePressEvent(event);
}

void DisplayPanel::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    seekTo(event->pos());
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void DisplayPanel::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_) {
    seekTo(event->pos());
    dragging_ = false;
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

QSize DisplayPanel::sizeHint() const { return QSize(460, 154); }

QSize DisplayPanel::minimumSizeHint() const { return QSize(340, 150); }

QString DisplayPanel::timeText() const {
  if (remaining_ && durationMs_ > 0) {
    return formatClock(std::max<qint64>(0, durationMs_ - positionMs_));
  }
  return formatClock(positionMs_);
}

QRect DisplayPanel::digitRect(int index) const {
  // Mesma geometria do paint: começa em x=34 (depois do glifo), quatro dígitos.
  const int top = 8;
  const int height = 34;
  const int digitWidth = 26;
  const int digitGap = 5;
  const int colonWidth = 12;
  int x = 34;
  for (int i = 0; i < index && i < 5; ++i) {
    x += (i == 2) ? colonWidth : (digitWidth + digitGap);
  }
  return QRect(x, top, digitWidth, height);
}

void DisplayPanel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateMarquees();
}

void DisplayPanel::updateMarquees() {
  QFont lineFont = font();
  lineFont.setPointSizeF(std::max(8.0, lineFont.pointSizeF()));
  lineFont.setBold(true);
  const QFontMetrics metrics(lineFont);

  const QString texts[2] = {artistText_, trackText_};
  bool anyScrolling = false;
  for (int i = 0; i < 2; ++i) {
    Line& line = lines_[i];
    line.text = texts[i];
    line.width = metrics.horizontalAdvance(line.text);
    const bool needsScroll = line.width > lineRect(i).width();
    if (needsScroll != line.scrolling) line.offset = 0.0;
    line.scrolling = needsScroll;
    anyScrolling = anyScrolling || needsScroll;
  }

  if (anyScrolling && !marqueeTimer_->isActive()) {
    marqueeTimer_->start();
  } else if (!anyScrolling && marqueeTimer_->isActive()) {
    marqueeTimer_->stop();
  }
}

void DisplayPanel::drawLine(QPainter& painter, const Line& line, const QRect& area) const {
  if (line.text.isEmpty()) return;
  QFont lineFont = font();
  lineFont.setPointSizeF(std::max(8.0, lineFont.pointSizeF()));
  lineFont.setBold(true);
  painter.setFont(lineFont);
  painter.setPen(th().displayValue);

  painter.save();
  painter.setClipRect(area);
  const QFontMetrics metrics(lineFont);
  const qreal baseline = area.center().y() + metrics.ascent() / 2.0 - 1.0;
  if (!line.scrolling) {
    painter.drawText(QPointF(area.left(), baseline), line.text);
  } else {
    const qreal x = area.left() - line.offset;
    painter.drawText(QPointF(x, baseline), line.text);
    painter.drawText(QPointF(x + line.width + 40, baseline), line.text);
  }
  painter.restore();
}

void DisplayPanel::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Fundo em gradiente, sem moldura: o painel se dissolve no fundo da janela em vez de
  // parecer uma caixa colada.
  QLinearGradient background(0, 0, 0, height());
  background.setColorAt(0.0, th().displayTop);
  background.setColorAt(1.0, th().displayBottom);
  painter.setPen(Qt::NoPen);
  painter.setBrush(background);
  painter.drawRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 0), 6, 6);

  drawGlyph(painter, QRect(10, 16, 18, 30));
  drawDigits(painter, clockRect(), timeText());

  QFont infoFont = font();
  infoFont.setPointSizeF(std::max(6.0, infoFont.pointSizeF() - 2.5));
  infoFont.setBold(true);
  const QFontMetrics infoMetrics(infoFont);
  painter.setFont(infoFont);

  // Informações alinhadas ao topo dos dígitos, encostadas à direita do painel.
  const int infoY = 12;
  int totalInfo = 0;
  {
    const QString b = kbps_ > 0 ? QString::number(kbps_) : QStringLiteral("---");
    const QString r = sampleRate_ > 0 ? QString::number(sampleRate_ / 1000.0, 'g', 3)
                                      : QStringLiteral("--");
    const QString c = channels_ == 1 ? QStringLiteral("MONO")
                      : channels_ == 2 ? QStringLiteral("STEREO")
                                       : QStringLiteral("---");
    totalInfo = infoMetrics.horizontalAdvance(QStringLiteral("KBPS")) + 6 +
                infoMetrics.horizontalAdvance(b) + 14 +
                infoMetrics.horizontalAdvance(QStringLiteral("KHZ")) + 6 +
                infoMetrics.horizontalAdvance(r) + 14 + infoMetrics.horizontalAdvance(c);
  }
  const int infoLeft = width() - 16 - totalInfo;
  int x = infoLeft;

  const QString bitrateValue = kbps_ > 0 ? QString::number(kbps_) : QStringLiteral("---");
  x = drawInfo(painter, x, infoY, QStringLiteral("KBPS"), bitrateValue, kbps_ > 0, infoMetrics) + 14;

  const QString rateValue = sampleRate_ > 0
                                ? QString::number(sampleRate_ / 1000.0, 'g', 3)
                                : QStringLiteral("--");
  x = drawInfo(painter, x, infoY, QStringLiteral("KHZ"), rateValue, sampleRate_ > 0, infoMetrics) + 14;

  const QString channelText = channels_ == 1 ? QStringLiteral("MONO")
                               : channels_ == 2 ? QStringLiteral("STEREO")
                                                : QStringLiteral("---");
  drawInfo(painter, x, infoY, QString(), channelText, channels_ > 0, infoMetrics);

  // Segunda linha: título à esquerda e, à direita, só o que é estado do app.
  QFont titleFont = font();
  titleFont.setPointSizeF(std::max(8.0, titleFont.pointSizeF()));
  titleFont.setBold(true);
  const QFontMetrics titleMetrics(titleFont);
  painter.setFont(infoFont);

  // LEDs abaixo do relógio, agrupados à esquerda como no original: três itens com vão
  // fixo de 12 px, alinhados ao início dos dígitos.
  const int tagY = 66;
  const int tagHeight = 18;
  const int tagCenterY = tagY + tagHeight / 2;
  const int iconSize = 18;
  const int gap = 12;
  const bool lit[3] = {equalizer_, repeatMode_ != 0, shuffle_};
  const int widths[3] = {infoMetrics.horizontalAdvance(QStringLiteral("EQ")), iconSize, iconSize};
  // Centraliza o grupo no relógio (largura real dos dígitos, não a caixa do clockRect):
  // mesma conta do drawDigits, para o grupo cair no meio do "0:49".
  // Centraliza no que está aceso: a casa do dígito das dezenas existe, mas fica apagada
  // quando a duração tem menos de dez minutos, e contá-la puxaria o grupo para a esquerda.
  const QString clockText = timeText();
  int inkLeft = clockRect().left();
  int inkWidth = 0;
  bool started = false;
  for (const QChar ch : clockText) {
    const int advance = (ch == QLatin1Char(':')) ? 12 : 26;
    if (!started && ch == QLatin1Char(' ')) {
      inkLeft += advance + 5;
      continue;
    }
    started = true;
    inkWidth += advance + (inkWidth > 0 ? 5 : 0);
  }
  const int rowWidth = widths[0] + widths[1] + widths[2] + 2 * gap;
  int tagX = inkLeft + (inkWidth - rowWidth) / 2;
  // Ajuste óptico por glifo: o laço tem a seta em cima e o 1‑2‑3 tem a massa embaixo.
  const double nudge[3] = {0.0, -2.0, -1.5};
  for (int i = 0; i < 3; ++i) {
    const QRect box(tagX, tagCenterY - tagHeight / 2, widths[i], tagHeight);
    tagRect_[i] = box.adjusted(-3, -2, 3, 2);
    const QColor color = lit[i] ? th().displayLit : th().displayValueDim;
    if (i == 0) {
      painter.setPen(color);
      const int baseline = tagCenterY + infoMetrics.capHeight() / 2;
      painter.drawText(QPoint(tagX, baseline), QStringLiteral("EQ"));
    } else {
      const QRectF glyph(tagX, tagCenterY - iconSize / 2.0 + nudge[i], iconSize, iconSize);
      if (i == 1) {
        wamp::ui::paintRepeat(painter, glyph, color, repeatMode_ == 2);
      } else {
        wamp::ui::paintShuffle(painter, glyph, color);
      }
    }
    tagX += widths[i] + gap;
  }

  // Artista e título em duas linhas independentes; cada uma rola se não couber.
  drawLine(painter, lines_[0], lineRect(0));
  drawLine(painter, lines_[1], lineRect(1));

  // Visualizador (F2C): medidores L/R, barras do espectro e o LED da batida.
  {
    const QRect area = spectrumRect();
    // Largura proporcional à área: numa janela alargada os medidores precisam crescer junto
    // com as barras, senão ficam dois fios perdidos à esquerda do espectro.
    const int meterW = std::clamp(area.width() / 16, 8, 22);
    const int meterGap = std::max(3, meterW / 4);
    // Medidores na altura cheia da área, como as barras do espectro: o que não pode competir
    // visualmente é o *trilho*, então ele vira um fio de 2 px no centro — a coluna escura
    // cheia é que fazia o medidor parecer maior que o espectro.
    // Topo compartilhado com a malha do espectro: qualquer diferença de alguns pixels
    // aparece como "o medidor é mais alto que as barras".
    const int meterTop = area.top() + 2;
    const int meterH = area.height() - 2;
    const int barsLeft = area.left() + 2 * meterW + meterGap + 14;
    const int barsSpace = area.right() - barsLeft + 1;
    // Largura inteira igual para todas as bandas; o início é recuado para que a última
    // encoste na borda direita (o mesmo eixo do fim do número da duração).
    const int barW = std::max(2, barsSpace / wamp::AudioAnalyzer::kBands);
    const int barsStart = area.right() + 1 - barW * wamp::AudioAnalyzer::kBands;

    // Malha de fundo alinhada às colunas: uma fileira de pontos no eixo de cada medidor e no
    // eixo de cada banda. O vão entre os medidores e o espectro fica limpo — pontos ali
    // pareciam "colunas que não estão sendo desenhadas".
    for (int m = 0; m < 2; ++m) {
      const int mx = area.left() + m * (meterW + meterGap) + (meterW - 2) / 2;
      for (int y = area.top() + 2; y < area.bottom(); y += 7) {
        painter.fillRect(QRect(mx, y, 2, 2), th().displayGrid);
      }
    }
    for (int i = 0; i < wamp::AudioAnalyzer::kBands; ++i) {
      const int cx = barsStart + i * barW + (barW - 2) / 2;
      for (int y = area.top() + 2; y < area.bottom(); y += 7) {
        painter.fillRect(QRect(cx, y, 2, 2), th().displayGrid);
      }
    }

    const auto drawMeter = [&](int index, float level, float peak) {
      const QRect track(area.left() + index * (meterW + meterGap), meterTop, meterW, meterH);
      // Sem trilho: qualquer coluna escura acima do nível faz o medidor parecer mais alto
      // que as barras. O que se vê é só o nível e o topo segurado.
      // Opacidade acompanha a intensidade: apagado no piano, vivo no forte.
      const int lit = static_cast<int>(track.height() * std::clamp(level, 0.0f, 1.0f));
      if (lit > 0) {
        QColor fill = th().displayLit;
        fill.setAlpha(110 + static_cast<int>(145.0f * std::clamp(level, 0.0f, 1.0f)));
        painter.fillRect(QRect(track.left(), track.bottom() - lit + 1, meterW, lit), fill);
      }
      const int peakY = track.bottom() - static_cast<int>(track.height() * std::clamp(peak, 0.0f, 1.0f));
      if (peak > 0.02f) painter.fillRect(QRect(track.left(), peakY, meterW, 2), th().displayBarPeak);
    };
    drawMeter(0, visual_.levelL, visual_.peakL);
    drawMeter(1, visual_.levelR, visual_.peakR);

    // "L" e "R" na mesma linha de KBPS/KHZ: ficam fora do caminho do nível (que sobe até o
    // topo quando a música é alta) e usam a mesma tipografia das outras informações.
    {
      const QFont previous = painter.font();
      QFont label = infoFont;
      label.setBold(false);
      painter.setFont(label);
      painter.setPen(th().displayLabel);
      const int rowY = infoY + 1;
      const int rowHeight = QFontMetrics(label).height();
      for (int m = 0; m < 2; ++m) {
        const QRect box(area.left() + m * (meterW + meterGap), rowY, meterW, rowHeight);
        painter.drawText(box, Qt::AlignCenter, m == 0 ? QStringLiteral("L") : QStringLiteral("R"));
      }
      painter.setFont(previous);
    }

    for (int i = 0; i < wamp::AudioAnalyzer::kBands; ++i) {
      const int x = barsStart + i * barW;
      const int width = barW - 1;
      const int height = static_cast<int>(area.height() * std::clamp(visual_.bands[i], 0.0f, 1.0f));
      if (height > 0) {
        painter.fillRect(QRect(x, area.bottom() - height + 1, width, height), th().displayBar);
      }
      const int peakY = area.bottom() - static_cast<int>(area.height() * std::clamp(visual_.peaks[i], 0.0f, 1.0f));
      if (visual_.peaks[i] > 0.02f) painter.fillRect(QRect(x, peakY, width, 1), th().displayBarPeak);
    }

    if (visual_.beat > 0.05f) {
      const int size = 4;
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(th().displayLit.red(), th().displayLit.green(), th().displayLit.blue(),
                              static_cast<int>(255 * std::clamp(visual_.beat, 0.0f, 1.0f))));
      painter.drawEllipse(QPointF(area.right() - size, area.bottom() - size), size, size);
    }
  }

  // Duração total pinada à direita, sempre visível (o nome rola, o número fica).
  {
    QFont timeFont = font();
    timeFont.setPointSizeF(std::max(7.5, timeFont.pointSizeF() - 1.0));
    timeFont.setBold(true);
    painter.setFont(timeFont);
    painter.setPen(th().displayLabel);
    const QString total = durationMs_ > 0 ? formatClock(durationMs_) : QStringLiteral("--:--");
    painter.drawText(durationRect(), Qt::AlignRight | Qt::AlignVCenter, total);
  }

  // Barra de posição, fina, no pé do painel.
  const QRect bar = barRect();
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x27, 0x33, 0x24));
  painter.drawRoundedRect(bar, 2, 2);
  if (durationMs_ > 0) {
    const double fraction = std::clamp(static_cast<double>(positionMs_) / durationMs_, 0.0, 1.0);
    QRect filled = bar;
    filled.setWidth(static_cast<int>(bar.width() * fraction));
    painter.setBrush(th().displayLit);
    painter.drawRoundedRect(filled, 2, 2);
  }
}

/// Rótulo em caixa alta e valor, sem moldura — como serigrafia no painel. Devolve o x
/// depois do valor.
int DisplayPanel::drawInfo(QPainter& painter, int x, int y, const QString& label,
                           const QString& value, bool active, const QFontMetrics& metrics) {
  if (!label.isEmpty()) {
    const int labelWidth = metrics.horizontalAdvance(label) + 1;
    painter.setPen(th().displayLabel);
    painter.drawText(QRect(x, y, labelWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, label);
    x += labelWidth + 4;
  }
  const int valueWidth = metrics.horizontalAdvance(value) + 2;
  painter.setPen(active ? th().displayValue : th().displayValueDim);
  painter.drawText(QRect(x, y, valueWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, value);
  return x + valueWidth;
}

void DisplayPanel::drawDigits(QPainter& painter, const QRect& area, const QString& text) const {
  const int digitWidth = 26;
  const int digitGap = 5;
  const int colonWidth = 12;
  int x = area.left();
  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text.at(i);
    if (ch == QLatin1Char(':')) {
      // Dois pontos: dois quadradinhos acesos.
      painter.setPen(Qt::NoPen);
      painter.setBrush(th().displayLit);
      const double cx = x + colonWidth / 2.0 - 1.5;
      painter.drawEllipse(QPointF(cx, area.top() + area.height() * 0.34), 2.4, 2.4);
      painter.drawEllipse(QPointF(cx, area.top() + area.height() * 0.66), 2.4, 2.4);
      x += colonWidth;
      continue;
    }
    if (!ch.isDigit()) {
      x += digitWidth + digitGap;
      continue;
    }
    const int digit = ch.digitValue();
    const std::uint8_t mask = kDigitMask[digit];
    const QRectF cell(x, area.top(), digitWidth, area.height());
    for (int segment = 0; segment < 7; ++segment) {
      const bool lit = (mask >> segment) & 1U;
      const QPolygonF shape = segmentPolygon(cell, segment);
      painter.setPen(Qt::NoPen);
      if (lit) {
        // Halo curto em volta do segmento: é o que dá a impressão de LED, sem borda.
        painter.setBrush(QColor(th().displayLit.red(), th().displayLit.green(), th().displayLit.blue(), 0x22));
        painter.drawPolygon(shape);
      }
      painter.setBrush(lit ? th().displayLit : th().displayOff);
      painter.drawPolygon(shape);
    }
    x += digitWidth + digitGap;
  }
}

void DisplayPanel::drawGlyph(QPainter& painter, const QRect& area) const {
  painter.setPen(Qt::NoPen);
  const bool active = state_ == wamp::PlaybackState::Playing ||
                      state_ == wamp::PlaybackState::Paused ||
                      state_ == wamp::PlaybackState::Loading;
  painter.setBrush(active ? th().displayLit : th().displayDim);
  switch (state_) {
    case wamp::PlaybackState::Playing: {
      QPolygonF triangle;
      triangle << QPointF(area.left() + 4, area.top() + 4)
               << QPointF(area.left() + 4, area.bottom() - 4)
               << QPointF(area.right() - 2, area.center().y());
      painter.drawPolygon(triangle);
      break;
    }
    case wamp::PlaybackState::Paused: {
      painter.drawRoundedRect(QRect(area.left() + 3, area.top() + 4, 5, area.height() - 8), 1, 1);
      painter.drawRoundedRect(QRect(area.left() + 11, area.top() + 4, 5, area.height() - 8), 1, 1);
      break;
    }
    default: {  // Idle/Error: quadrado, como o stop
      painter.drawRoundedRect(QRect(area.left() + 4, area.top() + 6, area.width() - 8,
                                    area.height() - 12),
                              2, 2);
      break;
    }
  }
}


}  // namespace wamp::ui
