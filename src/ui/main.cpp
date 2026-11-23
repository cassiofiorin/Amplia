#include <QApplication>
#include <QFileInfo>
#include <QDebug>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QSettings>

#include <memory>

#include "main_window.h"
#include "translator.h"

namespace {

// Instância única: com "fechar para a bandeja" o app vira um residente, e abrir de
// novo não pode criar um segundo player disputando o nome do MPRIS. O nome vira
// socket abstrato no Linux, então não depende de arquivo no disco nem do /tmp do
// sandbox do Flatpak.
constexpr auto kInstanceName = "io.github.cassiofiorin.Amplia.instance";

}  // namespace

int main(int argc, char** argv) {
  // Backend gráfico: Wayland nativo por padrão. Posição de janela só existe no X11/XWayland
  // (o protocolo xdg_toplevel não tem pedido de posição), então quem marcou a opção nas
  // Preferências abre em XWayland. A variável precisa entrar antes do QApplication: é o
  // plugin de plataforma que a lê na construção.
  // Antes de qualquer leitura: um INI com seções repetidas devolve valores trocados.
  wamp::ui::sanitizeSettingsFile();
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    QSettings early(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
    qInfo("amplia: xwaylandPosition na INI = %s",
          early.value(QStringLiteral("window/xwaylandPosition"), false).toBool() ? "true"
                                                                              : "false");
    if (early.value(QStringLiteral("window/xwaylandPosition"), false).toBool()) {
      qputenv("QT_QPA_PLATFORM", "xcb");
      qInfo("amplia: XWayland pedido pela preferência (arquivo %s)",
            qPrintable(wamp::ui::settingsFileForTests()));
    }
  }

  QApplication app(argc, argv);
  qInfo("amplia: plataforma %s", qPrintable(QGuiApplication::platformName()));
  QApplication::setApplicationName(QStringLiteral("Amplia"));
  QApplication::setApplicationDisplayName(QStringLiteral("Amplia"));
  QApplication::setOrganizationName(QStringLiteral("cassiofiorin"));
  QApplication::setDesktopFileName(QStringLiteral("io.github.cassiofiorin.Amplia"));

  // Idioma: precisa entrar antes de qualquer widget nascer — as strings em `tr()` são
  // resolvidas na construção do objeto, não no show.
  wamp::ui::installTranslators(wamp::ui::languageFromSettings());

  // Arquivos passados na linha de comando ("abrir com…" e `wamp a.mp3 b.mp3`):
  // o primeiro toca, os demais entram na playlist. `--playlist` abre já expandido e
  // `--preferences` abre direto nas preferências (útil para configurar o scrobbling).
  QStringList files;
  bool showPlaylist = false;
  bool showPreferences = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QLatin1String("--playlist")) {
      showPlaylist = true;
    } else if (arg == QLatin1String("--preferences")) {
      showPreferences = true;
    } else if (!arg.startsWith(QLatin1Char('-'))) {
      files << arg;
    }
  }
  // Com arquivos na linha de comando a sessão anterior não é restaurada: o que veio
  // de fora é a intenção do usuário.
  // Qt usa o ícone instalado (share/icons) para a janela e para a bandeja.
  QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.cassiofiorin.Amplia")));

  // Já existe um Amplia tocando? Manda os arquivos para ele e sai — sem segunda janela,
  // sem segundo dono do nome MPRIS.
  {
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(kInstanceName));
    if (probe.waitForConnected(300)) {
      QByteArray payload;
      for (const QString& file : files) payload += file.toUtf8() + '\n';
      probe.write(payload);
      probe.flush();
      probe.waitForBytesWritten(300);
      probe.disconnectFromServer();
      return 0;
    }
  }

  // A janela é recriável: "Reiniciar agora" (idioma) instala o tradutor e constrói uma
  // janela nova no mesmo processo. Nada de segundo processo: dentro do Flatpak não há
  // `flatpak run`, e o socket de instância única viraria corrida.
  auto current = std::make_shared<std::unique_ptr<MainWindow>>(
      std::make_unique<MainWindow>(nullptr, files.isEmpty()));
  (*current)->setRestartHandler([current] {
    (*current)->saveSettingsNow();
    wamp::ui::installTranslators(wamp::ui::languageFromSettings());
    auto fresh = std::make_unique<MainWindow>(nullptr, true);  // sessão anterior volta
    fresh->show();
    MainWindow* previous = current->release();  // troca a posse antes de destruir a antiga
    *current = std::move(fresh);
    previous->deleteLater();
  });
  (*current)->show();

  // Escape de depuração: exercita o caminho de "Reiniciar agora" sem clicar no diálogo.
  if (qEnvironmentVariableIsSet("AMPLIA_RESTART_TEST")) {
    QTimer::singleShot(1200, &app, [&app, current] {
      QSettings settings(wamp::ui::settingsFileForTests(), QSettings::IniFormat);
      settings.setValue(QStringLiteral("general/language"),
                        qEnvironmentVariable("AMPLIA_RESTART_TEST"));
      settings.sync();
      if (*current) (*current)->restartForLanguage();
    });
  }

  // Servidor da instância única: cada conexão traz caminhos (um por linha) que
  // entram na playlist da janela que já está aberta.
  QLocalServer server;
  auto pending = std::make_shared<QByteArray>();
  QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, current, pending] {
    while (QLocalSocket* socket = server.nextPendingConnection()) {
      pending->clear();
      QObject::connect(socket, &QLocalSocket::readyRead, socket,
                       [socket, pending] { pending->append(socket->readAll()); });
      QObject::connect(socket, &QLocalSocket::disconnected, socket, [socket, current, pending] {
        const QStringList incoming =
            QString::fromUtf8(*pending).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (incoming.isEmpty() || !*current) {
          socket->deleteLater();
          return;
        }
        (*current)->addFiles(incoming);
        (*current)->show();
        (*current)->raise();
        (*current)->activateWindow();
        socket->deleteLater();
      });
    }
  });
  if (!server.listen(QString::fromLatin1(kInstanceName))) {
    qWarning("amplia: instância única indisponível (%s); abrir duas vai duplicar o player",
             qPrintable(server.errorString()));
  }

  // Pasta na linha de comando entra pela varredura; arquivo toca direto.
  const bool firstIsFolder = !files.isEmpty() && QFileInfo(files.first()).isDir();
  if (firstIsFolder) {
    (*current)->addFiles(files);
  } else if (!files.isEmpty()) {
    (*current)->openFile(files.first());
    if (files.size() > 1) (*current)->addFiles(files.mid(1));
  }
  if (showPlaylist) (*current)->showPlaylist(true);
  if (showPreferences) {
    QMetaObject::invokeMethod(current->get(), "openPreferences", Qt::QueuedConnection);
  }

  return app.exec();
}
