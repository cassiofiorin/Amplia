#pragma once

#include <array>

#include <QPointF>
#include <QWidget>

namespace wamp::ui {

/// Desenha a resposta em frequência das dez bandas — a curva que a pessoa ouve —
/// sobre uma grade logarítmica. Arrastar um ponto move a banda correspondente.
class EqCurveView final : public QWidget {
  Q_OBJECT

 public:
  explicit EqCurveView(QWidget* parent = nullptr);

  /// Ganhos das dez bandas, em dB. O pré-amp não entra na curva: ele muda o nível,
  /// não a forma — e assim o ponto de arrasto de cada banda cai sobre a própria curva.
  void setBands(const std::array<double, 10>& bandsDb);

  /// Centro do ponto de arrasto da banda (para o teste enviar o clique no lugar certo).
  QPointF handlePosition(int band) const;
  int bandAt(const QPointF& position) const;

  QSize sizeHint() const override;

 signals:
  void bandMoved(int band, double gainDb);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  QRectF plotRect() const;
  double frequencyToX(double frequencyHz) const;
  double xToFrequency(double x) const;
  double dbToY(double db) const;
  double yToDb(double y) const;

  std::array<double, 10> bands_{};
  int dragging_ = -1;
};

}  // namespace wamp::ui
