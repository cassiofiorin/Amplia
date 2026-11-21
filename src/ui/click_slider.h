#pragma once

#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

namespace wamp::ui {

/// QSlider que pula para o ponto clicado.
///
/// O QSlider do Qt só muda de página no clique (pageStep): num slider de milissegundos
/// isso vira salto de minutos, e num de volume o clique parece não fazer nada. Aqui o
/// clique posiciona o valor e o arrasto continua de onde clicou — para o seek, o
/// setSliderDown(true/false) ainda emite sliderPressed/sliderReleased.
class ClickSlider final : public QSlider {
 public:
  explicit ClickSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
      : QSlider(orientation, parent) {}

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QSlider::mousePressEvent(event);
      return;
    }
    setSliderDown(true);  // emite sliderPressed: a UI para de empurrar a posição
    setValue(valueFromPos(static_cast<int>(event->position().x())));
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (!isSliderDown()) {
      QSlider::mouseMoveEvent(event);
      return;
    }
    setValue(valueFromPos(static_cast<int>(event->position().x())));
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || !isSliderDown()) {
      QSlider::mouseReleaseEvent(event);
      return;
    }
    setValue(valueFromPos(static_cast<int>(event->position().x())));
    setSliderDown(false);  // emite sliderReleased: a UI faz o seek
    event->accept();
  }

 private:
  int valueFromPos(int x) const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    return QStyle::sliderValueFromPosition(opt.minimum, opt.maximum,
                                           x - groove.x() - handle.width() / 2,
                                           groove.width() - handle.width(), opt.upsideDown);
  }
};

}  // namespace wamp::ui
