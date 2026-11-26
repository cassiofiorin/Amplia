#pragma once

#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include "wamp/audio_analyzer.h"
#include "wamp/types.h"

class QLabel;
class QTimer;

namespace wamp::ui {

/// O "display" do player: relógio de sete segmentos, estado, informações da faixa e a
/// barra de posição. Desenhado à mão (não é fonte) para escalar em qualquer DPI.
class DisplayPanel final : public QWidget {
  Q_OBJECT

 public:
  /// Margem interna à direita: o contador de tempo termina aqui dentro do display. Quem
  /// desenha algo que deve alinhar com ele (a barra de volume) soma este valor.
  static constexpr int kTimeInset = 16;

  explicit DisplayPanel(QWidget* parent = nullptr);

  void setState(wamp::PlaybackState state);
  void setPositionMs(qint64 positionMs);
  void setDurationMs(qint64 durationMs);
  /// Duas linhas independentes: artista e título. Cada uma só rola se não couber.
  void setTrackInfo(const QString& artist, const QString& title);
  void setTrackText(const QString& text);

  /// Bitrate em kbps, taxa em Hz e canais (2 = estéreo, 1 = mono).
  void setFormat(int kbps, int sampleRate, int channels);
  void setEqualizerActive(bool active);
  /// 0 = desligado, 1 = repetir a lista, 2 = repetir a faixa.
  void setRepeatMode(int mode);
  void setShuffleActive(bool active);

  /// Capa do álbum da faixa atual. Só aparece quando a janela está larga o bastante para
  /// caber entre os medidores e o espectro — expandir revela informação, como no original.
  void setAlbumArt(const QPixmap& art);

  /// Área da capa, ou um retângulo nulo quando ela está escondida pela largura.
  QRect coverRect() const;

  /// Alterna o relógio entre decorrido e restante (como clicar no tempo do original).
  void setShowRemaining(bool remaining);
  /// Quadro do analisador (F2C): barras, medidores e pulso da batida.
  void setVisual(const wamp::AudioAnalyzer::Frame& frame);
  bool showRemaining() const { return remaining_; }

  /// Área da barra de posição, para clique e arrasto.
  QRect barRect() const;
  QRect clockRect() const;
  /// Área de uma das pílulas (0 = EQ, 1 = REP, 2 = RAND).
  QRect tagRect(int index) const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  // Para os testes: o texto que os dígitos estão mostrando e a área de cada dígito.
  QString timeText() const;
  QRect digitRect(int index) const;

 signals:
  /// Pedido de posição, de 0 a 1 da duração (clique ou arrasto na barra).
  void seekRequested(qint64 positionMs);
  /// Clique no relógio: alterna decorrido/restante.
  void timeClicked();
  void equalizerClicked();
  void repeatClicked();
  void shuffleClicked();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void drawDigits(QPainter& painter, const QRect& area, const QString& text) const;

  struct Line {
    QString text;
    int width = 0;
    qreal offset = 0.0;
    bool scrolling = false;
  };
  void updateMarquees();
  void drawLine(QPainter& painter, const Line& line, const QRect& area) const;
  QRect lineRect(int index) const;
  QRect durationRect() const;
  QRect spectrumRect() const;
  int drawInfo(QPainter& painter, int x, int y, const QString& label, const QString& value,
               bool active, const QFontMetrics& metrics);
  void drawGlyph(QPainter& painter, const QRect& area) const;
  void seekTo(const QPoint& pos);
  void resizeEvent(QResizeEvent* event) override;

  wamp::PlaybackState state_ = wamp::PlaybackState::Idle;
  qint64 positionMs_ = 0;
  qint64 durationMs_ = 0;
  QString trackText_;
  QString artistText_;
  int kbps_ = 0;
  int sampleRate_ = 0;
  int channels_ = 0;
  bool equalizer_ = false;
  int repeatMode_ = 0;
  bool shuffle_ = false;
  QRect tagRect_[3];
  QTimer* marqueeTimer_ = nullptr;
  Line lines_[2];
  wamp::AudioAnalyzer::Frame visual_;
  QPixmap albumArt_;
  bool remaining_ = false;
  bool dragging_ = false;
};

}  // namespace wamp::ui
