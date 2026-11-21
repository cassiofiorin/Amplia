// Sonda de bandeja: descobre se QSystemTrayIcon funciona dentro do Flatpak/Wayland
// antes de investir no agente. Não faz parte do app.
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>

#include <cstdio>

namespace {

// Ícone simples (raio laranja em fundo escuro), sem depender de tema.
QIcon makeIcon() {
  QPixmap pixmap(64, 64);
  pixmap.fill(QColor(0x1b, 0x1d, 0x21));
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  QPolygonF bolt;
  bolt << QPointF(38, 6) << QPointF(18, 36) << QPointF(31, 36) << QPointF(26, 58)
       << QPointF(46, 28) << QPointF(33, 28);
  painter.setBrush(QColor(0xff, 0x9f, 0x1c));
  painter.drawPolygon(bolt);
  painter.end();
  return QIcon(pixmap);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Amplia tray probe"));
  QApplication::setApplicationDisplayName(QStringLiteral("Amplia tray probe"));

  std::printf("isSystemTrayAvailable=%d\n", QSystemTrayIcon::isSystemTrayAvailable() ? 1 : 0);

  QSystemTrayIcon tray(makeIcon());
  tray.setToolTip(QStringLiteral("Amplia (sonda)"));
  QMenu menu;
  menu.addAction(QStringLiteral("Mostrar janela"));
  menu.addAction(QStringLiteral("Sair"));
  tray.setContextMenu(&menu);
  tray.show();

  std::printf("visible=%d supportsMessages=%d\n", tray.isVisible() ? 1 : 0,
              tray.supportsMessages() ? 1 : 0);
  std::fflush(stdout);

  QTimer::singleShot(25000, &app, &QApplication::quit);
  return app.exec();
}
