// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#pragma once

#include <QProxyStyle>

namespace wamp::ui {

/// Desenha o indicador de QCheckBox/QRadioButton com a marca clássica (✔ e ponto), nas
/// cores do tema. O QSS antigo pintava a caixa cheia e não desenhava glifo nenhum, o que
/// lia como "bloco colorido" em vez de "marcado".
class CheckStyle final : public QProxyStyle {
 public:
  explicit CheckStyle(QStyle* base);
  void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override;
};

}  // namespace wamp::ui
