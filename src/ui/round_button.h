#pragma once

#include <QAbstractButton>

#include <functional>

namespace wamp::ui {

/// Botão circular desenhado à mão: o QSS não fecha círculo em todos os tamanhos, e o
/// desenho aqui garante o mesmo formato em 26 e 30 px, com estados de hover/pressionado.
class RoundButton final : public QAbstractButton {
  Q_OBJECT

 public:
  using GlyphPainter = std::function<void(QPainter&, const QRectF&, const QColor&)>;

  explicit RoundButton(QWidget* parent = nullptr);

  /// Glifo desenhado pelo próprio botão: pinta na cor do tema, sem passar por QIcon.
  void setGlyph(GlyphPainter painter);

  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  GlyphPainter glyph_;
};

}  // namespace wamp::ui
