#pragma once

#include <QColor>
#include <QIcon>
#include <QRectF>

class QPainter;

namespace wamp::ui {

/// Glifos desenhados à mão em coordenadas de 20x20, escalados para a área pedida.
/// Servem tanto para os botões (via QIcon) quanto para o display, que precisa pintar
/// na cor do LED (aceso ou apagado).
void paintPlay(QPainter& painter, const QRectF& box, const QColor& color);
void paintPause(QPainter& painter, const QRectF& box, const QColor& color);
void paintStop(QPainter& painter, const QRectF& box, const QColor& color);
void paintPrevious(QPainter& painter, const QRectF& box, const QColor& color);
void paintNext(QPainter& painter, const QRectF& box, const QColor& color);
void paintShuffle(QPainter& painter, const QRectF& box, const QColor& color);
void paintRepeat(QPainter& painter, const QRectF& box, const QColor& color, bool one);
void paintVolume(QPainter& painter, const QRectF& box, const QColor& color, bool muted);

QIcon playIcon();
QIcon pauseIcon();
QIcon stopIcon();
QIcon previousIcon();
QIcon nextIcon();
QIcon shuffleIcon();
QIcon repeatIcon();
QIcon repeatOneIcon();
QIcon volumeIcon();
QIcon mutedIcon();

}  // namespace wamp::ui
