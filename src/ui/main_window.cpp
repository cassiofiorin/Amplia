#include "main_window.h"

#include "wamp/metadata.h"
#include "wamp/mpris_server.h"
#include "wamp/scrobbler.h"
#include "wamp/equalizer.h"
#include "wamp/playlist_file.h"
#include "wamp/tag_editor.h"

#include "equalizer_dialog.h"
#include "display_panel.h"
#include "icons.h"

#include <cmath>

#include <QElapsedTimer>
#include <QPainter>
#include <QProgressDialog>
#include <QTimer>
#include "round_button.h"

#include "format_support.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QCollator>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDirIterator>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QMenuBar>
#include <QMetaObject>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QShortcut>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// O Wayland não deixa o cliente posicionar janela: o compositor decide. Só tentamos
// salvar/restaurar posição onde isso existe de fato (X11/XWayland).
bool windowPositionSupported() {
  const QString platform = QGuiApplication::platformName();
  return !platform.contains(QLatin1String("wayland"), Qt::CaseInsensitive);
}

using wamp::ui::isAudioFile;
using wamp::ui::isPlaylistFile;

/// Pasta vira os arquivos de áudio que estão dentro dela, recursivamente e em ordem
/// natural (CD1, CD2, CD10 — e não CD1, CD10, CD2). Arquivos passados direto ficam na
/// ordem em que vieram, e nada entra duas vezes.
QStringList expandAudioPaths(const QStringList& paths, int* folders, int* ignored) {
  QStringList result;
  QSet<QString> seen;
  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);

  for (const QString& path : paths) {
    const QFileInfo info(path);
    if (!info.isDir()) {
      if (seen.contains(path)) continue;
      seen.insert(path);
      result << path;
      continue;
    }

    QStringList found;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString file = it.next();
      if (isAudioFile(file)) {
        found << file;
      } else if (ignored != nullptr) {
        ++(*ignored);
      }
    }
    std::sort(found.begin(), found.end(), [&collator](const QString& a, const QString& b) {
      return collator.compare(a, b) < 0;
    });
    if (folders != nullptr) ++(*folders);
    for (const QString& file : found) {
      if (seen.contains(file)) continue;
      seen.insert(file);
      result << file;
    }
  }
  return result;
}

}  // namespace

namespace {

// Arrasto vindo de compartilhamento aberto no gerenciador (smb://, sftp://…) chega como
// endereço, não como arquivo: o app não tem como abrir sem a pasta estar montada no
// sistema. Silêncio aqui faz o usuário concluir que o arrasto está quebrado.
// Aviso de arrasto vindo de rede: o KIO entrega um atalho (smb://, sftp://…), que
// não existe como caminho dentro do sandbox. Uma caixa com botão é mais clara que
// a barra de status, que some antes de a pessoa ler.
void warnAboutNetworkDrop(QWidget* parent, const QList<QUrl>& urls, int rejected) {
  if (parent == nullptr || rejected <= 0 || urls.isEmpty()) return;

  QStringList schemes;
  for (const QUrl& url : urls) {
    if (!url.toLocalFile().isEmpty() || url.scheme().isEmpty()) continue;
    if (!schemes.contains(url.scheme())) schemes << url.scheme();
  }
  if (schemes.isEmpty()) return;

  QStringList listed;
  for (const QString& scheme : schemes) listed << scheme + QStringLiteral("://");

  auto* box = new QMessageBox(parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setIcon(QMessageBox::Information);
  box->setWindowTitle(QStringLiteral("Amplia"));
  box->setText(QCoreApplication::translate("MainWindow", "%1 arquivo(s) vieram de um atalho de rede").arg(rejected));
  box->setInformativeText(
      QCoreApplication::translate("MainWindow",
          "Os endereços vieram por %1 — um atalho do ambiente, não um arquivo do "
          "disco. O Amplia abre caminhos do sistema, e o atalho de rede não existe "
          "dentro do sandbox do Flatpak.\n\n"
          "Para tocar: monte o compartilhamento (ou copie os arquivos para o disco) "
          "e arraste da pasta montada.")
          .arg(listed.join(QCoreApplication::translate("MainWindow", ", "))));
  box->addButton(QCoreApplication::translate("MainWindow", "Entendido"),
                 QMessageBox::AcceptRole);
  box->show();
  box->raise();
}

}  // namespace

#include "file_info_dialog.h"
#include "jump_dialog.h"
#include "library_window.h"
#include "playlist_delegate.h"
#include "preferences_dialog.h"
#include "translator.h"
#include "version.h"
#include "theme.h"
#include "theme_editor.h"
#include "theme_store.h"
#include "theme_store.h"

#include "wamp/metadata_scanner.h"
#include "wamp/tag_cache.h"
#include "wamp/player_engine.h"
#include "wamp/playlist.h"
#include "wamp/replay_gain.h"

namespace {

QString formatTime(std::int64_t ms) {
  if (ms < 0) ms = 0;
  const std::int64_t totalSeconds = ms / 1000;
  const std::int64_t minutes = totalSeconds / 60;
  const std::int64_t seconds = totalSeconds % 60;
  return QStringLiteral("%1:%2")
      .arg(minutes)
      .arg(seconds, 2, 10, QLatin1Char('0'));
}

/// Configuração: um INI em ~/.config/amplia/amplia.ini, como o Winamp fazia.
QString settingsFile() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.config");
  dir += QStringLiteral("/amplia");
  QDir().mkpath(dir);
  return dir + QStringLiteral("/amplia.ini");
}

QString baseName(const std::string& path) {
  return QFileInfo(QString::fromStdString(path)).fileName();
}

/// Altura que a lista ocupa embaixo enquanto o PL está ligado.
constexpr int kPlaylistHeight = 300;
/// Piso da janela compacta: display (120) + transporte + barra de status + margens.
constexpr int kCompactFloor = 274;

/// Anúncio de "ouvindo agora" só depois de alguns segundos tocando: faixa que nem chegou a
/// tocar não aparece no perfil.
constexpr std::int64_t kNowPlayingDelayMs = 5000;


}  // namespace

MainWindow::MainWindow(QWidget* parent, bool restoreSession)
    : QMainWindow(parent),
      engine_(std::make_unique<wamp::PlayerEngine>()),
      playlist_(std::make_unique<wamp::Playlist>()) {
  restoreSession_ = restoreSession;
  sessionRestored_ = restoreSession;
  applyThemeFromSettings();
  setWindowTitle(QStringLiteral("Amplia"));
  setAcceptDrops(true);
  buildUi();
  buildMenus();
  wireShortcuts();
  resize(460, 236);

  // Callbacks do engine chegam da thread de controle: tudo é reenviado para a thread
  // da interface antes de tocar em widgets.
  // MPRIS2: o player aparece para o controle de mídia do sistema (e para o playerctl).
  mpris_ = std::make_unique<wamp::MprisServer>(
      wamp::MprisHooks{
          .state = [this] { return engine_->state(); },
          .positionMs = [this] { return engine_->positionMs(); },
          .track = [this] { return engine_->track(); },
          .artPath = [this] { return artPath_; },
          .hasNext =
              [this] {
                if (!playlist_ || playlist_->empty()) return false;
                if (!playlist_->queueEmpty()) return true;
                return shuffle_ || !playlist_->isLast(playlist_->currentIndex());
              },
          .hasPrevious =
              [this] { return playlist_ && playlist_->hasCurrent() && playlist_->currentIndex() > 0; },
          .play =
              [this] {
                engine_->play();
                refresh();
              },
          .pause =
              [this] {
                engine_->pause();
                refresh();
              },
          .playPause = [this] { togglePlayPause(); },
          .stop =
              [this] {
                engine_->stop();
                refresh();
              },
          .next = [this] { playNext(); },
          .previous = [this] { playPrevious(); },
          .seekTo =
              [this](std::int64_t ms) {
                engine_->seek(ms);
                refresh();
              },
          .openPath = [this](const std::string& path) { return openPath(QString::fromStdString(path)); },
          .shuffle = [this] { return shuffle_; },
          .setShuffle = [this](bool on) {
            if (on != shuffle_) toggleShuffle();
          },
          .repeat = [this] { return repeatActive(); },
          .setRepeat = [this](bool on) { setRepeatMode(on ? 1 : 0); },
          .repeatMode = [this] { return repeatMode_; },
          .setRepeatMode = [this](int mode) { setRepeatMode(mode); },
          .volume = [this] { return static_cast<double>(engine_->volume()); },
          .setVolume =
              [this](double value) {
                engine_->setVolume(static_cast<float>(value));
                if (volumeSlider_ != nullptr) {
                  volumeSlider_->setValue(static_cast<int>(value * 100.0 + 0.5));
                }
                refresh();
              },
          .raise = [this] {
            raise();
            activateWindow();
          },
          // "Sair" do MPRIS é sair de verdade, não esconder para a bandeja.
          .quit = [this] {
            quitting_ = true;
            close();
          },
      });
  if (!mpris_->start()) {
    qWarning("amplia: MPRIS2 não entrou no barramento; o controle de mídia não vai aparecer");
  }

  // Scrobbling: fila própria em disco, envio assíncrono. As chaves vêm da INI
  // (Preferências ▸ Scrobbling); sem chave, nada é enviado.
  scrobbler_ = std::make_unique<wamp::Scrobbler>(this);
  connect(scrobbler_.get(), &wamp::Scrobbler::statusMessage, this, [this](const QString& text) {
    statusBar()->showMessage(text, 8000);
  });

  engine_->setCallbacks({
      [this] { QMetaObject::invokeMethod(this, [this] { onTrackEnded(); }, Qt::QueuedConnection); },
      [this](const std::string& message) {
        const QString text = QString::fromStdString(message);
        QMetaObject::invokeMethod(
            this, [this, text] { showError(text); }, Qt::QueuedConnection);
      },
      [this](const wamp::TrackInfo& info) {
        const QString path = QString::fromStdString(info.path);
        QMetaObject::invokeMethod(
            this, [this, path] { applyGaplessTrackChange(path); }, Qt::QueuedConnection);
      },
  });

  // Fila de leitura de metadados: uma thread própria, uma faixa por vez. O resultado
  // volta por invokeMethod porque o callback chega da thread de leitura.
  scanner_ = std::make_unique<wamp::MetadataScanner>();
  // Cache de tags em SQLite: abrir a playlist não precisa reler arquivo por arquivo.
  const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataDir);
  cache_ = std::make_unique<wamp::TagCache>(
      (dataDir + QStringLiteral("/library.db")).toStdString());
  std::string cacheError;
  if (!cache_->open(&cacheError)) {
    qWarning("amplia: cache de tags indisponível: %s", cacheError.c_str());
    cache_.reset();
  }

  scanner_->setCallback([this](const std::string& path, const wamp::TrackInfo& info, bool ok) {
    QMetaObject::invokeMethod(
        this, [this, path, info, ok] { onMetadata(path, info, ok); }, Qt::QueuedConnection);
  });

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &MainWindow::refresh);
  timer_->start(200);

  // O "agente": ícone na bandeja com os comandos do agente do Winamp.
  buildTray();

  // Altura compacta medida com a playlist fechada: é ela que a janela retoma ao fechar,
  // e também o piso da janela — o painel de sete segmentos não pode ser esmagado. O
  // mínimo do layout só existe depois de ativá-lo, então o piso vem de kCompactFloor.
  layout()->activate();
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_LAYOUT")) {
    qInfo("hint janela=%dx%d display=%dx%d minimo=%dx%d",
          sizeHint().width(), sizeHint().height(), display_->sizeHint().width(),
          display_->sizeHint().height(), minimumSizeHint().width(), minimumSizeHint().height());
    for (int i = 0; i < layout()->count(); ++i) {
      QLayoutItem* item = layout()->itemAt(i);
      qInfo("  item %d: hint=%dx%d", i, item->sizeHint().width(), item->sizeHint().height());
    }
  }
  compactHeight_ = std::max(kCompactFloor, sizeHint().height());
  setMinimumSize(400, compactHeight_);
  loadSettings();
  schedulePendingPlaylistOpen();
  loadEqualizer();
  // Fila de indexação que sobrou da sessão anterior (fechou no meio, kill, queda).
  resumePendingScan();

  // Depuração: simula o usuário esticando a janela (o KWin não permite redimensionar
  // por script no Wayland sem interação).
  // Depuração: troca o modo da playlist em tempo de execução (a sessão de teste não
  // consegue clicar no combo de Preferências).
  if (qEnvironmentVariableIsSet("AMPLIA_TEST_XWAYLAND_OFF")) {
    QTimer::singleShot(1500, this, [this] {
      QSettings settings(settingsFile(), QSettings::IniFormat);
      settings.setValue(QStringLiteral("window/xwaylandPosition"), false);
      settings.sync();
      restartWithPlatformChange();
    });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_TEST_TOGGLE_TOP")) {
    QTimer::singleShot(1500, this, [this] {
      if (alwaysOnTopAction_ != nullptr) alwaysOnTopAction_->trigger();
    });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_TEST_MODE")) {
    const QString mode = qEnvironmentVariable("AMPLIA_TEST_MODE");
    QTimer::singleShot(1500, this, [this, mode] {
      setPlaylistMode(mode);
      QTimer::singleShot(600, this, [this] {
        qInfo("teste modo=%s externa_visivel=%d painel_na_externa=%d pai_do_painel=%s painel_visivel=%d",
              qPrintable(playlistMode_),
              playlistWindow_ != nullptr && playlistWindow_->isVisible() ? 1 : 0,
              playlistPanel_ != nullptr && playlistPanel_->parentWidget() == playlistWindow_ ? 1 : 0,
              playlistPanel_ ? playlistPanel_->parentWidget()->metaObject()->className() : "-",
              playlistPanel_ && playlistPanel_->isVisible() ? 1 : 0);
      });
    });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_TEST_RESIZE")) {
    QTimer::singleShot(1200, this, [this] {
      resize(width(), compactHeight_ + qEnvironmentVariableIntValue("AMPLIA_TEST_RESIZE"));
    });
  }

  // Depuração: fecha pelo caminho normal (passa pelo closeEvent), para testar a bandeja.
  if (qEnvironmentVariableIsSet("AMPLIA_TEST_HIDE")) {
    QTimer::singleShot(1500, this, [this] { close(); });
  }

  // Escape de depuração: salva a própria janela em PNG (sem capturar a tela do usuário).
  if (qEnvironmentVariableIsSet("AMPLIA_GRAB")) {
    const QString target = qEnvironmentVariable("AMPLIA_GRAB");
    // Atraso ajustável: útil para pegar a interface em estados transitórios (fila de
    // metadados, abertura da playlist). Padrão 2,5 s.
    const int grabDelay = qEnvironmentVariableIsSet("AMPLIA_GRAB_DELAY_MS")
                              ? qEnvironmentVariableIntValue("AMPLIA_GRAB_DELAY_MS")
                              : 2500;
    QTimer::singleShot(grabDelay, this, [this, target] {
      grab().save(target);
      qInfo("amplia: janela %dx%d compacta=%d display=%dx%d", width(), height(), compactHeight_,
            display_ ? display_->width() : -1, display_ ? display_->height() : -1);
      qInfo("amplia: janela salva em %s", qPrintable(target));
    });
  }

  // Escape de depuração: salva a janela auxiliar da playlist (sem capturar a tela).
  // Depuração: abrir a Biblioteca e fotografá-la, para testar sem teclado (a sessão de
  // teste não injeta atalhos).
  if (qEnvironmentVariableIsSet("AMPLIA_OPEN_LIBRARY")) {
    QTimer::singleShot(0, this, [this] { openLibrary(); });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_LIBRARY_RESCAN")) {
    QTimer::singleShot(200, this, [this] { rescanLibrary(); });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_GRAB_LIBRARY")) {
    const QString target = qEnvironmentVariable("AMPLIA_GRAB_LIBRARY");
    const int libraryDelay = qEnvironmentVariableIsSet("AMPLIA_GRAB_DELAY_MS")
                                 ? qEnvironmentVariableIntValue("AMPLIA_GRAB_DELAY_MS")
                                 : 4000;
    QTimer::singleShot(libraryDelay, this, [this, target] {
      if (!library_) return;
      library_->grab().save(target);
      qInfo("amplia: biblioteca %dx%d visivel=%d", library_->width(), library_->height(),
            library_->isVisible() ? 1 : 0);
    });
  }

  if (qEnvironmentVariableIsSet("AMPLIA_GRAB_PLAYLIST")) {
    const QString target = qEnvironmentVariable("AMPLIA_GRAB_PLAYLIST");
    QTimer::singleShot(3000, this, [this, target] {
      if (playlistIntegrated()) {
        if (playlistPanel_ == nullptr) return;
        playlistPanel_->grab().save(target);
        qInfo("amplia: playlist integrada %dx%d visivel=%d", playlistPanel_->width(),
              playlistPanel_->height(), playlistPanel_->isVisible() ? 1 : 0);
        return;
      }
      if (playlistWindow_ == nullptr) return;
      playlistWindow_->grab().save(target);
      qInfo("amplia: playlist %dx%d visivel=%d", playlistWindow_->width(),
            playlistWindow_->height(), playlistWindow_->isVisible() ? 1 : 0);
    });
  }

  if (qEnvironmentVariableIsSet("AMPLIA_OPEN_EQUALIZER")) {
    QTimer::singleShot(400, this, &MainWindow::openEqualizer);
  }

  if (qEnvironmentVariableIsSet("AMPLIA_OPEN_FILEINFO")) {
    QTimer::singleShot(400, this, &MainWindow::openFileInfo);
  }

  // Escape de depuração: abre o Jump to File sozinho (usado para conferir o desenho).
  if (qEnvironmentVariableIsSet("AMPLIA_CALIBRATE")) {
    QTimer::singleShot(1500, this, [this] { calibrateVisual(nullptr); });
  }
  if (qEnvironmentVariableIsSet("AMPLIA_OPEN_JUMP")) {
    QTimer::singleShot(400, this, &MainWindow::openJumpToFile);
  }
  // Camada A da F5: o mapa de teclas do original, sem inventar combinações.
  const auto shortcut = [this](const QKeySequence& keys, auto handler) {
    auto* action = new QAction(this);
    action->setShortcut(keys);
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this, handler);
    addAction(action);
  };
  shortcut(QKeySequence(Qt::Key_Right), [this] {
    engine_->seek(std::min(engine_->durationMs(), engine_->positionMs() + 5000));
  });
  shortcut(QKeySequence(Qt::Key_Left), [this] {
    engine_->seek(std::max<std::int64_t>(0, engine_->positionMs() - 5000));
  });
  shortcut(QKeySequence(Qt::Key_Up), [this] {
    volumeSlider_->setValue(std::min(100, volumeSlider_->value() + 2));
  });
  shortcut(QKeySequence(Qt::Key_Down), [this] {
    volumeSlider_->setValue(std::max(0, volumeSlider_->value() - 2));
  });
  shortcut(QKeySequence(QStringLiteral("Ctrl+T")), [this] { toggleTimeMode(); });
  shortcut(QKeySequence(Qt::Key_M), [this] { toggleMute(); });
  // Pular dez faixas: Num. 1 / Num. 3 como no original; Ctrl+setas para teclados sem teclado numérico.
  shortcut(QKeySequence(Qt::Key_1 | Qt::KeypadModifier), [this] { jumpTracks(-10); });
  shortcut(QKeySequence(Qt::Key_3 | Qt::KeypadModifier), [this] { jumpTracks(10); });
  shortcut(QKeySequence(QStringLiteral("Ctrl+Left")), [this] { jumpTracks(-10); });
  shortcut(QKeySequence(QStringLiteral("Ctrl+Right")), [this] { jumpTracks(10); });
  // Tamanho do texto da playlist. Além de Ctrl+Shift+= / -, aceitamos o "=" e o "-" sem
  // Shift e o teclado numérico (KeypadPlus/KeypadMinus), que o Qt entrega como teclas
  // próprias — era o caso do "+" do numérico que não respondia.
  const auto biggerFont = [this] {
    setPlaylistFontSize(playlistFontPx_ + 1);
    saveSettings();
  };
  const auto smallerFont = [this] {
    setPlaylistFontSize(playlistFontPx_ - 1);
    saveSettings();
  };
  shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+=")), biggerFont);
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), biggerFont);
  shortcut(QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Plus), biggerFont);
  shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+-")), smallerFont);
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), smallerFont);
  shortcut(QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Minus), smallerFont);
  shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+0")), [this] {
    setPlaylistFontSize(11);
    saveSettings();
  });

  // Camada C: janelas. Alt+W/E/G focam (mostrando, quando preciso); Ctrl+Tab alterna.
  shortcut(QKeySequence(QStringLiteral("Alt+W")), [this] {
    show();
    raise();
    activateWindow();
  });
  shortcut(QKeySequence(QStringLiteral("Alt+E")), [this] { focusPlaylist(); });
  shortcut(QKeySequence(QStringLiteral("Alt+G")), [this] { focusEqualizer(); });
  shortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), [this] { cycleWindows(); });

  // Contador da fila de metadados e versão ficam fixos na barra: a mensagem passageira
  // (showMessage) some sozinha e não serve para acompanhar um lote grande.
  queueLabel_ = new QLabel(this);
  queueLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  queueLabel_->setObjectName(QStringLiteral("queueLabel"));
  queueLabel_->hide();
  statusBar()->setSizeGripEnabled(false);
  queueLabel_->setContentsMargins(4, 0, 8, 0);
  statusBar()->addWidget(queueLabel_, 1);

  versionLabel_ = new QLabel(this);
  versionLabel_->setObjectName(QStringLiteral("versionLabel"));
  versionLabel_->setToolTip(tr("Versão desta compilação do Amplia"));
  // Só os dois dados: o nome do app já está na janela, na bandeja e no menu.
  versionLabel_->setText(QStringLiteral("%1 (%2)").arg(QStringLiteral(AMPLIA_VERSION),
                                                       QStringLiteral(AMPLIA_GIT)));
  versionLabel_->setContentsMargins(8, 0, 6, 0);
  statusBar()->addPermanentWidget(versionLabel_);

  statusBar()->showMessage(tr("Arraste arquivos para a playlist ou use Arquivo ▸ Abrir"));
  refresh();
}

MainWindow::~MainWindow() {
  // Fecha a fila antes dos widgets: a thread de leitura é juntada aqui.
  if (scanner_) {
    scanner_->setCallback(nullptr);
    scanner_->clearPending();
  }
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(6);
  mainLayout_ = layout;

  // O display ocupa o lugar do título e da barra de posição: relógio, informações do
  // arquivo, faixa e a própria barra (que agora é o controle de posição).
  display_ = new wamp::ui::DisplayPanel(central);
  // Altura fixa: o painel tem geometria desenhada à mão e não pode ser comprimido pela
  // lista da playlist quando ela abre.
  display_->setFixedHeight(display_->sizeHint().height());
  visualLast_ = std::chrono::steady_clock::now();
  visualTimer_ = new QTimer(this);
  visualTimer_->setInterval(1000 / std::max(1, visualFps_));
  connect(visualTimer_, &QTimer::timeout, this, &MainWindow::updateVisual);
  visualTimer_->start();
  layout->addWidget(display_);

  connect(display_, &wamp::ui::DisplayPanel::seekRequested, this, [this](qint64 ms) {
    engine_->seek(ms);
    refresh();
  });
  connect(display_, &wamp::ui::DisplayPanel::equalizerClicked, this, [this] {
    engine_->setEqualizerEnabled(!engine_->equalizerEnabled());
    saveEqualizer();
    refresh();
  });
  connect(display_, &wamp::ui::DisplayPanel::repeatClicked, this, &MainWindow::toggleRepeat);
  connect(display_, &wamp::ui::DisplayPanel::shuffleClicked, this, &MainWindow::toggleShuffle);
  connect(display_, &wamp::ui::DisplayPanel::timeClicked, this, &MainWindow::toggleTimeMode);


  // Sem stretch no display: com o display elástico, esticar a janela abria um vão em
  // branco em cima e embaixo dele; o espaço extra tem que ir para a playlist.
  display_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto roundButton = [central](wamp::ui::RoundButton::GlyphPainter glyph, int size) {
    auto* button = new wamp::ui::RoundButton(central);
    button->setGlyph(std::move(glyph));
    button->setFixedSize(size, size);
    return button;
  };

  // A coluna vive dentro de um QWidget: um layout solto com stretch empurraria a linha
  // inteira e abriria espaço em volta do display. Ela desce pela faixa do transporte (como
  // no original), então quem manda na altura é o bloco display+transporte, não o display.
  auto* sideWidget = new QWidget(central);
  sideWidget->setFixedWidth(28);
  auto* sideButtons = new QVBoxLayout(sideWidget);
  sideButtons->setSpacing(4);
  sideButtons->setContentsMargins(0, 0, 0, 0);

  shuffleButton_ = roundButton(wamp::ui::paintShuffle, 28);
  shuffleButton_->setCheckable(true);
  shuffleButton_->setToolTip(tr("Sorteio (S) — nível em Preferências"));
  connect(shuffleButton_, &QPushButton::clicked, this, &MainWindow::toggleShuffle);
  sideButtons->addWidget(shuffleButton_);

  repeatButton_ = roundButton(
      [](QPainter& painter, const QRectF& box, const QColor& color) {
        wamp::ui::paintRepeat(painter, box, color, false);
      },
      28);
  repeatButton_->setCheckable(true);
  repeatButton_->setToolTip(tr("Repetição desligada (R)"));
  connect(repeatButton_, &QPushButton::clicked, this, &MainWindow::toggleRepeat);
  sideButtons->addWidget(repeatButton_);

  // ML fica entre os modos e o PL: em cima os modos de reprodução, embaixo as janelas.
  mlButton_ = roundButton(nullptr, 28);
  mlButton_->setText(QStringLiteral("ML"));
  mlButton_->setFixedSize(28, 28);
  mlButton_->setCursor(Qt::PointingHandCursor);
  mlButton_->setToolTip(tr("Biblioteca (Alt+L)"));
  connect(mlButton_, &QPushButton::clicked, this, [this] {
    if (library_ != nullptr && library_->isVisible()) {
      library_->hide();
      return;
    }
    openLibrary();
  });
  sideButtons->addWidget(mlButton_);

  playlistButton_ = roundButton(nullptr, 28);
  playlistButton_->setText(QStringLiteral("PL"));
  playlistButton_->setCheckable(true);
  playlistButton_->setFixedSize(28, 28);
  playlistButton_->setCursor(Qt::PointingHandCursor);
  playlistButton_->setToolTip(tr("Mostrar / ocultar a playlist"));
  connect(playlistButton_, &QPushButton::toggled, this, &MainWindow::setPlaylistVisible);
  sideButtons->addWidget(playlistButton_);

  // Transporte circular, mute e volume — os botões redondos deixam mais largura para a
  // barra de volume do que os retangulares de antes.
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  row->setContentsMargins(0, 0, 0, 0);

  prevButton_ = roundButton(wamp::ui::paintPrevious, 30);
  prevButton_->setToolTip(tr("Faixa anterior (Z)"));
  connect(prevButton_, &QPushButton::clicked, this, &MainWindow::playPrevious);
  row->addWidget(prevButton_);

  playButton_ = roundButton(wamp::ui::paintPlay, 30);
  playButton_->setCheckable(true);
  playButton_->setToolTip(tr("Reproduzir (espaço, X) — tocando, recomeça a faixa"));
  connect(playButton_, &QPushButton::clicked, this, [this] {
    // Com a faixa andando, o play funciona como "do início" — atalho do original.
    if (restartOnPlay_ && engine_->state() == wamp::PlaybackState::Playing) {
      engine_->seek(0);
    } else {
      engine_->play();
    }
    refresh();
  });
  row->addWidget(playButton_);

  pauseButton_ = roundButton(wamp::ui::paintPause, 30);
  pauseButton_->setCheckable(true);
  pauseButton_->setToolTip(tr("Pausar (espaço, C)"));
  connect(pauseButton_, &QPushButton::clicked, this, [this] {
    engine_->pause();
    refresh();
  });
  row->addWidget(pauseButton_);

  stopButton_ = roundButton(wamp::ui::paintStop, 30);
  stopButton_->setToolTip(tr("Parar (V)"));
  connect(stopButton_, &QPushButton::clicked, this, [this] {
    finishScrobble();
    scrobbleStartUnix_ = 0;  // próxima reprodução começa uma sessão nova
    engine_->stop();
    refresh();
  });
  row->addWidget(stopButton_);

  nextButton_ = roundButton(wamp::ui::paintNext, 30);
  nextButton_->setToolTip(tr("Próxima faixa (B)"));
  connect(nextButton_, &QPushButton::clicked, this, &MainWindow::playNext);
  row->addWidget(nextButton_);

  muteButton_ = roundButton(
      [](QPainter& painter, const QRectF& box, const QColor& color) {
        wamp::ui::paintVolume(painter, box, color, false);
      },
      30);
  muteButton_->setCheckable(true);
  muteButton_->setToolTip(tr("Silenciar / restaurar volume"));
  connect(muteButton_, &QPushButton::clicked, this, [this] { toggleMute(); });
  row->addWidget(muteButton_);

  volumeSlider_ = new wamp::ui::ClickSlider(Qt::Horizontal, central);
  volumeSlider_->setRange(0, 100);
  volumeSlider_->setValue(80);
  volumeSlider_->setMinimumWidth(120);
  volumeSlider_->setToolTip(QStringLiteral("Volume"));
  connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
    if (value > 0) lastVolume_ = value;
    engine_->setVolume(static_cast<float>(value) / 100.0f);
    if (muteButton_ != nullptr) {
      const bool muted = value == 0;
      muteButton_->setGlyph([muted](QPainter& painter, const QRectF& box, const QColor& color) {
        wamp::ui::paintVolume(painter, box, color, muted);
      });
      muteButton_->setChecked(value == 0);
    }
  });
  row->addWidget(volumeSlider_, 1);

  // A coluna de estado fica ao lado do display e centralizada nele (é o que agrada o
  // olho); o transporte ocupa a largura inteira logo abaixo. Quatro botões de 28 px com
  // 4 de respiro dão 124 px, que cabem nos 154 do display.
  auto* displayRow = new QHBoxLayout();
  displayRow->setSpacing(6);
  displayRow->addWidget(display_, 1);
  displayRow->addWidget(sideWidget, 0, Qt::AlignVCenter);
  layout->addLayout(displayRow, 0);
  layout->addLayout(row, 0);

  // Editor de playlist em janela auxiliar: Qt::Tool com pai não entra na barra de tarefas
  // nem no Alt+Tab, e o compositor a mantém junto da principal. Assim o botão PL não
  // redimensiona a janela do player (era o que fazia a janela inteira se mexer).
  // Qt::Dialog (e não Qt::Tool) de propósito: no Wayland é o tipo "diálogo" que faz o Qt
  // enviar o xdg_toplevel.set_parent — o KWin então trata a janela como dependente e a
  // tira da barra de tarefas. Com Qt::Tool o parentesco nunca chegava ao compositor.
  playlistWindow_ = new QWidget(this, Qt::Tool);
  playlistWindow_->setWindowTitle(QStringLiteral("Playlist"));
  playlistWindow_->resize(440, 400);
  // No Wayland o papel "tool" sozinho não basta para o KWin tirar a janela da barra de
  // tarefas: o que marca a subordinação é o xdg_toplevel.set_parent, que o Qt só envia se
  // a janela-mãe já tiver handle quando esta é criada. Por isso o handle da playlist é
  // criado tarde, em setPlaylistVisible() — nunca aqui, durante o construtor.
  auto* windowLayout = new QVBoxLayout(playlistWindow_);
  windowLayout->setContentsMargins(8, 8, 8, 8);
  windowLayout->setSpacing(0);

  playlistPanel_ = new QWidget();
  // Sem altura fixa: quem absorve o espaço quando a janela é esticada é a lista. O
  // tamanho é controlado pelo resize (compacto + kPlaylistHeight) e pela altura
  // compacta limitada ao bloco natural. Altura fixa aqui virava vão em branco.
  playlistPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

  {
    // Teclas do Playlist Editor: Espaço mostra a faixa que está tocando; Ctrl+A
    // seleciona tudo (no player, Ctrl+A é "sempre no topo"). Ficam presas ao painel, não à
    // janela: assim seguem a lista quando ela muda de dono (integrada ↔ janela própria) e
    // só valem com o foco dentro da lista.
    auto* showPlaying = new QAction(playlistPanel_);
    // Sem atalho no QAction: as teclas são tratadas no eventFilter da lista
    // (ShortcutOverride), senão brigam com os atalhos da janela principal — Ctrl+A é
    // "sempre no topo" no player e "selecionar tudo" na lista, como no original.
    showPlaying->setText(tr("Ir para a faixa tocando"));
    connect(showPlaying, &QAction::triggered, this, [this] {
      if (list_ == nullptr || !playlist_->hasCurrent()) return;
      const int row = static_cast<int>(playlist_->currentIndex());
      if (QListWidgetItem* item = list_->item(row)) list_->scrollToItem(item);
    });
    playlistPanel_->addAction(showPlaying);

    auto* selectAll = new QAction(playlistPanel_);
    selectAll->setText(tr("Selecionar tudo"));
    connect(selectAll, &QAction::triggered, this, [this] {
      if (list_ == nullptr) return;
      list_->selectAll();
    });
    playlistPanel_->addAction(selectAll);
  }
  auto* panelLayout = new QVBoxLayout(playlistPanel_);
  panelLayout->setContentsMargins(0, 0, 0, 0);
  panelLayout->setSpacing(4);

  // Arrastar de qualquer ponto: quando ligado, um clique no painel do display ou no
  // fundo da janela inicia o movimento de sistema (no Wayland, startSystemMove é o
  // único caminho — o cliente não escolhe posição sozinho).
  display_->installEventFilter(this);
  central->installEventFilter(this);

  list_ = new QListWidget(playlistPanel_);
  setPlaylistFontSize(playlistFontPx_);
  delegate_ = new PlaylistDelegate(list_);
  list_->setItemDelegate(delegate_);
  list_->setUniformItemSizes(true);
  list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list_->setAcceptDrops(true);  // o drop é tratado pelo eventFilter da viewport
  list_->viewport()->setAcceptDrops(true);
  // Alt+clique esquerdo enfileira (como o JTFE do Winamp); clique direito abre o menu.
  list_->viewport()->installEventFilter(this);
  // Enter toca a selecionada, Delete remove — atalhos da lista do Winamp.
  list_->installEventFilter(this);
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    playIndex(static_cast<std::size_t>(list_->row(item)));
  });
  panelLayout->addWidget(list_, 1);

  // Ações com atalho vivem num lugar só: dois QAction com o mesmo atalho fariam o Qt
  // reclamar de atalho ambíguo. Topo e rodapé apontam para as mesmas instâncias.
  actNewPlaylist_ = new QAction(tr("Nova playlist"), this);
  actNewPlaylist_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
  connect(actNewPlaylist_, &QAction::triggered, this, &MainWindow::newPlaylist);

  actOpenPlaylist_ = new QAction(tr("Abrir playlist\u2026"), this);
  actOpenPlaylist_->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(actOpenPlaylist_, &QAction::triggered, this, &MainWindow::loadPlaylistDialog);

  actAddFiles_ = new QAction(tr("Adicionar arquivo(s)\u2026"), this);
  actAddFiles_->setShortcut(QKeySequence(QStringLiteral("L")));
  connect(actAddFiles_, &QAction::triggered, this, &MainWindow::addFilesDialog);

  actAddFolder_ = new QAction(tr("Adicionar pasta\u2026"), this);
  actAddFolder_->setShortcut(QKeySequence(QStringLiteral("Shift+L")));
  connect(actAddFolder_, &QAction::triggered, this, &MainWindow::addFolderDialog);

  actSavePlaylist_ = new QAction(tr("Salvar playlist\u2026"), this);
  actSavePlaylist_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
  connect(actSavePlaylist_, &QAction::triggered, this, &MainWindow::savePlaylistDialog);

  auto* panelButtons = new QHBoxLayout();
  panelButtons->setSpacing(4);

  // Três menus no rodapé, como o Playlist Editor do Winamp: Arquivo, Playlist e Ordenar.
  // As ações são as mesmas do menu do topo (mesmos atalhos), criadas uma vez só.
  auto* fileButton = new QPushButton(tr("Arquivo \u25be"), playlistPanel_);
  auto* footerFileMenu = new QMenu(fileButton);
  footerFileMenu->addAction(actNewPlaylist_);
  footerFileMenu->addAction(actOpenPlaylist_);
  footerFileMenu->addSeparator();
  footerFileMenu->addAction(actAddFiles_);
  footerFileMenu->addAction(actAddFolder_);
  footerFileMenu->addSeparator();
  footerFileMenu->addAction(actSavePlaylist_);
  fileButton->setMenu(footerFileMenu);
  panelButtons->addWidget(fileButton);

  auto* listButton = new QPushButton(tr("Playlist \u25be"), playlistPanel_);
  auto* listMenu = new QMenu(listButton);
  listMenu->addAction(tr("Selecionar tudo"), QKeySequence(QStringLiteral("Ctrl+Alt+A")),
                      this, &MainWindow::selectAllItems);
  listMenu->addAction(tr("Selecionar nada"), QKeySequence(QStringLiteral("Ctrl+Alt+D")),
                      this, &MainWindow::selectNone);
  listMenu->addAction(tr("Inverter seleção"), QKeySequence(QStringLiteral("Ctrl+I")),
                      this, &MainWindow::invertSelection);
  listMenu->addSeparator();
  listMenu->addAction(tr("Remover selecionada"), QKeySequence(QKeySequence::Delete),
                      this, &MainWindow::removeSelected);
  listMenu->addAction(tr("Manter só a seleção"), QKeySequence(QStringLiteral("Ctrl+Delete")),
                      this, &MainWindow::cropSelected);
  listMenu->addAction(tr("Limpar playlist"), QKeySequence(QStringLiteral("Ctrl+Shift+Delete")),
                      this, &MainWindow::clearPlaylist);
  listMenu->addSeparator();
  listMenu->addAction(tr("Ir para a faixa…"), QKeySequence(Qt::Key_Comma), this,
                      &MainWindow::openJumpToFile);
  listMenu->addAction(tr("Informações do arquivo"), QKeySequence(QStringLiteral("Alt+3")),
                      this, &MainWindow::openFileInfo);
  listMenu->addAction(tr("Remover arquivos inexistentes"),
                      QKeySequence(QStringLiteral("Alt+Delete")), this, &MainWindow::removeMissing);
  listMenu->addSeparator();
  showPathAction_ = listMenu->addAction(tr("Mostrar caminho completo"));
  showPathAction_->setCheckable(true);
  showPathAction_->setChecked(showPath_);
  connect(showPathAction_, &QAction::triggered, this, [this](bool on) {
    showPath_ = on;
    rebuildPlaylist();
    saveSettings();
  });
  listButton->setMenu(listMenu);
  panelButtons->addWidget(listButton);

  auto* sortButton = new QPushButton(tr("Ordenar \u25be"), playlistPanel_);
  auto* sortMenu = new QMenu(sortButton);
  sortMenu->addAction(tr("Por título"), QKeySequence(QStringLiteral("Ctrl+Shift+1")), this,
                      [this] { sortPlaylist(0); });
  sortMenu->addAction(tr("Por nome de arquivo"),
                      QKeySequence(QStringLiteral("Ctrl+Shift+2")), this, [this] { sortPlaylist(1); });
  sortMenu->addAction(tr("Por caminho"), QKeySequence(QStringLiteral("Ctrl+Shift+3")), this,
                      [this] { sortPlaylist(2); });
  sortMenu->addSeparator();
  sortMenu->addAction(tr("Inverter lista"), QKeySequence(QStringLiteral("Ctrl+R")), this,
                      &MainWindow::reverseList);
  sortMenu->addAction(tr("Embaralhar lista"),
                      QKeySequence(QStringLiteral("Ctrl+Shift+R")), this, &MainWindow::shuffleList);
  sortButton->setMenu(sortMenu);
  panelButtons->addWidget(sortButton);

  // Ações dentro de um QMenu preso a um botão não registram o atalho na janela: o menu
  // só existe enquanto está aberto. Registrar no widget faz o atalho valer sempre.
  for (QMenu* menu : {footerFileMenu, listMenu, sortMenu}) {
    for (QAction* action : menu->actions()) {
      // As ações compartilhadas com o menu do topo já vivem na janela; registrar de novo
      // daria atalho ambíguo. Só as criadas aqui dentro precisam do registro.
      if (action->parent() != this && !action->shortcut().isEmpty()) addAction(action);
    }
  }

  auto* queueButton = new QPushButton(tr("Enfileirar"), playlistPanel_);
  queueButton->setToolTip(tr("Toca a seleção antes de seguir a ordem (ou Alt+clique)"));
  connect(queueButton, &QPushButton::clicked, this, &MainWindow::queueSelected);
  panelButtons->addWidget(queueButton);

  panelButtons->addStretch(1);
  panelLayout->addLayout(panelButtons);

  setCentralWidget(central);

  engine_->setVolume(0.8f);

  // A playlist mora na mesma janela. No Wayland o cliente não posiciona janelas, então
  // "acoplar" só existe como janela única: a lista entra embaixo, a janela cresce para
  // baixo (o canto superior esquerdo é do compositor e não se mexe) e volta ao tamanho
  // compacto ao fechar.
  playlistWindow_->installEventFilter(this);
  playlistWindow_->hide();
  applyPlaylistMode();
}

void MainWindow::applyThemeFromSettings() {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  const QString id =
      settings.value(QStringLiteral("appearance/theme"), QStringLiteral("grafite")).toString();
  savedThemes_ = wamp::ui::loadSavedThemes();
  const wamp::ui::Theme* saved = nullptr;
  for (const wamp::ui::Theme& theme : savedThemes_) {
    if (theme.id == id) saved = &theme;
  }
  if (saved != nullptr) {
    // Tema vindo de CSV: a paleta já está derivada, aplica direto.
    wamp::ui::setActiveTheme(*saved);
  } else if (id == QLatin1String("personalizado")) {
    wamp::ui::setActiveTheme(wamp::ui::makeCustom(customBackground(), customAccent(),
                                                  customSecondary(), customButtons(),
                                                  customContrast()));
  } else {
    wamp::ui::setActiveTheme(id);
  }
  qApp->setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));
  syncThemeChecks();
}

QColor MainWindow::customBackground() const {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  return QColor(settings.value(QStringLiteral("appearance/customBg"),
                               QStringLiteral("#232323")).toString());
}

QColor MainWindow::customAccent() const {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  return QColor(settings.value(QStringLiteral("appearance/customAccent"),
                               QStringLiteral("#e0b050")).toString());
}

QColor MainWindow::customSecondary() const {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  return QColor(settings.value(QStringLiteral("appearance/customSecondary"),
                               QStringLiteral("#d8d8d8")).toString());
}

QColor MainWindow::customButtons() const {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  return QColor(settings.value(QStringLiteral("appearance/customButtons"),
                               QStringLiteral("#3a3a3a")).toString());
}

int MainWindow::customContrast() const {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  return settings.value(QStringLiteral("appearance/customContrast"), 100).toInt();
}

void MainWindow::setTheme(const QString& id) {
  if (id == QLatin1String("personalizado")) {
    wamp::ui::setActiveTheme(wamp::ui::makeCustom(customBackground(), customAccent(),
                                                  customSecondary(), customButtons(),
                                                  customContrast()));
  } else {
    wamp::ui::setActiveTheme(id);
  }
  qApp->setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));
  QSettings settings(settingsFile(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("appearance/theme"), id);
  settings.sync();
  // Widgets que se pintam à mão (display, lista, botões redondos, curva do EQ) não
  // dependem do stylesheet: precisam de um update para ler a paleta nova.
  for (QWidget* widget : QApplication::allWidgets()) widget->update();
  syncThemeChecks();
}

void MainWindow::saveCurrentThemeAs() {
  // O editor guarda a paleta em "Personalizado"; para compartilhar (ou exportar em CSV) ela
  // precisa de nome e de arquivo próprio.
  const wamp::ui::Theme current = wamp::ui::activeTheme();
  const QColor background = current.customBackground.isValid() ? current.customBackground
                                                                : current.windowBg;
  const QColor accent =
      current.customAccent.isValid() ? current.customAccent : current.displayLit;
  const QColor secondary =
      current.customSecondary.isValid() ? current.customSecondary : current.windowText;
  const QColor buttons = current.customButtons.isValid() ? current.customButtons : current.controlBg;
  const int contrast = current.customContrast > 0 ? current.customContrast : 100;

  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Salvar tema"),
                                             tr("Nome do tema:"), QLineEdit::Normal,
                                             current.name, &ok)
                           .trimmed();
  if (!ok || name.isEmpty()) return;

  wamp::ui::Theme saved = wamp::ui::makeCustom(background, accent, secondary, buttons, contrast);
  saved.name = name;
  saved.id = wamp::ui::themeSlug(name);
  if (wamp::ui::loadSavedThemes().size() >= wamp::ui::kMaxSavedThemes) {
    QMessageBox::information(
        this, tr("Salvar tema"),
        tr("Limite de %1 temas personalizados atingido. Apague algum antes.")
            .arg(wamp::ui::kMaxSavedThemes));
    return;
  }
  QString error;
  if (!wamp::ui::saveThemeFile(saved, &error)) {
    QMessageBox::warning(this, tr("Salvar tema"), error);
    return;
  }
  savedThemes_ = wamp::ui::loadSavedThemes();
  refreshThemeDialog();
  statusBar()->showMessage(tr("Tema \"%1\" salvo").arg(name), 5000);
}

void MainWindow::importThemes() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Importar temas"), QDir::homePath(), tr("CSV (*.csv)"));
  if (path.isEmpty()) return;

  QStringList warnings;
  QString error;
  const int count = wamp::ui::importThemesCsv(path, &warnings, &error);
  if (count < 0) {
    QMessageBox::warning(this, tr("Importar temas"), error);
    return;
  }
  savedThemes_ = wamp::ui::loadSavedThemes();
  refreshThemeDialog();
  QString message = tr("%1 tema(s) importado(s).").arg(count);
  if (!warnings.isEmpty()) {
    message += QStringLiteral("\n\n") + warnings.join(QLatin1Char('\n'));
  }
  QMessageBox::information(this, tr("Importar temas"), message);
}

void MainWindow::exportThemes() {
  if (savedThemes_.isEmpty()) {
    QMessageBox::information(this, tr("Exportar temas"),
                             tr("Não há temas personalizados salvos ainda."));
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Exportar temas"), QDir::homePath() + QStringLiteral("/temas-amplia.csv"),
      tr("CSV (*.csv)"));
  if (path.isEmpty()) return;
  QString error;
  if (!wamp::ui::exportThemesCsv(path, &error)) {
    QMessageBox::warning(this, tr("Exportar temas"), error);
    return;
  }
  statusBar()->showMessage(tr("%1 tema(s) exportado(s) em %2")
                               .arg(savedThemes_.size())
                               .arg(path),
                           6000);
}

void MainWindow::syncThemeChecks() {
  if (themeDialog_ == nullptr) return;
  themeDialog_->setThemes(savedThemes_, wamp::ui::activeTheme().id);
}

void MainWindow::refreshThemeDialog() {
  if (themeDialog_ == nullptr) return;
  savedThemes_ = wamp::ui::loadSavedThemes();
  themeDialog_->setThemes(savedThemes_, wamp::ui::activeTheme().id);
}

void MainWindow::applyThemeById(const QString& id) {
  for (const wamp::ui::Theme& theme : wamp::ui::allThemes()) {
    if (theme.id != id) continue;
    setTheme(id);
    if (themeDialog_ != nullptr) {
      themeDialog_->setColors(theme.windowBg, theme.accent, theme.windowText, theme.controlBg,
                              themeDialog_->contrastPercent());
    }
    return;
  }
  for (const wamp::ui::Theme& theme : savedThemes_) {
    if (theme.id != id) continue;
    // Tema salvo em disco: a paleta já está derivada, aplica direto.
    wamp::ui::setActiveTheme(theme);
    qApp->setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("appearance/theme"), id);
    for (QWidget* widget : QApplication::allWidgets()) widget->update();
    if (themeDialog_ != nullptr) {
      themeDialog_->setColors(theme.customBackground, theme.customAccent, theme.customSecondary,
                              theme.customButtons, theme.customContrast);
    }
    return;
  }
  if (id == QLatin1String("personalizado")) {
    applyCustomTheme(customBackground(), customAccent(), customSecondary(), customButtons(),
                     customContrast());
    if (themeDialog_ != nullptr) {
      themeDialog_->setColors(customBackground(), customAccent(), customSecondary(),
                              customButtons(), customContrast());
    }
  }
}

void MainWindow::applyCustomTheme(const QColor& background, const QColor& accent,
                                  const QColor& secondary, const QColor& buttons,
                                  int contrastPercent) {
  wamp::ui::setActiveTheme(wamp::ui::makeCustom(background, accent, secondary, buttons,
                                                contrastPercent));
  qApp->setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));
  for (QWidget* widget : QApplication::allWidgets()) widget->update();
}

void MainWindow::saveCustomTheme(const QColor& background, const QColor& accent,
                                 const QColor& secondary, const QColor& buttons,
                                 int contrastPercent) {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("appearance/customBg"), background.name(QColor::HexRgb));
  settings.setValue(QStringLiteral("appearance/customAccent"), accent.name(QColor::HexRgb));
  settings.setValue(QStringLiteral("appearance/customSecondary"),
                    secondary.name(QColor::HexRgb));
  settings.setValue(QStringLiteral("appearance/customButtons"), buttons.name(QColor::HexRgb));
  settings.setValue(QStringLiteral("appearance/customContrast"), contrastPercent);
  settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("personalizado"));
  settings.sync();
  syncThemeChecks();
}

void MainWindow::openThemeDialog() {
  if (themeDialog_ != nullptr) {
    themeDialog_->raise();
    themeDialog_->activateWindow();
    return;
  }

  themeBeforeEditor_ = wamp::ui::activeTheme();
  savedThemes_ = wamp::ui::loadSavedThemes();
  auto* dialog = new ThemeDialog(customBackground(), customAccent(), customSecondary(),
                                 customButtons(), customContrast(), this);
  // Não modal de propósito: com diálogo modal o KWin aplica o efeito "Dialog Parent" e
  // escurece a janela de trás — impossível julgar cor assim. Sem modalidade, o player
  // continua tocando, as barras animam e o transporte responde.
  dialog->setModal(false);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setMinimumSize(680, 420);
  themeDialog_ = dialog;
  dialog->setThemes(savedThemes_, wamp::ui::activeTheme().id);

  connect(dialog, &ThemeDialog::themeChosen, this, [this](const QString& id) {
    applyThemeById(id);
  });
  connect(dialog, &ThemeDialog::previewChanged, this, [this, dialog] {
    applyCustomTheme(dialog->background(), dialog->accent(), dialog->secondary(), dialog->buttons(),
                     dialog->contrastPercent());
  });
  connect(dialog, &ThemeDialog::revertRequested, this, [this] {
    wamp::ui::setActiveTheme(themeBeforeEditor_);
    qApp->setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));
    for (QWidget* widget : QApplication::allWidgets()) widget->update();
    syncThemeChecks();
  });
  connect(dialog, &ThemeDialog::saveAsRequested, this, [this] {
    saveCurrentThemeAs();
    refreshThemeDialog();
  });
  connect(dialog, &ThemeDialog::importRequested, this, [this] {
    importThemes();
    refreshThemeDialog();
  });
  connect(dialog, &ThemeDialog::exportRequested, this, [this] { exportThemes(); });
  connect(dialog, &ThemeDialog::sampleRequested, this, [this] { saveThemeSample(); });
  connect(dialog, &ThemeDialog::deleteRequested, this, [this, dialog](const QString& id) {
    const auto answer = QMessageBox::question(
        dialog, tr("Excluir tema"),
        tr("Excluir o tema \"%1\"? O arquivo CSV será apagado.")
            .arg(id),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    QString error;
    wamp::ui::removeThemeFile(id, &error);
    refreshThemeDialog();
  });
  connect(dialog, &QDialog::finished, this, [this, dialog] {
    themeDialog_ = nullptr;
    // Sem OK/Cancelar: tudo aplica na hora. Fechar mantém o que está na tela e grava a
    // paleta editada como "Personalizado" se ela foi mexida.
    if (wamp::ui::activeTheme().id == QLatin1String("personalizado")) {
      saveCustomTheme(dialog->background(), dialog->accent(), dialog->secondary(),
                      dialog->buttons(), dialog->contrastPercent());
    }
  });

  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::saveThemeSample() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Modelo de CSV"), QDir::homePath() + QStringLiteral("/temas-amplia.csv"),
      tr("CSV (*.csv)"));
  if (path.isEmpty()) return;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Não foi possível gravar: %1").arg(file.errorString()), 6000);
    return;
  }
  file.write(wamp::ui::themeCsvSample().toUtf8());
  file.close();
  statusBar()->showMessage(tr("Modelo gravado em %1").arg(path), 6000);
}

void MainWindow::buildMenus() {
  auto* fileMenu = menuBar()->addMenu(tr("&Arquivo"));

  auto* openFileAction = new QAction(tr("Abrir arquivo\u2026"), this);
  openFileAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
  connect(openFileAction, &QAction::triggered, this, &MainWindow::openFileDialog);
  fileMenu->addAction(openFileAction);

  fileMenu->addAction(actAddFiles_);
  fileMenu->addAction(actAddFolder_);

  auto* urlAction = new QAction(tr("Abrir URL\u2026"), this);
  urlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
  connect(urlAction, &QAction::triggered, this, &MainWindow::openLocation);
  fileMenu->addAction(urlAction);

  fileMenu->addSeparator();

  fileMenu->addAction(actOpenPlaylist_);
  fileMenu->addSeparator();
  actLibrary_ = new QAction(tr("Biblioteca\u2026"), this);
  actLibrary_->setShortcut(QKeySequence(QStringLiteral("Alt+L")));
  connect(actLibrary_, &QAction::triggered, this, &MainWindow::openLibrary);
  fileMenu->addAction(actLibrary_);
  fileMenu->addAction(actSavePlaylist_);
  fileMenu->addAction(actNewPlaylist_);

  fileMenu->addSeparator();
  auto* quit = new QAction(QStringLiteral("Sair"), this);
  quit->setShortcut(QKeySequence::Quit);
  connect(quit, &QAction::triggered, this, &MainWindow::close);
  fileMenu->addAction(quit);

  auto* playMenu = menuBar()->addMenu(tr("Reprodução"));

  auto* playAction = new QAction(tr("Reproduzir / pausar"), this);
  playAction->setShortcut(Qt::Key_Space);
  connect(playAction, &QAction::triggered, this, &MainWindow::togglePlayPause);
  playMenu->addAction(playAction);

  auto* nextAction = new QAction(tr("Próxima faixa"), this);
  nextAction->setShortcut(Qt::Key_B);
  connect(nextAction, &QAction::triggered, this, &MainWindow::playNext);
  playMenu->addAction(nextAction);

  stopAfterAction_ = new QAction(tr("Parar depois desta faixa"), this);
  stopAfterAction_->setCheckable(true);
  stopAfterAction_->setShortcut(QKeySequence(QStringLiteral("Shift+R")));
  connect(stopAfterAction_, &QAction::toggled, this, [this](bool on) {
    stopAfterCurrent_ = on;
    updateNextUp();
    statusBar()->showMessage(on ? tr("Vou parar no fim desta faixa")
                                : tr("Sigo para a próxima"), 2500);
  });
  playMenu->addAction(stopAfterAction_);
  playMenu->addSeparator();

  auto* prevAction = new QAction(tr("Faixa anterior"), this);
  prevAction->setShortcut(Qt::Key_Z);
  connect(prevAction, &QAction::triggered, this, &MainWindow::playPrevious);
  playMenu->addAction(prevAction);

  auto* optionsMenu = menuBar()->addMenu(tr("Opções"));

  auto* shuffleAction = new QAction(tr("Sorteio"), this);
  shuffleAction->setCheckable(true);
  shuffleAction->setShortcut(Qt::Key_S);
  connect(shuffleAction, &QAction::toggled, this, [this](bool on) {
    if (on == shuffle_) return;
    toggleShuffle();
  });
  optionsMenu->addAction(shuffleAction);
  shuffleAction_ = shuffleAction;

  alwaysOnTopAction_ = new QAction(tr("Sempre no topo"), this);
  alwaysOnTopAction_->setCheckable(true);
  alwaysOnTopAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));
  connect(alwaysOnTopAction_, &QAction::toggled, this, [this](bool on) {
    alwaysOnTop_ = on;
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();
    saveSettings();
  });
  optionsMenu->addAction(alwaysOnTopAction_);

  easyMoveAction_ = new QAction(tr("Arrastar de qualquer ponto"), this);
  easyMoveAction_->setCheckable(true);
  easyMoveAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
  connect(easyMoveAction_, &QAction::toggled, this, &MainWindow::applyEasyMove);
  optionsMenu->addAction(easyMoveAction_);

  repeatAction_ = new QAction(tr("Repetir"), this);
  repeatAction_->setCheckable(true);
  repeatAction_->setShortcut(Qt::Key_R);
  connect(repeatAction_, &QAction::toggled, this, [this](bool on) {
    if (on == repeatActive()) return;
    setRepeatMode(on ? 1 : 0);
  });
  optionsMenu->addAction(repeatAction_);

  auto* eqAction = new QAction(tr("Equalizador…"), this);
  eqAction->setShortcut(QStringLiteral("Alt+G"));
  connect(eqAction, &QAction::triggered, this, &MainWindow::openEqualizer);
  optionsMenu->addAction(eqAction);

  auto* appearanceAction = new QAction(tr("Aparência…"), this);
  appearanceAction->setShortcut(QStringLiteral("Ctrl+,"));
  connect(appearanceAction, &QAction::triggered, this, &MainWindow::openThemeDialog);
  optionsMenu->addAction(appearanceAction);

  auto* rgMenu = optionsMenu->addMenu(QStringLiteral("ReplayGain"));
  auto* rgGroup = new QActionGroup(this);
  rgGroup->setExclusive(true);
  const std::pair<const char*, int> modes[] = {
      {"Desligado", 0}, {"Por faixa", 1}, {"Por álbum", 2}};
  for (const auto& [label, mode] : modes) {
    auto* action = new QAction(QString::fromUtf8(label), this);
    action->setCheckable(true);
    action->setChecked(mode == replayGainMode_);
    rgGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode] { setReplayGainMode(mode); });
    rgMenu->addAction(action);
  }

  auto* prefsAction = new QAction(tr("Preferências…"), this);
  prefsAction->setShortcut(QStringLiteral("Ctrl+P"));
  connect(prefsAction, &QAction::triggered, this, &MainWindow::openPreferences);
  optionsMenu->addAction(prefsAction);

  auto* stopAction = new QAction(QStringLiteral("Parar"), this);
  stopAction->setShortcut(Qt::Key_V);
  connect(stopAction, &QAction::triggered, this, [this] { engine_->stop(); refresh(); });
  playMenu->addAction(stopAction);
}

void MainWindow::wireShortcuts() {
  // Teclas clássicas do Winamp, além do espaço: X toca, C pausa.
  // (O Jump to File é a tecla "," — vive no menu Playlist, porque `QKeySequence(",")`
  //  trata a vírgula como separador de acorde e a sequência sairia vazia.)

  auto* playKey = new QAction(this);
  playKey->setShortcut(Qt::Key_X);
  connect(playKey, &QAction::triggered, this, [this] {
    if (engine_->state() != wamp::PlaybackState::Playing) engine_->play();
    refresh();
  });
  addAction(playKey);

  auto* pauseKey = new QAction(this);
  pauseKey->setShortcut(Qt::Key_C);
  connect(pauseKey, &QAction::triggered, this, [this] {
    if (engine_->state() == wamp::PlaybackState::Playing) engine_->pause();
    refresh();
  });
  addAction(pauseKey);
}

void MainWindow::openFileDialog() {
  playlistDirty_ = true;
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Abrir arquivo de áudio"), QString(),
      QStringLiteral("Áudio (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac *.wma *.ape);;"
                     "Todos os arquivos (*)"));
  if (!path.isEmpty()) openPath(path);
}

void MainWindow::addFilesDialog() {
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("Adicionar à playlist"), QString(),
      QStringLiteral("Áudio (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac *.wma *.ape);;"
                     "Todos os arquivos (*)"));
  if (!paths.isEmpty()) addPaths(paths);
}

void MainWindow::loadPlaylistDialog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Abrir playlist"), QString(),
      tr("Playlists (*.m3u *.m3u8 *.pls *.b4s *.wpl);;Todos os arquivos (*)"));
  if (path.isEmpty()) return;

  std::string error;
  if (!appendPlaylistFile(path, &error)) {
    showError(QString::fromStdString(error));
    return;
  }
  setPlaylistVisible(true);
  updateNextUp();
  statusBar()->showMessage(
      tr("%1 faixas carregadas de %2").arg(playlist_->size()).arg(QFileInfo(path).fileName()),
      4000);
}

void MainWindow::savePlaylistDialog() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Salvar playlist"), QStringLiteral("wamp.m3u"),
      tr("Playlist (*.m3u *.pls *.b4s *.wpl)"));
  if (path.isEmpty()) return;

  // Sem extensão conhecida, grava no .m3u: é o formato que qualquer player abre.
  QString target = path;
  const QString suffix = QFileInfo(target).suffix().toLower();
  if (suffix != QLatin1String("m3u") && suffix != QLatin1String("m3u8") &&
      suffix != QLatin1String("pls") && suffix != QLatin1String("b4s") &&
      suffix != QLatin1String("wpl")) {
    target += QStringLiteral(".m3u");
  }

  std::string error;
  if (!playlist_->savePlaylist(target.toStdString(), &error)) {
    showError(QString::fromStdString(error));
    return;
  }
  statusBar()->showMessage(tr("Playlist salva em ") + QFileInfo(target).fileName(),
                           4000);
}

bool MainWindow::appendPlaylistFile(const QString& path, std::string* error) {
  std::vector<std::string> entries;
  if (!wamp::loadPlaylistFile(path.toStdString(), &entries, error)) return false;
  const std::vector<std::string> missing = missesFor(entries);
  playlist_->addAll(entries);
  playlistDirty_ = true;
  metadataTotal_ += missing.size();
  scanner_->enqueueAll(missing);
  updateMetadataStatus();
  rebuildPlaylist();
  startReading();
  return true;
}

void MainWindow::warnUnsupportedPath(const QString& path) {
  const QFileInfo info(path);
  auto* box = new QMessageBox(this);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setIcon(QMessageBox::Information);
  box->setWindowTitle(tr("Arquivo não suportado"));
  box->setText(tr("“%1” não é um formato suportado.").arg(info.fileName()));
  box->setInformativeText(
      tr("O Amplia toca: %1.").arg(wamp::ui::audioExtensions().join(tr(", "))));

  QPushButton* open = box->addButton(tr("Abrir no player do sistema"),
                                     QMessageBox::ActionRole);
  box->addButton(QCoreApplication::translate("MainWindow", "Entendido"),
                 QMessageBox::AcceptRole);
  connect(box, &QMessageBox::buttonClicked, this, [open, path](QAbstractButton* clicked) {
    if (clicked == open) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  });
  box->open();
}

void MainWindow::openFile(const QString& path) {
  if (!isPlaylistFile(path) && !isAudioFile(path) &&
      !wamp::probeAudioOnly(path.toStdString())) {
    // Extensão fora da lista: pode ser áudio num contêiner exótico (.mka, .m4b) — o
    // FFmpeg decide. Vídeo e o resto caem na mensagem, em vez de "falha ao abrir".
    warnUnsupportedPath(path);
    return;
  }
  if (!isPlaylistFile(path)) {
    openPath(path);
    return;
  }
  std::string error;
  if (!appendPlaylistFile(path, &error)) {
    showError(QString::fromStdString(error));
    return;
  }
  setPlaylistVisible(true);
  // Abrir uma lista pela linha de comando toca a primeira faixa, como abrir um arquivo.
  if (!playlist_->hasCurrent() && !playlist_->empty()) {
    playlist_->setCurrent(0);
    openPath(QString::fromStdString(playlist_->at(0)), true);
  }
}

void MainWindow::addFiles(const QStringList& paths) { addPaths(paths); }

bool MainWindow::openPath(const QString& path, bool autoplay) {
  refresh();
  // "Missing file" do original: só descobrimos ao tocar. Não abre, marca e devolve false
  // para quem chamou pular adiante.
  if (!QFileInfo::exists(path)) {
    setMissing(path.toStdString(), true);
    showError(tr("Arquivo não encontrado: ") + QFileInfo(path).fileName());
    return false;
  }
  if (!engine_->load(path.toStdString())) {
    showError(QString::fromStdString(engine_->lastError()));
    return false;
  }
  applyReplayGain(engine_->track());
  if (autoplay) {
    engine_->play();
    armPlaycount(path.toStdString());
  }

  const std::string native = path.toStdString();
  std::size_t index = wamp::Playlist::kNone;
  for (std::size_t i = 0; i < playlist_->size(); ++i) {
    if (playlist_->at(i) == native) {
      index = i;
      break;
    }
  }
  if (index == wamp::Playlist::kNone) index = playlist_->add(native);
  playlist_->setCurrent(index);
  wamp::TrackInfo cached;
  if (!cachedInfo(native, &cached)) scanner_->enqueue(native);
  rebuildPlaylist();

  const wamp::TrackInfo track = engine_->track();
  if (!track.path.empty()) {
    // A faixa que toca já tem tags do decoder: semeia o cache para a linha aparecer
    // completa na hora, sem esperar a fila.
    metadata_[track.path] = track;
    setMissing(track.path, false);
    if (cache_) cache_->store(track);
    updateArtwork(track);
    if (QListWidgetItem* item = list_->item(static_cast<int>(index))) {
      item->setText(rowLabel(track.path, true));
      item->setData(kDurationRole, durationText(track.path));
      item->setToolTip(QString::fromStdString(track.path));
    }
    if (autoplay) {
      beginScrobble(track);
    } else {
      // Carregada em pausa: a sessão de scrobble só começa quando o play vier.
      scrobbleTrack_ = track;
      scrobbleStartUnix_ = 0;
      scrobblePlayedMs_ = 0;
      scrobbleLastPos_ = 0;
      scrobbleSent_ = false;
      scrobbleAnnounced_ = false;
    }
  }
  updateNextUp();
  refresh();
  return true;
}

void MainWindow::addPaths(const QStringList& paths) {
  // Playlist dentro do que foi solto/aberto é lida como lista, não como áudio.
  QStringList playlists;
  QStringList plain;
  QStringList unsupported;
  for (const QString& path : paths) {
    const QFileInfo info(path);
    if (info.isFile() && isPlaylistFile(path)) playlists.append(path);
    else if (info.isFile() && !isAudioFile(path)) unsupported.append(path);
    else plain.append(path);
  }

  // Um arquivo solto pode ser áudio num contêiner fora da lista: pergunta ao FFmpeg.
  // Em lote isso custaria uma abertura por arquivo — aí a lista branca resolve.
  if (unsupported.size() == 1 && paths.size() == 1 &&
      wamp::probeAudioOnly(unsupported.front().toStdString())) {
    plain.append(unsupported.takeFirst());
  }
  int playlistFailures = 0;
  for (const QString& file : playlists) {
    std::string error;
    if (!appendPlaylistFile(file, &error)) {
      ++playlistFailures;
      statusBar()->showMessage(QString::fromStdString(error), 6000);
    }
  }

  // Pastas são varridas antes de entrar na lista: um item de pasta não tem o que tocar.
  int folders = 0;
  int ignored = 0;
  const QStringList files = expandAudioPaths(plain, &folders, &ignored);
  if (files.isEmpty()) {
    if (!playlists.isEmpty()) {
      if (playlistFailures == 0) setPlaylistVisible(true);
      return;
    }
    if (!unsupported.isEmpty()) {
      // Arrastar 200 arquivos não pode virar 200 avisos: em lote, filtra em silêncio.
      if (paths.size() == 1) {
        warnUnsupportedPath(unsupported.front());
      } else {
        statusBar()->showMessage(
            tr("%1 arquivo(s) não suportado(s) ignorado(s)").arg(unsupported.size()),
            6000);
      }
      return;
    }
    statusBar()->showMessage(
        folders > 0 ? tr("Nenhum arquivo de áudio nesta(s) pasta(s)")
                    : tr("Nada para adicionar"),
        6000);
    return;
  }

  std::vector<std::string> native;
  native.reserve(static_cast<std::size_t>(files.size()));
  for (const QString& path : files) native.push_back(path.toStdString());

  const std::vector<std::string> missing = missesFor(native);
  const std::size_t first = playlist_->addAll(native);
  playlistDirty_ = true;
  metadataTotal_ += missing.size();
  scanner_->enqueueAll(missing);
  updateMetadataStatus();
  rebuildPlaylist();
  startReading();

  // Ao importar mais de uma faixa, mostra a lista: é onde o usuário vai rearrumar.
  if (!playlistAutoShown_ && playlist_->size() > 1) {
    playlistAutoShown_ = true;
    setPlaylistVisible(true);
  }

  // Organizar a lista não deve começar a tocar: a primeira faixa só é carregada,
  // em pausa, para a janela já mostrar o que foi adicionado.
  QString note;
  if (folders > 0) {
    note = tr("%1 arquivo(s) de %2 pasta(s)").arg(files.size()).arg(folders);
  }
  ignored += unsupported.size();
  if (ignored > 0) {
    if (!note.isEmpty()) note += tr(" • ");
    note += tr("%1 ignorado(s)").arg(ignored);
  }

  if (first != wamp::Playlist::kNone && engine_->state() != wamp::PlaybackState::Playing &&
      !playlist_->hasCurrent()) {
    playlist_->setCurrent(first);
    openPath(QString::fromStdString(playlist_->at(first)), false);
    list_->setCurrentRow(static_cast<int>(first));
    const QString loaded = tr("Faixa carregada em pausa — Play para ouvir");
    statusBar()->showMessage(note.isEmpty() ? loaded : loaded + tr(" • ") + note, 6000);
  } else if (!note.isEmpty()) {
    statusBar()->showMessage(note, 6000);
  }
}

void MainWindow::openJumpToFile() {
  if (QApplication::activeModalWidget() != nullptr) return;  // não rouba foco de diálogo

  if (jumpDialog_ != nullptr) {
    jumpDialog_->raise();
    jumpDialog_->activateWindow();
    return;
  }
  if (playlist_->empty()) {
    statusBar()->showMessage(tr("A playlist está vazia"), 3000);
    return;
  }

  QVector<wamp::ui::JumpEntry> entries;
  entries.reserve(static_cast<int>(playlist_->size()));
  for (const std::string& path : playlist_->items()) {
    wamp::ui::JumpEntry entry;
    entry.path = QString::fromStdString(path);
    const auto it = metadata_.find(path);
    if (it != metadata_.end()) {
      entry.title = QString::fromStdString(it->second.title);
      entry.artist = QString::fromStdString(it->second.artist);
      entry.album = QString::fromStdString(it->second.album);
    }
    if (entry.title.isEmpty()) entry.title = baseName(path);
    entries.push_back(entry);
  }

  jumpDialog_ = new wamp::ui::JumpDialog(std::move(entries), this);
  jumpDialog_->setAttribute(Qt::WA_DeleteOnClose);
  connect(jumpDialog_, &wamp::ui::JumpDialog::playRequested, this, &MainWindow::jumpPlay);
  connect(jumpDialog_, &wamp::ui::JumpDialog::queueRequested, this, &MainWindow::jumpQueue);
  connect(jumpDialog_, &QObject::destroyed, this, [this] { jumpDialog_ = nullptr; });
  jumpDialog_->show();
  jumpDialog_->activateWindow();
}

void MainWindow::jumpPlay(const QString& path) {
  const std::string native = path.toStdString();
  for (std::size_t i = 0; i < playlist_->size(); ++i) {
    if (playlist_->at(i) != native) continue;
    playIndex(i);
    return;
  }
}

void MainWindow::jumpQueue(const QString& path) {
  const std::string native = path.toStdString();
  for (std::size_t i = 0; i < playlist_->size(); ++i) {
    if (playlist_->at(i) != native) continue;
    if (!playlist_->queuePosition(i)) {
      playlist_->enqueue(i);
      refreshQueue();
      updateNextUp();
    }
    statusBar()->showMessage(tr("Enfileirada: %1").arg(baseName(native)), 3000);
    return;
  }
}

void MainWindow::openFileInfo() {
  if (playlist_->empty()) {
    statusBar()->showMessage(tr("Nada para mostrar: a playlist está vazia"), 3000);
    return;
  }
  std::size_t index = playlist_->hasCurrent() ? playlist_->currentIndex() : 0;
  if (QListWidgetItem* item = list_->currentItem(); item != nullptr && list_->currentRow() >= 0) {
    index = static_cast<std::size_t>(list_->currentRow());
  }

  QSettings settings(settingsFile(), QSettings::IniFormat);
  const bool safeSave = settings.value(QStringLiteral("fileinfo/safeSave"), true).toBool();

  wamp::ui::FileInfoDialog dialog(playlist_->items(), index, safeSave, this);
  connect(&dialog, &wamp::ui::FileInfoDialog::fileSaved, this, &MainWindow::onFileSaved);
  dialog.exec();
  settings.setValue(QStringLiteral("fileinfo/safeSave"), dialog.safeSaveEnabled());
}

void MainWindow::onFileSaved(const QString& path) {
  const std::string native = path.toStdString();
  wamp::TrackInfo info;
  std::string error;
  if (!wamp::probeTrack(native, &info, &error)) {
    statusBar()->showMessage(tr("Tags salvas, mas a releitura falhou"), 4000);
    return;
  }
  metadata_[native] = info;
  if (cache_) cache_->store(info);

  const bool current = playlist_->hasCurrent() && playlist_->at(playlist_->currentIndex()) == native;
  for (int row = 0; row < list_->count(); ++row) {
    QListWidgetItem* item = list_->item(row);
    if (item->data(Qt::UserRole).toString() != path) continue;
    item->setText(rowLabel(native, current));
  }
  if (current) {
    updateWindowTitle();
  }
  updateTray();
  statusBar()->showMessage(tr("Tags salvas"), 3000);
}

void MainWindow::newPlaylist() {
  if (playlist_->empty()) {
    statusBar()->showMessage(tr("A playlist já está vazia"), 3000);
    return;
  }
  const auto answer = QMessageBox::question(
      this, tr("Nova playlist"),
      QStringLiteral("Começar uma playlist nova? As %1 faixas atuais saem da lista "
                     "(os arquivos continuam no disco).")
          .arg(playlist_->size()),
      QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
  if (answer != QMessageBox::Yes) return;

  playlist_->clear();
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Playlist nova"), 3000);
}

void MainWindow::addFolderDialog() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Adicionar pasta"), QDir::homePath());
  if (dir.isEmpty()) return;
  addPaths(QStringList{dir});
}

void MainWindow::selectAllItems() {
  if (list_->count() == 0) return;
  list_->selectAll();
}

void MainWindow::selectNone() { list_->clearSelection(); }

void MainWindow::invertSelection() {
  const QList<QListWidgetItem*> selected = list_->selectedItems();
  const bool allSelected = selected.size() == list_->count();
  list_->clearSelection();
  if (allSelected) return;
  for (int row = 0; row < list_->count(); ++row) {
    QListWidgetItem* item = list_->item(row);
    if (!selected.contains(item)) item->setSelected(true);
  }
}

void MainWindow::cropSelected() {
  const QList<QListWidgetItem*> selected = list_->selectedItems();
  if (selected.isEmpty()) {
    statusBar()->showMessage(tr("Nada selecionado para manter"), 3000);
    return;
  }
  std::vector<std::string> keep;
  keep.reserve(static_cast<std::size_t>(selected.size()));
  for (int row = 0; row < list_->count(); ++row) {
    if (!list_->item(row)->isSelected()) continue;
    keep.push_back(playlist_->at(static_cast<std::size_t>(row)));
  }

  const std::string currentPath =
      playlist_->hasCurrent() ? playlist_->at(playlist_->currentIndex()) : std::string();
  auto position = [&keep](const std::string& path) {
    const auto it = std::find(keep.begin(), keep.end(), path);
    return it == keep.end() ? wamp::Playlist::kNone
                            : static_cast<std::size_t>(it - keep.begin());
  };
  playlist_->setItems(std::move(keep), position(currentPath));
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Só a seleção ficou na playlist"), 3000);
}

void MainWindow::clearPlaylist() {
  if (playlist_->empty()) return;
  const auto answer = QMessageBox::question(
      this, tr("Limpar playlist"),
      tr("Remover as %1 faixas da playlist? (os arquivos não são apagados)")
          .arg(playlist_->size()),
      QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
  if (answer != QMessageBox::Yes) return;

  playlist_->clear();
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Playlist limpa"), 3000);
}

void MainWindow::removeMissing() {
  const std::size_t removed = playlist_->removeMissingFiles();
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  if (removed == 0) {
    statusBar()->showMessage(tr("Todas as faixas existem no disco"), 4000);
  } else {
    statusBar()->showMessage(
        tr("%1 faixa(s) fora do disco removida(s) da lista").arg(removed), 5000);
  }
}

bool MainWindow::metadataReady() {
  if (scanner_ && scanner_->pending() > 0) {
    statusBar()->showMessage(
        tr("Aguarde a leitura das tags terminar para ordenar"), 5000);
    return false;
  }
  return true;
}

void MainWindow::sortPlaylist(int mode) {
  if (!metadataReady() || playlist_->size() < 2) return;

  const std::string currentPath =
      playlist_->hasCurrent() ? playlist_->at(playlist_->currentIndex()) : std::string();

  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);

  auto keyFor = [this, mode](const std::string& path) -> QString {
    if (mode == 0) {
      const auto it = metadata_.find(path);
      if (it != metadata_.end() && !it->second.title.empty()) {
        return QString::fromStdString(it->second.title);
      }
      return QFileInfo(QString::fromStdString(path)).fileName();
    }
    if (mode == 1) return QFileInfo(QString::fromStdString(path)).fileName();
    return QString::fromStdString(path);
  };

  std::vector<std::string> items = playlist_->items();
  std::stable_sort(items.begin(), items.end(), [&](const std::string& a, const std::string& b) {
    const int order = collator.compare(keyFor(a), keyFor(b));
    if (order != 0) return order < 0;
    return collator.compare(QString::fromStdString(a), QString::fromStdString(b)) < 0;
  });

  std::size_t current = wamp::Playlist::kNone;
  if (!currentPath.empty()) {
    const auto it = std::find(items.begin(), items.end(), currentPath);
    if (it != items.end()) current = static_cast<std::size_t>(it - items.begin());
  }
  playlist_->setItems(std::move(items), current);
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Playlist ordenada"), 3000);
}

void MainWindow::reverseList() {
  if (playlist_->size() < 2) return;
  playlist_->reverseOrder();
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Ordem invertida"), 3000);
}

void MainWindow::shuffleList() {
  if (playlist_->size() < 2) return;
  playlist_->shuffleOrder();
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
  statusBar()->showMessage(tr("Lista embaralhada"), 3000);
}

void MainWindow::playIndex(std::size_t index) {
  const std::size_t total = playlist_->size();
  if (index >= total) return;
  // Pula as que não existem: no máximo uma volta na lista, para não girar para sempre.
  for (std::size_t tentativa = 0; tentativa < total; ++tentativa) {
    const std::size_t atual = (index + tentativa) % total;
    playlist_->setCurrent(atual);
    if (openPath(QString::fromStdString(playlist_->at(atual)))) {
      list_->setCurrentRow(static_cast<int>(atual));
      return;
    }
  }
  statusBar()->showMessage(tr("Nenhuma faixa desta lista pôde ser aberta"), 6000);
}

void MainWindow::playNext() {
  if (playlist_->empty()) return;
  if (!nextUpPath_.empty()) {
    const QString next = QString::fromStdString(nextUpPath_);
    if (nextUpFromQueue_) {
      playlist_->dequeue();
      refreshQueue();
    }
    if (!openPath(next)) {
      // Faixa da fila/nova não abriu (arquivo ausente): segue adiante pela lista.
      playIndex(nextTrackIndex());
    }
    return;
  }
  if (!shuffle_ && playlist_->hasCurrent() && playlist_->isLast(playlist_->currentIndex())) {
    statusBar()->showMessage(tr("Fim da playlist"), 3000);
    return;
  }
  playIndex(nextTrackIndex());
}

void MainWindow::updateNextUp() {
  nextUpPath_.clear();
  nextUpFromQueue_ = false;
  if (playlist_->empty() || !playlist_->hasCurrent()) {
    engine_->setNextTrack(std::string());
    return;
  }

  // "Parar depois desta": sem prefetch, o fim chega pelo onEnded e paramos ali.
  if (stopAfterCurrent_) {
    engine_->setNextTrack(std::string());
    return;
  }

  // Repetindo uma faixa só, não há próxima: sem prefetch, o fim chega pelo onEnded.
  if (repeatMode_ == 2) {
    engine_->setNextTrack(std::string());
    return;
  }

  const std::string queued = playlist_->queueFront();
  if (!queued.empty()) {
    nextUpPath_ = queued;
    nextUpFromQueue_ = true;
    engine_->setNextTrack(queued);
    return;
  }

  const std::size_t index = nextTrackIndex();
  if (index != wamp::Playlist::kNone && index < playlist_->size()) {
    nextUpPath_ = playlist_->at(index);
    engine_->setNextTrack(nextUpPath_);
  } else {
    engine_->setNextTrack(std::string());
  }
}

void MainWindow::applyGaplessTrackChange(const QString& path) {
  const std::string native = path.toStdString();
  if (nextUpFromQueue_ && playlist_->queueFront() == native) {
    playlist_->dequeue();
    refreshQueue();
  }

  std::size_t index = wamp::Playlist::kNone;
  for (std::size_t i = 0; i < playlist_->size(); ++i) {
    if (playlist_->at(i) == native) {
      index = i;
      break;
    }
  }
  if (index == wamp::Playlist::kNone) index = playlist_->add(native);
  playlist_->setCurrent(index);
  rebuildPlaylist();
  list_->scrollToItem(list_->item(static_cast<int>(index)));

  const wamp::TrackInfo track = engine_->track();
  if (!track.path.empty()) {
    metadata_[track.path] = track;
    setMissing(track.path, false);
    if (cache_) cache_->store(track);
    updateArtwork(track);
    beginScrobble(track);
  }
  scanner_->enqueue(native);
  updateNextUp();
  applyReplayGain(engine_->track());
  refresh();
}

std::size_t MainWindow::nextTrackIndex() {
  if (playlist_->empty()) return wamp::Playlist::kNone;
  // Sem sorteio e sem repetição, a última faixa é o fim: `nextIndex()` daria a volta.
  if (!shuffle_ && repeatMode_ == 0 && playlist_->isLast(playlist_->currentIndex())) {
    return wamp::Playlist::kNone;
  }
  if (shuffle_) return playlist_->shuffledNextIndex(shuffleLevel_);
  return playlist_->nextIndex();
}

void MainWindow::toggleTimeMode() {
  remaining_ = !remaining_;
  if (display_ != nullptr) display_->setShowRemaining(remaining_);
  saveSettings();
}

void MainWindow::setPlaylistFontSize(int px) {
  playlistFontPx_ = std::clamp(px, 9, 22);
  if (list_ == nullptr) return;
  QFont font = list_->font();
  font.setFamily(QStringLiteral("monospace"));
  font.setStyleHint(QFont::Monospace);
  font.setPixelSize(playlistFontPx_);
  list_->setFont(font);
  if (delegate_ != nullptr) rebuildPlaylist();
}

void MainWindow::jumpTracks(int delta) {
  if (playlist_ == nullptr || playlist_->empty()) return;
  const int size = static_cast<int>(playlist_->size());
  int current = playlist_->hasCurrent() ? static_cast<int>(playlist_->currentIndex()) : -1;
  int target = std::clamp(current + delta, 0, size - 1);
  playIndex(static_cast<std::size_t>(target));
}

void MainWindow::openLocation() {
  bool ok = false;
  const QString url = QInputDialog::getText(this, tr("Abrir URL"),
                                            tr("Endereço da rádio ou do arquivo:"),
                                            QLineEdit::Normal, QString(), &ok);
  if (!ok) return;
  const QString trimmed = url.trimmed();
  if (trimmed.isEmpty()) return;
  const QString scheme = QUrl(trimmed).scheme().toLower();
  const QStringList allowed = {QStringLiteral("http"), QStringLiteral("https"),
                               QStringLiteral("mms"), QStringLiteral("mmsh"),
                               QStringLiteral("rtsp"), QStringLiteral("rtmp")};
  if (!allowed.contains(scheme)) {
    QMessageBox::information(this, tr("Abrir URL"),
                             tr("Endereço não suportado. Use http, https, mms ou rtsp."));
    return;
  }
  // Rede não passa pela varredura de pastas nem pela fila de metadados: o FFmpeg
  // abre o stream direto, e sondar URL a cada item atrasaria a lista.
  const std::string path = trimmed.toStdString();
  playlist_->add(path);
  playlistDirty_ = true;
  const std::size_t index = playlist_->size() - 1;
  playlist_->setCurrent(index);
  rebuildPlaylist();
  openPath(trimmed);
  statusBar()->showMessage(tr("Abrindo %1").arg(trimmed), 4000);
}

void MainWindow::focusPlaylist() {
  setPlaylistVisible(true);
}

void MainWindow::focusEqualizer() {
  openEqualizer();
}

void MainWindow::cycleWindows() {
  QList<QWidget*> windows;
  windows << this;
  if (!playlistIntegrated() && playlistWindow_ != nullptr && playlistWindow_->isVisible()) {
    windows << playlistWindow_;
  }
  if (eqDialog_ != nullptr && eqDialog_->isVisible()) windows << eqDialog_.get();
  if (windows.size() < 2) return;
  QWidget* active = QApplication::activeWindow();
  const int index = windows.indexOf(active);
  QWidget* next = windows[(index + 1) % windows.size()];
  next->raise();
  next->activateWindow();
}

void MainWindow::applyEasyMove(bool on) {
  easyMove_ = on;
  statusBar()->showMessage(on ? tr("Arrastar de qualquer ponto ligado")
                              : tr("Arrastar de qualquer ponto desligado"), 2500);
  saveSettings();
}

void MainWindow::toggleMute() {
  const int current = volumeSlider_ != nullptr ? volumeSlider_->value() : 0;
  const int target = current == 0 ? std::max(1, lastVolume_) : 0;
  if (volumeSlider_ != nullptr) volumeSlider_->setValue(target);
  engine_->setVolume(static_cast<float>(target) / 100.0f);
  statusBar()->showMessage(target == 0 ? QStringLiteral("Silenciado")
                                       : tr("Volume %1%").arg(target),
                           2000);
  saveSettings();
}

void MainWindow::toggleShuffle() {
  shuffle_ = !shuffle_;
  if (shuffle_) playlist_->resetShuffle();
  updateNextUp();
  shuffleButton_->setChecked(shuffle_);
  if (shuffleAction_ != nullptr && shuffleAction_->isChecked() != shuffle_) {
    QSignalBlocker block(shuffleAction_);
    shuffleAction_->setChecked(shuffle_);
  }
  statusBar()->showMessage(
      shuffle_ ? tr("Sorteio ligado (%1%)").arg(qRound(shuffleLevel_ * 100))
               : tr("Sorteio desligado"),
      3000);
  saveSettings();
}

void MainWindow::toggleRepeat() { setRepeatMode((repeatMode_ + 1) % 3); }

void MainWindow::setRepeatMode(int mode) {
  repeatMode_ = std::clamp(mode, 0, 2);
  if (repeatAction_ != nullptr && repeatAction_->isChecked() != repeatActive()) {
    QSignalBlocker block(repeatAction_);
    repeatAction_->setChecked(repeatActive());
  }
  if (repeatButton_ != nullptr) {
    repeatButton_->setChecked(repeatActive());
    const bool one = repeatMode_ == 2;
    repeatButton_->setGlyph([one](QPainter& painter, const QRectF& box, const QColor& color) {
      wamp::ui::paintRepeat(painter, box, color, one);
    });
    repeatButton_->setToolTip(repeatMode_ == 2   ? tr("Repetir a faixa (R)")
                              : repeatMode_ == 1 ? tr("Repetir a lista (R)")
                                                 : tr("Repetição desligada (R)"));
  }
  updateNextUp();
  updateTray();
  statusBar()->showMessage(repeatMode_ == 2   ? tr("Repetindo a faixa")
                           : repeatMode_ == 1 ? tr("Repetindo a lista")
                                              : tr("Repetição desligada"),
                           3000);
  saveSettings();
  refresh();
}

void MainWindow::buildTray() {
  if (qEnvironmentVariableIsSet("AMPLIA_NO_TRAY")) {
    qInfo("amplia: agente desligado por AMPLIA_NO_TRAY");
    return;
  }
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    // Sem observador de bandeja na sessão (SNI): o agente simplesmente não existe.
    qInfo("amplia: sem bandeja neste ambiente; o agente não foi criado");
    return;
  }

  QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.cassiofiorin.Amplia"));
  if (icon.isNull()) icon = windowIcon();
  if (icon.isNull()) icon = QApplication::windowIcon();

  tray_ = std::make_unique<QSystemTrayIcon>(icon, this);
  tray_->setToolTip(QStringLiteral("Amplia"));

  trayMenu_ = std::make_unique<QMenu>();
  trayShowAction_ = trayMenu_->addAction(tr("Mostrar janela"));
  connect(trayShowAction_, &QAction::triggered, this, [this] {
    if (isVisible()) {
      hide();
    } else {
      show();
      raise();
      activateWindow();
    }
    updateTray();
  });

  trayMenu_->addSeparator();
  trayPlayAction_ = trayMenu_->addAction(QStringLiteral("Reproduzir"));
  connect(trayPlayAction_, &QAction::triggered, this, &MainWindow::togglePlayPause);
  trayPrevAction_ = trayMenu_->addAction(tr("Anterior"));
  connect(trayPrevAction_, &QAction::triggered, this, &MainWindow::playPrevious);
  trayNextAction_ = trayMenu_->addAction(tr("Próxima"));
  connect(trayNextAction_, &QAction::triggered, this, &MainWindow::playNext);

  trayMenu_->addSeparator();
  trayShuffleAction_ = trayMenu_->addAction(tr("Aleatório"));
  trayShuffleAction_->setCheckable(true);
  connect(trayShuffleAction_, &QAction::toggled, this, [this](bool on) {
    if (on == shuffle_) return;
    toggleShuffle();
  });
  trayRepeatAction_ = trayMenu_->addAction(tr("Repetir"));
  trayRepeatAction_->setCheckable(true);
  connect(trayRepeatAction_, &QAction::toggled, this, [this](bool on) {
    if (on == repeatActive()) return;
    setRepeatMode(on ? 1 : 0);
  });

  trayMenu_->addSeparator();
  auto* quitAction = trayMenu_->addAction(QStringLiteral("Sair"));
  connect(quitAction, &QAction::triggered, this, [this] {
    quitting_ = true;
    close();
  });

  tray_->setContextMenu(trayMenu_.get());
  connect(tray_.get(), &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
              if (isVisible()) {
                hide();
              } else {
                show();
                raise();
                activateWindow();
              }
              updateTray();
            } else if (reason == QSystemTrayIcon::MiddleClick) {
              togglePlayPause();
            }
          });
  tray_->show();
  updateTray();
}

void MainWindow::updateTray() {
  if (tray_ == nullptr) return;
  const wamp::TrackInfo track = engine_->track();
  const wamp::PlaybackState state = engine_->state();
  const bool playing = state == wamp::PlaybackState::Playing;

  if (track.path.empty()) {
    tray_->setToolTip(QStringLiteral("Amplia"));
  } else {
    tray_->setToolTip(tr("%1\nAmplia — %2")
                          .arg(QString::fromStdString(track.displayTitle()),
                               playing ? QStringLiteral("tocando") : tr("em pausa")));
  }

  if (trayPlayAction_ != nullptr) {
    trayPlayAction_->setText(playing ? QStringLiteral("Pausar") : QStringLiteral("Reproduzir"));
  }
  if (trayShowAction_ != nullptr) {
    trayShowAction_->setText(isVisible() ? tr("Ocultar janela")
                                         : tr("Mostrar janela"));
  }
  if (trayShuffleAction_ != nullptr && trayShuffleAction_->isChecked() != shuffle_) {
    QSignalBlocker block(trayShuffleAction_);
    trayShuffleAction_->setChecked(shuffle_);
  }
  if (trayRepeatAction_ != nullptr && trayRepeatAction_->isChecked() != repeatActive()) {
    QSignalBlocker block(trayRepeatAction_);
    trayRepeatAction_->setChecked(repeatActive());
  }
}

void MainWindow::openEqualizer() {
  if (!eqDialog_) {
    eqDialog_ = std::make_unique<wamp::ui::EqualizerDialog>(
        engine_->equalizerEnabled(), engine_->equalizerBands(), engine_->equalizerPreamp(), this);
    // A curva carregada pode ter vindo de um preset salvo: mantém o nome para o
    // "Salvar curva" sugerir o mesmo, em vez de "Personalizado".
    {
      QSettings settings(settingsFile(), QSettings::IniFormat);
      const QString name = settings.value(QStringLiteral("eq/name")).toString();
      if (!name.isEmpty()) eqDialog_->setPresetName(name);
    }
    eqSaveTimer_ = new QTimer(this);
    eqSaveTimer_->setSingleShot(true);
    eqSaveTimer_->setInterval(500);
    connect(eqSaveTimer_, &QTimer::timeout, this, &MainWindow::saveEqualizer);
    connect(eqDialog_.get(), &wamp::ui::EqualizerDialog::changed, this,
            &MainWindow::applyEqualizer);
  }
  // Janela auxiliar: sem entrada na barra de tarefas e sem escurecer o pai.
  eqDialog_->setWindowFlag(Qt::Tool, true);
  eqDialog_->show();
  // Mesmo motivo da playlist: no Wayland é o transient parent que tira a janela da
  // barra de tarefas, e ele só pode ser declarado com as duas janelas já criadas.
  if (eqDialog_->windowHandle() != nullptr && windowHandle() != nullptr) {
    eqDialog_->windowHandle()->setTransientParent(windowHandle());
  }
  eqDialog_->raise();
  eqDialog_->activateWindow();
}

void MainWindow::setReplayGainMode(int mode) {
  replayGainMode_ = std::clamp(mode, 0, 2);
  applyReplayGain(engine_->track());
  QSettings settings(settingsFile(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("replaygain/mode"), replayGainMode_);
  QString modeName;
  switch (replayGainMode_) {
    case 1: modeName = tr("por faixa"); break;
    case 2: modeName = tr("por álbum"); break;
    default: modeName = tr("desligado"); break;
  }
  statusBar()->showMessage(tr("ReplayGain: %1").arg(modeName), 4000);
}

void MainWindow::applyReplayGain(const wamp::TrackInfo& track) {
  if (replayGainMode_ == 0 || track.path.empty()) {
    engine_->setReplayGainFactor(1.0);
    return;
  }

  wamp::TagFormat format;
  std::string error;
  if (!wamp::TagEditor::read(track.path, nullptr, &format, nullptr, &error)) {
    engine_->setReplayGainFactor(1.0);
    return;
  }

  // Álbum usa os campos de álbum; se a faixa só tiver os de faixa, ela cai neles.
  const bool album = replayGainMode_ == 2;
  std::string gain = album ? format.replayGainAlbum : format.replayGainTrack;
  std::string peak = album ? format.replayGainAlbumPeak : format.replayGainTrackPeak;
  if (gain.empty() && album) {
    gain = format.replayGainTrack;
    peak = format.replayGainTrackPeak;
  }
  const double factor = wamp::replayGainFactor(gain, peak);
  engine_->setReplayGainFactor(factor);
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_RG")) {
    qInfo("rg: modo=%d ganho=(%s) pico=(%s) fator=%.4f", replayGainMode_, gain.c_str(), peak.c_str(),
          factor);
  }
}

void MainWindow::applyVisualSettings() {
  if (visualTimer_ == nullptr) return;
  visualTimer_->setInterval(1000 / std::max(10, std::min(60, visualFps_)));
  if (visualEnabled_) {
    visualTimer_->start();
  } else {
    visualTimer_->stop();
    display_->setVisual(wamp::AudioAnalyzer::Frame{});
  }
}

void MainWindow::calibrateVisual(PreferencesDialog* dialog) {
  if (display_ == nullptr) return;

  // Mede o custo real de desenhar o painel: alimenta um analisador local com um tom e
  // desenha o painel de verdade num pixmap, quadro a quadro. O primeiro segundo é descartado
  // (cache de fonte e primeira pintura não representam o regime).
  constexpr double kSeconds = 10.0;
  constexpr double kWarmup = 1.0;

  QProgressDialog progress(tr("Calibrando o visualizador…"), tr("Cancelar"),
                           0, 100, this);
  progress.setWindowTitle(tr("Calibração"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setValue(0);

  wamp::AudioAnalyzer analyzer;
  std::vector<float> chunk(1024 * 2);
  QPixmap target(display_->size());
  QElapsedTimer wall;
  wall.start();
  std::vector<double> costs;
  double phase = 0.0;
  bool cancelled = false;

  while (true) {
    const double elapsed = wall.nsecsElapsed() / 1e9;
    if (elapsed >= kSeconds) break;
    if (progress.wasCanceled()) {
      cancelled = true;
      break;
    }
    progress.setValue(static_cast<int>(elapsed / kSeconds * 100.0));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    for (std::size_t i = 0; i < chunk.size() / 2; ++i) {
      const float sample = static_cast<float>(std::sin(phase));
      phase += 2.0 * M_PI * 1000.0 / 48000.0;
      chunk[i * 2] = sample;
      chunk[i * 2 + 1] = sample * 0.7f;
    }
    analyzer.push(chunk.data(), 1024, 2);
    display_->setVisual(analyzer.analyze(1.0 / std::max(10, visualFps_), 0));

    QElapsedTimer frame;
    frame.start();
    target.fill(Qt::transparent);
    display_->render(&target);
    const double ms = frame.nsecsElapsed() / 1e6;
    if (elapsed >= kWarmup) costs.push_back(ms);
  }
  display_->setVisual(wamp::AudioAnalyzer::Frame{});
  progress.close();

  if (cancelled || costs.empty()) {
    if (dialog != nullptr) dialog->setCalibrationResult(visualFps_, 0.0);
    return;
  }

  std::sort(costs.begin(), costs.end());
  const double p95 = costs[static_cast<std::size_t>(0.95 * (costs.size() - 1))];
  // 40% de folga: o desenho pode ocupar no máximo 60% do orçamento de um quadro.
  const int fps = std::clamp(static_cast<int>(std::floor(600.0 / std::max(0.05, p95))), 10, 60);
  const double cpu = p95 * fps / 10.0;
  if (qEnvironmentVariableIsSet("AMPLIA_CALIBRATE_LOG")) {
    qInfo("calibracao: p95=%.2f ms fps=%d cpu=%.1f%% (%zu amostras)", p95, fps, cpu, costs.size());
  }
  if (dialog != nullptr) dialog->setCalibrationResult(fps, p95);
}

void MainWindow::updateVisual() {
  if (engine_ == nullptr || display_ == nullptr) return;
  if (!visualEnabled_ || !isVisible() || isMinimized()) {
    visualLast_ = std::chrono::steady_clock::now();
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::chrono::duration<double>(now - visualLast_).count();
  visualLast_ = now;
  const wamp::PlaybackState state = engine_->state();

  if (state == wamp::PlaybackState::Playing) {
    const std::int64_t latency = engine_->analyzerLatencyFrames();
    lastVisual_ = engine_->analyzer().analyze(dt, latency);
    haveVisual_ = true;
    display_->setVisual(lastVisual_);
    if (qEnvironmentVariableIsSet("AMPLIA_VISUAL_LOG")) {
      static int tick = 0;
      if (++tick % 20 == 0) {
        qInfo("visual: atraso=%lld frames (%.0f ms)", static_cast<long long>(latency),
              latency * 1000.0 / 48000.0);
      }
    }
    return;
  }

  // Pausa: congela o quadro. A música vai continuar dali a instantes, e um decaimento daria
  // a impressão falsa de que a faixa acabou.
  if (state == wamp::PlaybackState::Paused) {
    display_->setVisual(haveVisual_ ? lastVisual_ : wamp::AudioAnalyzer::Frame{});
    return;
  }

  // Stop (ou fim da faixa): desvanece o quadro em ~0,25 s. Corte seco parece pane; a queda
  // suave é a transição real de um VU analógico.
  if (!haveVisual_) {
    display_->setVisual(wamp::AudioAnalyzer::Frame{});
    return;
  }
  const auto fall = static_cast<float>(std::exp(-std::max(0.0, dt) / 0.25));
  float loudest = 0.0f;
  for (std::size_t i = 0; i < lastVisual_.bands.size(); ++i) {
    lastVisual_.bands[i] *= fall;
    lastVisual_.peaks[i] *= fall;
    loudest = std::max(loudest, lastVisual_.bands[i]);
  }
  lastVisual_.levelL *= fall;
  lastVisual_.levelR *= fall;
  lastVisual_.peakL *= fall;
  lastVisual_.peakR *= fall;
  if (loudest < 0.01f && lastVisual_.levelL < 0.01f && lastVisual_.levelR < 0.01f) {
    haveVisual_ = false;
    display_->setVisual(wamp::AudioAnalyzer::Frame{});
  } else {
    display_->setVisual(lastVisual_);
  }
}

void MainWindow::applyEqualizer() {
  if (!eqDialog_) return;
  engine_->setEqualizerEnabled(eqDialog_->enabled());
  engine_->setEqualizerBands(eqDialog_->bands());
  engine_->setEqualizerPreamp(eqDialog_->preamp());
  // Arrastar um slider dispara muitos valores: grava a INI só quando a mão para.
  if (eqSaveTimer_ != nullptr) eqSaveTimer_->start();
}

void MainWindow::loadEqualizer() {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  replayGainMode_ = std::clamp(settings.value(QStringLiteral("replaygain/mode"), 0).toInt(), 0, 2);
  visualEnabled_ = settings.value(QStringLiteral("visual/enabled"), true).toBool();
  visualFps_ = std::clamp(settings.value(QStringLiteral("visual/fps"), 15).toInt(), 10, 60);
  visualCostP95Ms_ = settings.value(QStringLiteral("visual/fpsCostP95Ms"), 0.0).toDouble();
  visualCpuPercent_ = settings.value(QStringLiteral("visual/fpsCpuPercent"), 0.0).toDouble();
  const bool enabled = settings.value(QStringLiteral("eq/enabled"), false).toBool();
  const double preamp = settings.value(QStringLiteral("eq/preamp"), 0.0).toDouble();
  std::array<double, wamp::Equalizer::kBands> bands{};
  for (int band = 0; band < wamp::Equalizer::kBands; ++band) {
    bands[static_cast<std::size_t>(band)] =
        settings.value(QStringLiteral("eq/band%1").arg(band), 0.0).toDouble();
  }
  engine_->setEqualizerEnabled(enabled);
  engine_->setEqualizerBands(bands);
  engine_->setEqualizerPreamp(preamp);
  applyVisualSettings();
}

void MainWindow::saveEqualizer() {
  if (!eqDialog_) return;
  QSettings settings(settingsFile(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("eq/enabled"), eqDialog_->enabled());
  settings.setValue(QStringLiteral("eq/preamp"), eqDialog_->preamp());
  settings.setValue(QStringLiteral("eq/name"), eqDialog_->presetName());
  const std::array<double, wamp::Equalizer::kBands> bands = eqDialog_->bands();
  for (int band = 0; band < wamp::Equalizer::kBands; ++band) {
    settings.setValue(QStringLiteral("eq/band%1").arg(band), bands[static_cast<std::size_t>(band)]);
  }
  settings.sync();
}

void MainWindow::openPreferences() {
  const wamp::ScrobbleConfig scrobble =
      scrobbler_ ? scrobbler_->config() : wamp::ScrobbleConfig{};
  const bool xwaylandOn =
      QSettings(settingsFile(), QSettings::IniFormat)
          .value(QStringLiteral("window/xwaylandPosition"), false)
          .toBool();
  PreferencesDialog dialog(shuffle_, qRound(shuffleLevel_ * 100), closeToTray_, restartOnPlay_,
                           visualEnabled_, visualFps_, visualCostP95Ms_, visualCpuPercent_,
                           playlistMode_, xwaylandOn, scrobble,
                           wamp::ui::languageFromSettings(), this);
  connect(&dialog, &PreferencesDialog::calibrateRequested, this,
          [this, &dialog] { calibrateVisual(&dialog); });
  if (dialog.exec() != QDialog::Accepted) return;

  // Idioma só vale na próxima abertura: widgets já criados não são retraduzidos.
  {
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString chosen = dialog.language();
    if (chosen != settings.value(QStringLiteral("general/language")).toString()) {
      settings.setValue(QStringLiteral("general/language"), chosen);
      settings.sync();
      QMessageBox box(this);
      box.setIcon(QMessageBox::Question);
      box.setWindowTitle(tr("Idioma alterado"));
      box.setText(tr("O novo idioma é aplicado ao recriar a interface."));
      box.setInformativeText(tr("Reiniciar agora interrompe a reprodução e reabre com a "
                                "playlist atual."));
      auto* now = box.addButton(tr("Reiniciar agora"), QMessageBox::AcceptRole);
      box.addButton(tr("Na próxima"), QMessageBox::RejectRole);
      box.setDefaultButton(now);
      box.exec();
      if (box.clickedButton() == now) {
        saveSettings();
        if (restartHandler_) {
          restartHandler_();
          return;  // a janela atual será destruída; não tocar em membros daqui pra frente
        }
        statusBar()->showMessage(
            tr("Idioma salvo — vale na próxima abertura do Amplia"), 6000);
      }
    }
  }

  if (dialog.playlistMode() != playlistMode_) setPlaylistMode(dialog.playlistMode());

  if (dialog.xwaylandPosition() != xwaylandOn) {
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("window/xwaylandPosition"), dialog.xwaylandPosition());
    settings.sync();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Posição das janelas"));
    box.setText(dialog.xwaylandPosition()
                    ? tr("Guardar posição exige reiniciar com XWayland.")
                    : tr("Voltar ao Wayland nativo exige reiniciar."));
    box.setInformativeText(dialog.xwaylandPosition()
                               ? tr("A playlist e a Biblioteca voltam onde estavam. Reiniciar "
                                    "interrompe a reprodução.")
                               : tr("As janelas passam a abrir onde o compositor decidir. "
                                    "Reiniciar interrompe a reprodução."));
    auto* now = box.addButton(tr("Reiniciar agora"), QMessageBox::AcceptRole);
    box.addButton(tr("Na próxima"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();
    if (box.clickedButton() == now) {
      restartWithPlatformChange();
      return;  // o processo será encerrado
    }
    statusBar()->showMessage(tr("Posição das janelas: vale na próxima abertura"), 6000);
  }

  visualEnabled_ = dialog.visualEnabled();
  visualFps_ = dialog.visualFps();
  visualCostP95Ms_ = dialog.visualCostP95Ms();
  visualCpuPercent_ = dialog.visualCpuPercent();
  applyVisualSettings();
  saveSettings();

  if (scrobbler_) {
    scrobbler_->setConfig(dialog.scrobbleConfig());
    const int pending = scrobbler_->pendingCount();
    if (pending > 0) {
      statusBar()->showMessage(tr("%1 scrobble(s) na fila").arg(pending), 5000);
    }
  }

  closeToTray_ = dialog.closeToTray();
  shuffle_ = dialog.shuffleEnabled();
  shuffleLevel_ = dialog.shuffleLevel() / 100.0;
  playlist_->resetShuffle();
  updateNextUp();
  shuffleButton_->setChecked(shuffle_);
  if (shuffleAction_ != nullptr) {
    QSignalBlocker block(shuffleAction_);
    shuffleAction_->setChecked(shuffle_);
  }
  saveSettings();
  statusBar()->showMessage(
      shuffle_ ? tr("Sorteio ligado (%1%)").arg(dialog.shuffleLevel())
               : tr("Sorteio desligado"),
      3000);
}

void MainWindow::playPrevious() {
  if (playlist_->empty()) return;
  if (engine_->positionMs() > 3000) {
    engine_->seek(0);
    return;
  }
  playIndex(playlist_->previousIndex());
}

void MainWindow::onTrackEnded() {
  if (stopAfterCurrent_) {
    stopAfterCurrent_ = false;
    if (stopAfterAction_ != nullptr) stopAfterAction_->setChecked(false);
    updateNextUp();
    statusBar()->showMessage(tr("Parado no fim da faixa"), 3000);
    return;
  }
  finishScrobble();
  if (playlist_->empty()) return;
  if (repeatMode_ == 2) {
    // Repetir a faixa: recomeça a mesma, mantendo a sessão de scrobble nova.
    const wamp::TrackInfo track = engine_->track();
    if (!track.path.empty()) {
      openPath(QString::fromStdString(track.path));
      return;
    }
  }
  // O engine só avisa fim quando não havia próxima faixa: o gapless já teria
  // emendado a seguinte sem parar o áudio.
  if (!nextUpPath_.empty()) {
    const QString next = QString::fromStdString(nextUpPath_);
    if (nextUpFromQueue_) {
      playlist_->dequeue();
      refreshQueue();
    }
    if (!openPath(next)) {
      // Faixa da fila/nova não abriu (arquivo ausente): segue adiante pela lista.
      playIndex(nextTrackIndex());
    }
    return;
  }
  statusBar()->showMessage(tr("Fim da playlist"), 3000);
}

void MainWindow::toggleQueue(int row) {
  if (row < 0 || static_cast<std::size_t>(row) >= playlist_->size()) return;
  const std::size_t index = static_cast<std::size_t>(row);
  if (playlist_->queuePosition(index) > 0) {
    playlist_->removeFromQueue(index);
    statusBar()->showMessage(tr("Fora da fila"), 2000);
  } else {
    playlist_->enqueue(index);
    statusBar()->showMessage(
        tr("Enfileirada na posição %1").arg(playlist_->queueSize()), 2000);
  }
  refreshQueue();
  updateNextUp();
}

void MainWindow::showListMenu(const QPoint& globalPos) {
  QListWidgetItem* item = list_->currentItem();
  if (item == nullptr) return;
  const int row = list_->row(item);

  QMenu menu(this);
  QAction* play = menu.addAction(QStringLiteral("Reproduzir"));
  const bool queued = playlist_->queuePosition(static_cast<std::size_t>(row)) > 0;
  QAction* queue = menu.addAction(queued ? tr("Tirar da fila")
                                         : tr("Enfileirar (Alt+clique esquerdo)"));
  menu.addSeparator();
  QAction* remove = menu.addAction(tr("Remover da playlist"));

  QAction* chosen = menu.exec(globalPos);
  if (chosen == nullptr) return;
  if (chosen == play) {
    playIndex(row);
  } else if (chosen == queue) {
    toggleQueue(row);
  } else if (chosen == remove) {
    playlist_->removeAt(static_cast<std::size_t>(row));
    playlistDirty_ = true;
    rebuildPlaylist();
    updateNextUp();
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // Arrastar de qualquer ponto: o painel do display e o fundo da janela iniciam o
  // movimento do sistema. Botões, sliders e a lista ficam de fora para não roubar o clique.
  if (easyMove_ && event->type() == QEvent::MouseButtonPress && QApplication::activeModalWidget() == nullptr) {
    auto* press = static_cast<QMouseEvent*>(event);
    const bool onDisplay = (display_ != nullptr && watched == display_);
    const bool onBackground = (watched == centralWidget());
    if (press->button() == Qt::LeftButton && (onDisplay || onBackground)) {
      if (windowHandle() != nullptr) windowHandle()->startSystemMove();
    }
  }

  // Fechar a janela da playlist (X) só esconde; o botão PL acompanha.
  if ((playlistWindow_ != nullptr && watched == playlistWindow_ ||
       (library_ != nullptr && watched == library_.get())) &&
      event->type() == QEvent::Move) {
    scheduleSettingsSave();
  }
  if (playlistWindow_ != nullptr && watched == playlistWindow_ &&
      event->type() == QEvent::Close) {
    playlistButton_->setChecked(false);
    saveSettings();
  }
  if (list_ != nullptr && watched == list_->viewport() &&
      event->type() == QEvent::MouseButtonPress) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    const bool alt = mouse->modifiers() & Qt::AltModifier;
    if (mouse->button() == Qt::LeftButton && alt) {
      QListWidgetItem* item = list_->itemAt(mouse->position().toPoint());
      if (item != nullptr) {
        toggleQueue(list_->row(item));  // Alt+clique esquerdo enfileira
        return true;
      }
    }
    if (mouse->button() == Qt::RightButton) {
      QListWidgetItem* item = list_->itemAt(mouse->position().toPoint());
      if (item != nullptr) {
        list_->setCurrentItem(item);
        showListMenu(mouse->globalPosition().toPoint());
        return true;
      }
    }
  }

  // Arrastar arquivos sobre a lista: a viewport engoliria o drop (o view do Qt aceita
  // drags para arrumar itens), então o alvo é tratado aqui e cai no mesmo caminho da janela.
  if (list_ != nullptr && watched == list_->viewport()) {
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
      auto* drag = static_cast<QDragMoveEvent*>(event);
      if (drag->mimeData()->hasUrls()) {
        drag->acceptProposedAction();
        return true;
      }
    } else if (event->type() == QEvent::Drop) {
      auto* drop = static_cast<QDropEvent*>(event);
      QStringList paths;
      int rejected = 0;
      const QList<QUrl> urls = drop->mimeData()->urls();
      for (const QUrl& url : urls) {
        const QString local = url.toLocalFile();
        if (local.isEmpty()) {
          ++rejected;
        } else {
          paths << local;
        }
      }
      if (!paths.isEmpty()) {
        drop->acceptProposedAction();
        handleDroppedPaths(paths);
        warnAboutNetworkDrop(this, urls, rejected);
        return true;
      }
      if (!urls.isEmpty()) {
        warnAboutNetworkDrop(this, urls, rejected);
        return true;
      }
    }
  }
  // Teclas que pertencem à lista quando ela tem o foco: aceitar o ShortcutOverride faz o
  // Qt entregar o evento ao widget em vez de disparar o atalho da janela (setas = volume,
  // Ctrl+A = sempre no topo). Sem isso, no modo integrado as setas mexiam no volume em vez
  // de rolar a lista.
  if (list_ != nullptr && watched == list_ && event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool ctrl = key->modifiers().testFlag(Qt::ControlModifier);
    switch (key->key()) {
      case Qt::Key_Up:
      case Qt::Key_Down:
      case Qt::Key_PageUp:
      case Qt::Key_PageDown:
      case Qt::Key_Home:
      case Qt::Key_End:
      case Qt::Key_Space:
        key->accept();
        return true;
      case Qt::Key_A:
        if (ctrl) {
          key->accept();
          return true;
        }
        break;
      default:
        break;
    }
  }
  if (list_ != nullptr && watched == list_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      const int row = list_->currentRow();
      if (row >= 0) {
        playIndex(static_cast<std::size_t>(row));
        return true;
      }
    }
    if (key->key() == Qt::Key_Delete) {
      removeSelected();
      return true;
    }
    if (key->key() == Qt::Key_Space) {
      if (playlist_->hasCurrent()) {
        const int row = static_cast<int>(playlist_->currentIndex());
        if (QListWidgetItem* item = list_->item(row)) list_->scrollToItem(item);
      }
      return true;
    }
    if (key->key() == Qt::Key_A && key->modifiers().testFlag(Qt::ControlModifier)) {
      list_->selectAll();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::schedulePendingPlaylistOpen() {
  // O showEvent pode acontecer antes de loadSettings() (a janela é mapeada durante a
  // construção, dependendo do ambiente), então o agendamento é idempotente e chamado dos
  // dois lados. Sem isso, abrir com a playlist ligada não abria a playlist.
  if (!pendingPlaylistVisible_ || playlistShowScheduled_) return;
  playlistShowScheduled_ = true;
  // O atraso existe para o KWin receber o xdg_toplevel.set_parent da janela externa, que
  // só é enviado quando a principal já foi mapeada. Com a playlist integrada não há janela
  // nenhuma para subordinar (e em X11 o pedido de parentesco não depende do mapeamento).
  const int defaultDelay = (playlistIntegrated() || windowPositionSupported()) ? 0 : 200;
  const int delay = qEnvironmentVariableIsSet("AMPLIA_PLAYLIST_DELAY_MS")
                        ? qEnvironmentVariableIntValue("AMPLIA_PLAYLIST_DELAY_MS")
                        : defaultDelay;
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_WINDOWS")) {
    qInfo("janelas: abrindo playlist com atraso de %d ms (modo=%s)", delay,
          qPrintable(playlistMode_));
  }
  QTimer::singleShot(delay, this, [this] {
    pendingPlaylistVisible_ = false;
    setPlaylistVisible(true);
  });
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  schedulePendingPlaylistOpen();
}

void MainWindow::restartWithPlatformChange() {
  saveSettings();
  // O processo atual já rodou com a plataforma antiga: só um processo novo relê a INI em
  // main() e escolhe o backend. Limpa a variável que o próprio app pôs, senão o filho
  // herda xcb e a preferência desligada não teria efeito.
  qunsetenv("QT_QPA_PLATFORM");
  const QString program = QCoreApplication::applicationFilePath();
  QProcess::startDetached(QStringLiteral("/bin/sh"),
                          {QStringLiteral("-c"), QStringLiteral("sleep 1; exec \"$0\""), program});
  QCoreApplication::quit();
}

void MainWindow::scheduleSettingsSave() {
  if (!settingsLoaded_) return;
  if (saveStateTimer_ == nullptr) {
    saveStateTimer_ = new QTimer(this);
    saveStateTimer_->setSingleShot(true);
    saveStateTimer_->setInterval(700);
    connect(saveStateTimer_, &QTimer::timeout, this, [this] { saveSettings(); });
  }
  saveStateTimer_->start();
}

void MainWindow::moveEvent(QMoveEvent* event) {
  QMainWindow::moveEvent(event);
  // Só faz sentido quando o backend deixa posicionar (XWayland/X11).
  if (windowPositionSupported()) scheduleSettingsSave();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  // Sem a playlist não existe quem absorva altura: esticar a janela deixaria um vão morto
  // embaixo do transporte. A altura é a do bloco compacto (o mesmo critério do Winamp,
  // cuja janela principal não é redimensionável).
  if (!playlistOpen_ && playlistIntegrated() && std::abs(height() - compactHeight_) > 1) {
    resize(width(), compactHeight_);
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // Fechar escondendo para a bandeja não interrompe nada — a indexação segue lá.
  const bool hidingToTray =
      closeToTray_ && !quitting_ && tray_ != nullptr && tray_->isVisible();
  if (!hidingToTray && scanner_ != nullptr && scanner_->pending() > 0) {
    qInfo("amplia: close com %zu faixa(s) na fila — perguntando", scanner_->pending());

    // Diálogo próprio em vez de QMessageBox: ele fixa o layout no tamanho do texto e
    // corta rótulos longos como "Fechar mesmo assim" (o mínimo é ignorado no showEvent).
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Ainda lendo metadados"));
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLabel(tr("Ainda faltam %1 de %2 faixas.")
                                 .arg(static_cast<qulonglong>(scanner_->pending()))
                                 .arg(static_cast<qulonglong>(metadataTotal_)),
                             &dialog);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    auto* hint = new QLabel(tr("A indexação continua na próxima vez que o Amplia abrir — "
                               "os pendentes já estão guardados no banco."),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(hint);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* keep = new QPushButton(tr("Continuar lendo"), &dialog);
    auto* leave = new QPushButton(tr("Fechar mesmo assim"), &dialog);
    keep->setDefault(true);
    buttons->addWidget(keep);
    buttons->addWidget(leave);
    layout->addLayout(buttons);

    connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(leave, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (qEnvironmentVariableIsSet("AMPLIA_GRAB_GUARD")) {
      // Depuração: fotografa o próprio diálogo e responde "continuar" (a sessão de teste
      // não tem como clicar).
      dialog.show();
      QCoreApplication::processEvents();
      dialog.grab().save(qEnvironmentVariable("AMPLIA_GRAB_GUARD"));
      dialog.hide();
      event->ignore();
      return;
    }
    if (dialog.exec() != QDialog::Accepted) {
      event->ignore();
      return;
    }
  }

  closing_ = true;
  // Anota antes de fechar: saveSettings() roda depois e a auxiliar já estaria invisível.
  playlistOpenAtExit_ = playlistOpen_;
  if (playlistWindow_ != nullptr && !playlistIntegrated()) playlistWindow_->close();
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_CLOSE")) {
    qInfo("close: closeToTray=%d quitting=%d tray=%d visivel=%d", closeToTray_ ? 1 : 0,
          quitting_ ? 1 : 0, tray_ != nullptr ? 1 : 0, isVisible() ? 1 : 0);
  }
  // Fechar com o agente ligado só esconde a janela: a música continua e o ícone
  // da bandeja é o caminho de volta. "Sair" no menu da bandeja desliga `quitting_`.
  if (closeToTray_ && !quitting_ && tray_ != nullptr && tray_->isVisible()) {
    // Esconder não é fechar, mas é a hora em que o usuário "terminou" a janela: grava o
    // estado agora. Sem isso, quem usa a bandeja nunca salvava a posição/tamanho das
    // janelas (só o "Sair" salvava) e a posição parecia não funcionar.
    saveSettings();
    hide();
    event->ignore();
    closing_ = false;
    if (!trayNoticeShown_) {
      trayNoticeShown_ = true;
      tray_->showMessage(QStringLiteral("Amplia"),
                         tr("Continuo tocando aqui. Clique no ícone para voltar."),
                         QSystemTrayIcon::Information, 4000);
    }
    return;
  }
  if (cache_) cache_->flush();
  saveSettings();

  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_CLOSE")) qInfo("close: indo para o base");
  QMainWindow::closeEvent(event);
  // Com o ícone da bandeja no ar, o Qt deixa de encerrar sozinho ao fechar a última
  // janela (o item SNI segura o processo). Sair é explícito: agenda o encerramento
  // para depois do evento de fechamento.
  if (event->isAccepted() && (quitting_ || !closeToTray_)) {
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
  }
}

void MainWindow::loadSettings() {
  // A INI pode guardar token do ListenBrainz e session key do Last.fm: garante 600 mesmo
  // para arquivos criados por versões anteriores.
  if (QFile::exists(settingsFile())) {
    QFile::setPermissions(settingsFile(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
  QSettings settings(settingsFile(), QSettings::IniFormat);
  shuffle_ = settings.value(QStringLiteral("play/shuffle"), false).toBool();
  restartOnPlay_ = settings.value(QStringLiteral("play/restartOnPlay"), true).toBool();
  restartOnPlay_ = settings.value(QStringLiteral("play/restartOnPlay"), true).toBool();
  repeatMode_ = std::clamp(settings.value(QStringLiteral("play/repeatMode"),
                                          settings.value(QStringLiteral("play/repeat"), false).toBool() ? 1 : 0)
                               .toInt(),
                           0, 2);
  closeToTray_ = settings.value(QStringLiteral("window/closeToTray"), false).toBool();
  showPath_ = settings.value(QStringLiteral("playlist/showPath"), false).toBool();
  if (showPathAction_ != nullptr) {
    QSignalBlocker block(showPathAction_);
    showPathAction_->setChecked(showPath_);
  }
  shuffleLevel_ = std::clamp(settings.value(QStringLiteral("play/shuffleLevel"), 50).toInt(), 0, 100) / 100.0;
  const int volume = std::clamp(settings.value(QStringLiteral("play/volume"), 80).toInt(), 0, 100);
  remaining_ = settings.value(QStringLiteral("play/remaining"), false).toBool();
  setPlaylistFontSize(settings.value(QStringLiteral("playlist/fontPx"), 11).toInt());

  alwaysOnTop_ = settings.value(QStringLiteral("window/alwaysOnTop"), false).toBool();
  if (alwaysOnTop_) {
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    if (alwaysOnTopAction_ != nullptr) alwaysOnTopAction_->setChecked(true);
  }
  easyMove_ = settings.value(QStringLiteral("window/easyMove"), false).toBool();
  if (easyMoveAction_ != nullptr) easyMoveAction_->setChecked(easyMove_);
  if (display_ != nullptr) display_->setShowRemaining(remaining_);

  shuffleButton_->setChecked(shuffle_);
  if (shuffleAction_ != nullptr) {
    QSignalBlocker block(shuffleAction_);
    shuffleAction_->setChecked(shuffle_);
  }
  if (repeatAction_ != nullptr && repeatAction_->isChecked() != repeatActive()) {
    QSignalBlocker block(repeatAction_);
    repeatAction_->setChecked(repeatActive());
  }
  volumeSlider_->setValue(volume);
  engine_->setVolume(static_cast<float>(volume) / 100.0f);

  // Tamanho: o cliente escolhe (o compositor respeita); a posição é dele no Wayland.
  // Guardamos o tamanho TOTAL e a altura do painel e derivamos a altura compacta — assim
  // abrir/fechar a lista não inventa um vão nem encolhe a janela.
  const QSize savedSize = settings.value(QStringLiteral("window/size")).toSize();
  const int savedCompact = settings.value(QStringLiteral("window/compactHeight"), 0).toInt();
  int width = std::clamp(settings.value(QStringLiteral("window/width"), 460).toInt(), 400, 4000);
  // A altura compacta é a altura natural do bloco display+transporte: aceitar um valor
  // maior reabriria a janela com um vão em branco (aconteceu: 1008 salvo depois de uma
  // sessão que esticou a janela com a playlist aberta).
  const int naturalCompact = std::max(kCompactFloor, sizeHint().height());
  compactHeight_ = std::clamp(
      std::max(settings.value(QStringLiteral("window/collapsedHeight"), compactHeight_).toInt(),
               compactHeight_),
      kCompactFloor, naturalCompact + 8);
  if (savedCompact >= kCompactFloor) {
    // Caminho normal: a altura compacta é guardada explicitamente. Derivar do tamanho total
    // a cada abertura fazia o valor derivar com o tempo (painel e janela se realimentavam).
    compactHeight_ = std::clamp(savedCompact, kCompactFloor, naturalCompact + 8);
    if (savedSize.isValid() && savedSize.width() >= 400) {
      width = std::clamp(savedSize.width(), 400, 4000);
    }
  } else if (savedSize.isValid() && savedSize.width() >= 400 && savedSize.height() >= kCompactFloor) {
    // Versões anteriores: deriva do total e da altura do painel.
    width = std::clamp(savedSize.width(), 400, 4000);
    const bool panelOpen = settings.value(QStringLiteral("playlist/visible"), false).toBool();
    const int panelHeight = settings.value(QStringLiteral("window/panelHeight"), 300).toInt();
    compactHeight_ = std::clamp(savedSize.height() - (panelOpen ? std::max(160, panelHeight) : 0),
                                kCompactFloor, naturalCompact + 8);
  }
  resize(width, compactHeight_);

  wamp::ScrobbleConfig scrobble;
  scrobble.lastfmEnabled = settings.value(QStringLiteral("scrobble/lastfmEnabled"), false).toBool();
  scrobble.lastfmApiKey = settings.value(QStringLiteral("scrobble/lastfmApiKey")).toString().toStdString();
  scrobble.lastfmSharedSecret =
      settings.value(QStringLiteral("scrobble/lastfmSecret")).toString().toStdString();
  scrobble.lastfmSessionKey =
      settings.value(QStringLiteral("scrobble/lastfmSession")).toString().toStdString();
  scrobble.listenbrainzEnabled =
      settings.value(QStringLiteral("scrobble/listenbrainzEnabled"), false).toBool();
  scrobble.listenbrainzToken =
      settings.value(QStringLiteral("scrobble/listenbrainzToken")).toString().toStdString();
  scrobble.announceNowPlaying =
      settings.value(QStringLiteral("scrobble/nowPlaying"), false).toBool();
  if (scrobbler_) scrobbler_->setConfig(scrobble);

  // Posições só fazem sentido onde a plataforma permite (ver windowPositionSupported).
  const QByteArray savedGeometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
  const bool hasSavedGeometry = windowPositionSupported() && !savedGeometry.isEmpty();
  if (hasSavedGeometry && restoreGeometry(savedGeometry)) {
    if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_GEOM")) {
      qInfo("restore: geometria salva aplicada em %d,%d %dx%d", this->x(), this->y(),
            this->width(), this->height());
    }
    // Largura e altura vieram junto com a posição.
    width = this->width();
    compactHeight_ = std::clamp(height(), kCompactFloor, 1200);
  }

  playlistMode_ = settings.value(QStringLiteral("playlist/mode"), QStringLiteral("integrada"))
                      .toString();
  // A chave é window/panelHeight (é o que saveSettings grava); integratedHeight é o nome
  // antigo e fica como leitura de migração.
  // Altura da lista quando o usuário estica a janela com ela aberta: lembrada entre
  // sessões, com piso no tamanho padrão (menos que isso não mostra a lista).
  integratedHeight_ =
      std::clamp(settings.value(QStringLiteral("window/panelHeight"), kPlaylistHeight).toInt(),
                 kPlaylistHeight, 900);
  applyPlaylistMode();

  const int playlistW = settings.value(QStringLiteral("window/playlistWidth"), 440).toInt();
  const int playlistH = settings.value(QStringLiteral("window/playlistHeight"), 400).toInt();
  if (playlistWindow_ != nullptr) {
    const QByteArray savedPlaylistGeometry =
        settings.value(QStringLiteral("window/playlistGeometry")).toByteArray();
    if (windowPositionSupported() && !savedPlaylistGeometry.isEmpty()) {
      playlistWindow_->restoreGeometry(savedPlaylistGeometry);
    } else {
      playlistWindow_->resize(playlistW, playlistH);
    }
  }
  if (settings.value(QStringLiteral("playlist/visible"), false).toBool()) {
    // Não abre aqui: o construtor roda antes de a janela principal ser mapeada, e no
    // Wayland o parentesco não seria enviado. showEvent() agenda a abertura.
    // Sincroniza o botão sem emitir toggled: o sinal chamaria setPlaylistVisible()
    // na hora e derrubaria o agendamento feito em showEvent().
    {
      const QSignalBlocker blocker(playlistButton_);
      playlistButton_->setChecked(true);
    }
    pendingPlaylistVisible_ = true;
  }

  // Sessão anterior: devolve a lista e a faixa atual, sem tocar (D-029). Arquivo passado
  // na linha de comando tem prioridade — nesse caso o construtor vem com restoreSession
  // desligado.
  if (restoreSession_) {
    const QStringList items = settings.value(QStringLiteral("playlist/items")).toStringList();
    if (!items.isEmpty()) {
      std::vector<std::string> paths;
      paths.reserve(static_cast<std::size_t>(items.size()));
      for (const QString& item : items) paths.push_back(item.toStdString());
      // Abertura instantânea: a lista vem da INI e as tags do cache, **sem** stat nem
      // sondagem. Tag só é atualizada ao tocar ou ao salvar no editor (D-082).
      for (const std::string& path : paths) {
        wamp::TrackInfo cached;
        cachedInfoFast(path, &cached);
      }
      playlist_->addAll(paths);
      rebuildPlaylist();

      const int current = settings.value(QStringLiteral("playlist/current"), -1).toInt();
      if (current >= 0 && current < static_cast<int>(playlist_->size())) {
        playlist_->setCurrent(static_cast<std::size_t>(current));
        openPath(QString::fromStdString(playlist_->at(static_cast<std::size_t>(current))),
                 false);
        list_->setCurrentRow(current);
        statusBar()->showMessage(
            tr("Sessão anterior: %1 faixa(s) — Play para continuar")
                .arg(playlist_->size()),
            8000);
      }
    }
  }
  settingsLoaded_ = true;
}

void MainWindow::saveSettings() {
  // Antes do loadSettings o estado ainda é o de fábrica: salvar aqui apagaria a sessão
  // anterior (o construtor monta a interface e mexe no painel da lista).
  if (!settingsLoaded_) return;
  QSettings settings(settingsFile(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("play/shuffle"), shuffle_);
  settings.setValue(QStringLiteral("play/shuffleLevel"), qRound(shuffleLevel_ * 100));
  settings.setValue(QStringLiteral("play/repeatMode"), repeatMode_);
  settings.setValue(QStringLiteral("play/restartOnPlay"), restartOnPlay_);
  if (volumeSlider_ != nullptr) settings.setValue(QStringLiteral("play/volume"), volumeSlider_->value());
  settings.setValue(QStringLiteral("play/remaining"), remaining_);
  settings.setValue(QStringLiteral("playlist/fontPx"), playlistFontPx_);
  settings.setValue(QStringLiteral("window/alwaysOnTop"),
                    alwaysOnTopAction_ != nullptr ? alwaysOnTopAction_->isChecked() : alwaysOnTop_);
  settings.setValue(QStringLiteral("window/easyMove"), easyMove_);
  if (playlistWindow_ != nullptr && playlistWindow_->isVisible()) {
    settings.setValue(QStringLiteral("window/playlistWidth"), playlistWindow_->width());
    settings.setValue(QStringLiteral("window/playlistHeight"), playlistWindow_->height());
  }
  const bool playlistOpen = closing_ ? playlistOpenAtExit_ : playlistOpen_;
  // Maximizar não é "o tamanho que o usuário escolheu": guardar isso reabriria a janela
  // gigante sem a playlist.
  if (!playlistOpen && !isMaximized() && !isFullScreen()) {
    compactHeight_ = std::max(minimumHeight(), height());
  }
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_WINDOWS")) {
    qInfo("saveSettings: tamanho=%dx%d", width(), height());
  }
  if (!isMaximized() && !isFullScreen()) {
    // Com o painel aberto, a diferença para a altura compacta é a altura real do painel.
    if (playlistIntegrated() && playlistOpen_ && height() > compactHeight_) {
      integratedHeight_ = std::clamp(height() - compactHeight_, 160, 900);
    }
    // Tamanho vale em qualquer plataforma; só a posição depende do X11.
    settings.setValue(QStringLiteral("window/compactHeight"), compactHeight_);
    settings.setValue(QStringLiteral("window/size"), size());
    settings.setValue(QStringLiteral("window/panelHeight"), integratedHeight_);
  }
  if (windowPositionSupported() && !isMaximized() && !isFullScreen()) {
    if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_GEOM")) {
      qInfo("save: gravando posição %d,%d %dx%d", this->x(), this->y(), this->width(),
            this->height());
    }
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  }
  if (windowPositionSupported() && playlistWindow_ != nullptr && playlistOpen) {
    settings.setValue(QStringLiteral("window/playlistGeometry"), playlistWindow_->saveGeometry());
  }
  if (windowPositionSupported() && library_ != nullptr && library_->isVisible()) {
    settings.setValue(QStringLiteral("window/libraryGeometry"), library_->saveGeometry());
  }
  settings.setValue(QStringLiteral("window/width"), width());
  settings.setValue(QStringLiteral("window/collapsedHeight"), compactHeight_);
  settings.setValue(QStringLiteral("window/closeToTray"), closeToTray_);
  settings.setValue(QStringLiteral("playlist/visible"), playlistOpen);
  settings.setValue(QStringLiteral("playlist/mode"), playlistMode_);
  settings.setValue(QStringLiteral("window/integratedHeight"), integratedHeight_);
  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_CLOSE")) {
    qInfo("saveSettings: playlistOpen=%d", playlistOpen ? 1 : 0);
  }
  settings.setValue(QStringLiteral("playlist/showPath"), showPath_);
  // Sessão: a próxima abertura devolve a lista e a faixa atual (carregada em pausa).
  // Mas quando o app foi aberto com arquivos na linha de comando, a lista em memória começa
  // do zero: gravar isso apagaria a sessão anterior. Só grava se ela foi restaurada ou se o
  // usuário mexeu na lista durante esta execução.
  const bool savePlaylist = sessionRestored_ || playlistDirty_;
  if (savePlaylist && playlist_ != nullptr && !playlist_->empty()) {
    QStringList items;
    items.reserve(static_cast<int>(playlist_->size()));
    for (const std::string& path : playlist_->items()) items << QString::fromStdString(path);
    settings.setValue(QStringLiteral("playlist/items"), items);
    const int current = playlist_->hasCurrent()
                            ? static_cast<int>(playlist_->currentIndex())
                            : -1;
    settings.setValue(QStringLiteral("playlist/current"), current);
  } else if (savePlaylist) {
    settings.remove(QStringLiteral("playlist/items"));
    settings.remove(QStringLiteral("playlist/current"));
  }
  if (scrobbler_) {
    const wamp::ScrobbleConfig scrobble = scrobbler_->config();
    settings.setValue(QStringLiteral("scrobble/lastfmEnabled"), scrobble.lastfmEnabled);
    settings.setValue(QStringLiteral("scrobble/lastfmApiKey"),
                      QString::fromStdString(scrobble.lastfmApiKey));
    settings.setValue(QStringLiteral("scrobble/lastfmSecret"),
                      QString::fromStdString(scrobble.lastfmSharedSecret));
    settings.setValue(QStringLiteral("scrobble/lastfmSession"),
                      QString::fromStdString(scrobble.lastfmSessionKey));
    settings.setValue(QStringLiteral("scrobble/listenbrainzEnabled"), scrobble.listenbrainzEnabled);
    settings.setValue(QStringLiteral("scrobble/listenbrainzToken"),
                      QString::fromStdString(scrobble.listenbrainzToken));
    settings.setValue(QStringLiteral("scrobble/nowPlaying"), scrobble.announceNowPlaying);
  }

  // A INI guarda a session key do Last.fm e o token do ListenBrainz: só o dono lê.
  settings.sync();
  QFile::setPermissions(settingsFile(),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

namespace {

wamp::ScrobbleEvent makeScrobbleEvent(const wamp::TrackInfo& track, std::int64_t startedAt,
                                      std::int64_t playedMs) {
  wamp::ScrobbleEvent event;
  event.artist = track.artist;
  event.title = track.title;
  event.album = track.album;
  event.albumArtist = track.albumArtist;
  event.path = track.path;
  event.durationMs = track.durationMs;
  event.startedAt = startedAt;
  event.playedMs = playedMs;
  return event;
}

}  // namespace

void MainWindow::beginScrobble(const wamp::TrackInfo& track) {
  finishScrobble();  // fecha a sessão anterior, se houver
  scrobbleTrack_ = track;
  scrobbleStartUnix_ = QDateTime::currentSecsSinceEpoch();
  scrobblePlayedMs_ = 0;
  scrobbleLastPos_ = 0;
  scrobbleSent_ = false;
  scrobbleAnnounced_ = false;
}

void MainWindow::finishScrobble() {
  if (!scrobbler_ || scrobbleTrack_.path.empty() || scrobbleSent_) return;
  const wamp::ScrobbleEvent event =
      makeScrobbleEvent(scrobbleTrack_, scrobbleStartUnix_, scrobblePlayedMs_);
  if (wamp::qualifiesForScrobble(event.durationMs, event.playedMs)) {
    scrobbler_->scrobble(event);
    scrobbleSent_ = true;
  }
}

void MainWindow::onPlaybackTick() {
  if (!scrobbler_) return;
  if (engine_->state() != wamp::PlaybackState::Playing) return;

  // Faixa carregada em pausa (adicionada à playlist) só conta quando começa a tocar.
  if (scrobbleStartUnix_ == 0) {
    const wamp::TrackInfo track = engine_->track();
    if (track.path.empty()) return;
    scrobbleTrack_ = track;
    scrobbleStartUnix_ = QDateTime::currentSecsSinceEpoch();
    scrobblePlayedMs_ = 0;
    scrobbleLastPos_ = engine_->positionMs();
    scrobbleSent_ = false;
    return;
  }
  if (scrobbleTrack_.path.empty()) return;

  const std::int64_t position = engine_->positionMs();
  if (engine_->state() == wamp::PlaybackState::Playing) {
    const std::int64_t delta = position - scrobbleLastPos_;
    // Um salto grande é seek, não reprodução: só o avanço natural conta como tempo tocado.
    if (delta > 0 && delta < 5000) scrobblePlayedMs_ += delta;
  }
  scrobbleLastPos_ = position;

  // "Ouvindo agora" é opção (Preferências ▸ Scrobbling, desligada por padrão): o Last.fm
  // não tem chamada para encerrar o anúncio, então ele fica pendurado no perfil se o
  // player parar. Anunciar só depois de alguns segundos evita anúncio de faixa que nem tocou.
  if (!scrobbleAnnounced_ && scrobbler_->config().announceNowPlaying &&
      scrobblePlayedMs_ >= kNowPlayingDelayMs) {
    scrobbler_->nowPlaying(makeScrobbleEvent(scrobbleTrack_, scrobbleStartUnix_, scrobblePlayedMs_));
    scrobbleAnnounced_ = true;
  }

  if (!scrobbleSent_ && wamp::qualifiesForScrobble(scrobbleTrack_.durationMs, scrobblePlayedMs_)) {
    wamp::ScrobbleEvent event =
        makeScrobbleEvent(scrobbleTrack_, scrobbleStartUnix_, scrobblePlayedMs_);
    scrobbler_->scrobble(event);
    scrobbleSent_ = true;
  }
}

void MainWindow::saveSettingsNow() { saveSettings(); }

void MainWindow::restartForLanguage() {
  qInfo("amplia: recriando a interface (idioma=%s)", qPrintable(wamp::ui::languageFromSettings()));
  saveSettings();
  if (restartHandler_) restartHandler_();
}

void MainWindow::setRestartHandler(std::function<void()> handler) {
  restartHandler_ = std::move(handler);
}

void MainWindow::showPlaylist(bool visible) {
  playlistAutoShown_ = true;  // escolha explícita: o auto-mostrar não mexe mais
  setPlaylistVisible(visible);
}

void MainWindow::queueSelected() {
  const QList<QListWidgetItem*> selected = list_->selectedItems();
  if (selected.isEmpty()) return;
  std::vector<int> rows;
  rows.reserve(static_cast<std::size_t>(selected.size()));
  for (QListWidgetItem* item : selected) rows.push_back(list_->row(item));
  std::sort(rows.begin(), rows.end());
  for (int row : rows) playlist_->enqueue(static_cast<std::size_t>(row));
  refreshQueue();
  updateNextUp();
  statusBar()->showMessage(tr("%1 faixas na fila").arg(playlist_->queueSize()), 3000);
}

void MainWindow::removeSelected() {
  const QList<QListWidgetItem*> selected = list_->selectedItems();
  if (selected.isEmpty()) return;
  std::vector<std::size_t> rows;
  rows.reserve(static_cast<std::size_t>(selected.size()));
  for (QListWidgetItem* item : selected) rows.push_back(static_cast<std::size_t>(list_->row(item)));
  std::sort(rows.rbegin(), rows.rend());
  for (std::size_t row : rows) playlist_->removeAt(row);
  playlistDirty_ = true;
  rebuildPlaylist();
  updateNextUp();
}

void MainWindow::rebuildPlaylist() {
  if (list_ == nullptr) return;
  const bool hasCurrent = playlist_->hasCurrent();
  const std::size_t current = hasCurrent ? playlist_->currentIndex() : wamp::Playlist::kNone;
  list_->clear();
  for (std::size_t i = 0; i < playlist_->size(); ++i) {
    const std::string& path = playlist_->at(i);
    auto* item = new QListWidgetItem(rowLabel(path, i == current), list_);
    item->setData(kMissingRole, missingFiles_.count(path) != 0);
    item->setData(Qt::UserRole, QString::fromStdString(path));
    item->setData(kDurationRole, durationText(path));
    item->setToolTip(QString::fromStdString(path));
  }
  if (playlist_->hasCurrent()) {
    list_->setCurrentRow(static_cast<int>(playlist_->currentIndex()));
  }
  delegate_->setTrackCount(static_cast<int>(playlist_->size()));
  refreshQueue();
  playlistButton_->setToolTip(tr("Playlist (%1 faixas)").arg(playlist_->size()));
}

void MainWindow::refreshQueue() {
  if (list_ == nullptr) return;
  for (int row = 0; row < list_->count(); ++row) {
    QListWidgetItem* item = list_->item(row);
    const std::size_t position = playlist_->queuePosition(static_cast<std::size_t>(row));
    item->setData(kQueueRole, static_cast<int>(position));
    QString tip = QString::fromStdString(playlist_->at(static_cast<std::size_t>(row)));
    if (position > 0) tip += tr("\nFila: posição %1").arg(position);
    item->setToolTip(tip);
  }
  list_->viewport()->update();
}

QString MainWindow::rowLabel(const std::string& path, bool current) const {
  QString text;
  if (showPath_) {
    // Caminho completo: é o que se quer quando duas pastas têm o mesmo "01 - Intro".
    text = QString::fromStdString(path);
  } else {
    const auto it = metadata_.find(path);
    if (it != metadata_.end() && (!it->second.artist.empty() || !it->second.title.empty())) {
      text = QString::fromStdString(it->second.displayTitle());
    } else {
      text = baseName(path);
    }
  }
  if (current) text.prepend(tr("\u25b6 "));
  if (missingFiles_.count(path) != 0) text.prepend(QStringLiteral("\u26a0 "));
  return text;
}

QString MainWindow::durationText(const std::string& path) const {
  const auto it = metadata_.find(path);
  if (it == metadata_.end() || it->second.durationMs <= 0) return {};
  return formatTime(it->second.durationMs);
}

void MainWindow::updateMetadataStatus() {
  if (queueLabel_ == nullptr || scanner_ == nullptr) return;
  const std::size_t left = scanner_->pending();
  if (left == 0) {
    metadataTotal_ = 0;
    queueLabel_->hide();
    if (library_) library_->setStatus(QString());
    return;
  }
  // total acumulado das filas somadas; se vier uma fila nova sem a anterior zerar,
  // somamos no enfileiramento (abaixo) em vez de adivinhar aqui.
  if (metadataTotal_ < left) metadataTotal_ = left;
  const std::size_t done = metadataTotal_ - left;
  // Texto curto: a barra é estreita e o rótulo da versão ocupa a direita. O aviso
  // completo fica no tooltip, para não competir por espaço.
  const QString text =
      tr("Lendo metadados… %1/%2 — não feche").arg(done).arg(metadataTotal_);
  // Elide pelo espaço real: sem isso o texto escapa da geometria do rótulo e deixa
  // fragmentos na barra (visto ao lado da versão).
  const int available = queueLabel_->width() > 40 ? queueLabel_->width() - 8 : 320;
  queueLabel_->setText(queueLabel_->fontMetrics().elidedText(text, Qt::ElideRight, available));
  queueLabel_->setToolTip(tr("Lendo as tags de %1 de %2 faixas. Não feche o Amplia durante "
                             "o processamento — a lista ficaria sem metadados.")
                              .arg(done)
                              .arg(metadataTotal_));
  // Sem mensagem passageira por baixo: os rótulos têm fundo transparente e o texto da
  // mensagem aparecia nos vãos, colado na versão.
  statusBar()->clearMessage();
  queueLabel_->show();
  // A Biblioteca não tem fila própria: mostra o mesmo andamento, para o usuário não
  // fechar o app no meio da indexação (o banco ficaria com metade das tags).
  if (library_) library_->setStatus(queueLabel_->text());
}

void MainWindow::openLibrary() {
  if (!library_) {
    library_ = std::make_unique<wamp::ui::LibraryWindow>(this);
    connect(library_.get(), &wamp::ui::LibraryWindow::addFolderRequested, this,
            &MainWindow::addLibraryFolder);
    connect(library_.get(), &wamp::ui::LibraryWindow::refreshRequested, this,
            &MainWindow::rescanLibrary);
    connect(library_.get(), &wamp::ui::LibraryWindow::playRequested, this,
            &MainWindow::playFromLibrary);
    // Trocou de visão ou digitou na busca: refaz a consulta (barato, é só banco).
    connect(library_.get(), &wamp::ui::LibraryWindow::queryChanged, this,
            &MainWindow::refreshLibrary);
    connect(library_.get(), &wamp::ui::LibraryWindow::playSelectionRequested, this,
            [this](const QStringList& paths) {
              if (paths.isEmpty()) return;
              addPaths(paths);
              playIndex(list_->count() - 1);
            });
    connect(library_.get(), &wamp::ui::LibraryWindow::ratingRequested, this,
            [this](const QString& path, int rating) {
              if (cache_ == nullptr) return;
              cache_->setRating(path.toStdString(), rating);
              cache_->flush();
              refreshLibrary();
            });
    connect(library_.get(), &wamp::ui::LibraryWindow::enqueueSelectionRequested, this,
            [this](const QStringList& paths) {
              if (paths.isEmpty()) return;
              addPaths(paths);
              statusBar()->showMessage(
                  tr("%1 faixa(s) enfileirada(s)").arg(paths.size()), 2500);
            });
  }
  if (windowPositionSupported()) {
    const QByteArray saved =
        QSettings(settingsFile(), QSettings::IniFormat)
            .value(QStringLiteral("window/libraryGeometry"))
            .toByteArray();
    if (!saved.isEmpty()) library_->restoreGeometry(saved);
  }
  if (qEnvironmentVariableIsSet("AMPLIA_LIBRARY_VIEW")) {
    library_->selectView(qEnvironmentVariableIntValue("AMPLIA_LIBRARY_VIEW"));
  }
  library_->installEventFilter(this);  // para salvar a posição quando ela se mover
  refreshLibrary();
  library_->show();
  library_->raise();
  library_->activateWindow();
}

QString MainWindow::librarySummary(const std::vector<wamp::LibraryEntry>& rows) const {
  qint64 totalMs = 0;
  qint64 totalBytes = 0;
  for (const wamp::LibraryEntry& entry : rows) {
    totalMs += entry.info.durationMs;
    totalBytes += entry.info.fileSize;
  }
  const qint64 seconds = totalMs / 1000;
  const QString time = QStringLiteral("%1h %2min").arg(seconds / 3600).arg((seconds % 3600) / 60);
  const double megabytes = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
  // Mesmo espírito do rodapé do ML: quantidade, tempo total e tamanho em disco.
  return tr("%1 faixa(s) — %2 — %3 MB")
      .arg(rows.size())
      .arg(time)
      .arg(QString::number(megabytes, 'f', megabytes >= 1024 ? 0 : 1));
}

void MainWindow::refreshLibrary() {
  if (!library_ || !cache_) return;

  // Rápido de propósito: só lê o banco. Varrer pastas de rede leva segundos, então isso
  // fica no rescanLibrary(), que o usuário dispara (ou que roda ao adicionar uma pasta).
  std::string error;
  libraryRoots_ = cache_->roots(&error);
  if (libraryRoots_.empty()) {
    library_->setRows({});
    library_->setSummary(tr("Nenhuma pasta na biblioteca. Use “Adicionar pasta…”."));
    updateMetadataStatus();
    return;
  }

  std::vector<wamp::LibraryEntry> rows =
      cache_->queryLibrary(library_->view(), library_->filter().toStdString(), 3, &error);

  // A consulta é sobre o banco inteiro; a Biblioteca mostra só o que está sob as raízes.
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [this](const wamp::LibraryEntry& entry) {
                              const std::string& path = entry.info.path;
                              return !std::any_of(libraryRoots_.begin(), libraryRoots_.end(),
                                                  [&path](const std::string& root) {
                                                    return path.compare(0, root.size(), root) == 0;
                                                  });
                            }),
             rows.end());

  library_->setRows(rows);
  library_->setSummary(librarySummary(rows));
  updateMetadataStatus();
}

void MainWindow::rescanLibrary() {
  if (!library_ || !cache_ || scanner_ == nullptr) return;

  statusBar()->showMessage(tr("Procurando arquivos nas pastas da biblioteca…"));
  QApplication::setOverrideCursor(Qt::WaitCursor);

  std::string error;
  std::vector<std::string> missing;
  for (const auto& root : cache_->roots(&error)) {
    int folders = 0;
    int ignored = 0;
    const QStringList files = expandAudioPaths({QString::fromStdString(root)}, &folders, &ignored);
    for (const QString& file : files) {
      const std::string path = file.toStdString();
      wamp::TrackInfo info;
      if (!cache_->lookupFast(path, &info)) missing.push_back(path);
    }
  }

  QApplication::restoreOverrideCursor();
  statusBar()->clearMessage();

  if (!missing.empty()) {
    // Grava a fila antes de começar: se o app morrer no meio, a próxima execução retoma.
    cache_->queueScan(missing);
    metadataTotal_ += missing.size();
    scanner_->enqueueAll(missing);
  }
  refreshLibrary();
}

void MainWindow::addLibraryFolder() {
  QWidget* const dialogParent = library_ ? static_cast<QWidget*>(library_.get()) : this;
  const QString dir = QFileDialog::getExistingDirectory(
      dialogParent, tr("Pasta para adicionar à biblioteca"));
  if (dir.isEmpty() || !cache_) return;

  std::string error;
  if (!cache_->addRoot(dir.toStdString(), &error)) {
    statusBar()->showMessage(tr("Não foi possível guardar a pasta: %1")
                                 .arg(QString::fromStdString(error)),
                             6000);
    return;
  }

  int folders = 0;
  int ignored = 0;
  const QStringList files = expandAudioPaths({dir}, &folders, &ignored);
  std::vector<std::string> paths;
  paths.reserve(static_cast<std::size_t>(files.size()));
  for (const QString& file : files) paths.push_back(file.toStdString());

  const std::vector<std::string> missing = missesFor(paths);
  if (!missing.empty()) {
    metadataTotal_ += missing.size();
    scanner_->enqueueAll(missing);
  }
  rescanLibrary();
  statusBar()->showMessage(tr("%1 faixa(s) na biblioteca").arg(files.size()), 6000);
}

void MainWindow::playFromLibrary(const QString& path) {
  if (path.isEmpty()) return;
  addPaths({path});
  playIndex(list_->count() - 1);
  setPlaylistVisible(true);
}

void MainWindow::armPlaycount(const std::string& path) {
  if (cache_ == nullptr) return;
  if (playcountTimer_ == nullptr) {
    playcountTimer_ = new QTimer(this);
    playcountTimer_->setSingleShot(true);
    connect(playcountTimer_, &QTimer::timeout, this, [this] {
      // Mesma janela do ML original (350 ms): pulou a faixa antes disso, não conta.
      if (playcountPath_.empty()) return;
      if (engine_->state() != wamp::PlaybackState::Playing) return;
      cache_->bumpPlaycount(playcountPath_);
      playcountPath_.clear();
    });
  }
  playcountPath_ = path;
  playcountTimer_->start(350);
}

void MainWindow::resumePendingScan() {
  if (cache_ == nullptr || scanner_ == nullptr) return;
  std::string error;
  const std::vector<std::string> pending = cache_->scanQueue(&error);
  if (pending.empty()) return;
  metadataTotal_ += pending.size();
  scanner_->enqueueAll(pending);
  updateMetadataStatus();
  qInfo("amplia: retomando indexação de %zu faixa(s)", pending.size());
}

void MainWindow::onMetadata(const std::string& path, const wamp::TrackInfo& info, bool ok) {
  if (ok) {
    metadata_[path] = info;
    if (cache_) cache_->store(info);
    // A tabela da Biblioteca é recarregada no fim do lote (ver abaixo), não a cada faixa:
    // reordenar a tabela a cada tag faria as linhas pularem enquanto o usuário lê.
    if (library_ != nullptr && library_->isVisible() && scanner_ != nullptr &&
        scanner_->pending() == 0) {
      refreshLibrary();
    }
  }

  if (ok && library_ != nullptr && library_->isVisible()) refreshLibrary();

  if (!ok && cache_ != nullptr) cache_->dropFromScanQueue(path);

  const QString qpath = QString::fromStdString(path);
  const bool current =
      playlist_->hasCurrent() && playlist_->at(playlist_->currentIndex()) == path;
  for (int row = 0; row < list_->count(); ++row) {
    QListWidgetItem* item = list_->item(row);
    if (item->data(Qt::UserRole).toString() != qpath) continue;
    if (!ok) continue;  // arquivo ilegível: a linha segue com o nome do arquivo
    item->setText(rowLabel(path, current));
    item->setData(kDurationRole, durationText(path));
  }

  // Fim de lote: confirma tags e a baixa na fila de indexação (inclusive a remoção de
  // arquivo ilegível). Sem isso, um kill no meio perderia o "já li" até o flush periódico.
  if (scanner_->pending() == 0 && cache_ != nullptr) cache_->flush();

  updateMetadataStatus();
  if (scanner_->pending() == 0) {
    statusBar()->showMessage(tr("Metadados da playlist carregados"), 2500);
  }
}

bool MainWindow::cachedInfo(const std::string& path, wamp::TrackInfo* info) {
  if (!cache_ || info == nullptr) return false;
  if (!cache_->lookup(path, info)) return false;
  metadata_[path] = *info;
  return true;
}

bool MainWindow::cachedInfoFast(const std::string& path, wamp::TrackInfo* info) {
  if (!cache_ || info == nullptr) return false;
  if (!cache_->lookupFast(path, info)) return false;
  metadata_[path] = *info;
  return true;
}

void MainWindow::setMissing(const std::string& path, bool missing) {
  if (missing) {
    if (!missingFiles_.insert(path).second) return;
  } else if (missingFiles_.erase(path) == 0) {
    return;
  }
  const QString qpath = QString::fromStdString(path);
  for (int row = 0; row < list_->count(); ++row) {
    QListWidgetItem* item = list_->item(row);
    if (item->data(Qt::UserRole).toString() != qpath) continue;
    item->setData(kMissingRole, missing);
    const bool current =
        playlist_->hasCurrent() && playlist_->at(playlist_->currentIndex()) == path;
    item->setText(rowLabel(path, current));
    item->setToolTip(missing ? tr("Arquivo não encontrado") : qpath);
  }
}

std::vector<std::string> MainWindow::missesFor(const std::vector<std::string>& paths) {
  std::vector<std::string> missing;
  missing.reserve(paths.size());
  for (const std::string& path : paths) {
    if (metadata_.find(path) != metadata_.end()) continue;
    wamp::TrackInfo cached;
    if (cachedInfo(path, &cached)) continue;
    missing.push_back(path);
  }
  return missing;
}

void MainWindow::startReading() {
  // A fila é mostrada pelo rótulo fixo da barra (com total e aviso), não por mensagem
  // transitória: as duas se sobrepunham e a mensagem era cortada pelo rótulo da versão.
  updateMetadataStatus();
}

void MainWindow::applyPlaylistMode() {
  if (playlistPanel_ == nullptr || playlistWindow_ == nullptr) return;
  if (playlistIntegrated()) {
    if (playlistWindow_->isVisible()) playlistWindow_->hide();
    if (playlistPanel_->parentWidget() != this) playlistPanel_->setParent(this);
    if (mainLayout_ != nullptr && mainLayout_->indexOf(playlistPanel_) < 0) {
      // Stretch 1: o espaço extra da janela é da lista, nunca do bloco do display.
      mainLayout_->addWidget(playlistPanel_, 1);
    }
    playlistPanel_->setVisible(playlistOpen_);
  } else {
    if (mainLayout_ != nullptr) mainLayout_->removeWidget(playlistPanel_);
    if (playlistPanel_->parentWidget() != playlistWindow_) {
      playlistPanel_->setParent(playlistWindow_);
      if (auto* windowLayout = qobject_cast<QVBoxLayout*>(playlistWindow_->layout())) {
        windowLayout->addWidget(playlistPanel_);
      }
    }
    playlistPanel_->show();
  }
}

bool MainWindow::playlistIntegrated() const { return playlistMode_ != QLatin1String("externa"); }

void MainWindow::setPlaylistMode(const QString& mode) {
  const QString wanted =
      mode == QLatin1String("externa") ? QStringLiteral("externa") : QStringLiteral("integrada");
  if (wanted == playlistMode_) return;
  // Guarda a altura do painel antes de trocar de dono.
  if (playlistIntegrated() && playlistOpen_ && height() > compactHeight_) {
    integratedHeight_ = std::clamp(height() - compactHeight_, 160, 900);
  }
  playlistMode_ = wanted;
  applyPlaylistMode();
  setPlaylistVisible(playlistOpen_);
  saveSettings();
}

void MainWindow::setPlaylistVisible(bool visible) {
  if (playlistPanel_ == nullptr || playlistWindow_ == nullptr) return;
  playlistOpen_ = visible;
  // O botão reflete o estado; emitir toggled aqui chamaria este método de novo.
  const QSignalBlocker blocker(playlistButton_);
  playlistButton_->setChecked(visible);

  // Limites de altura dependem só do estado (integrada + lista aberta ou não). Fixar o
  // máximo quando a lista fecha impedia o usuário de redimensionar qualquer coisa depois,
  // e um mínimo esquecido de "compacto + 160" travava a janela grande com vão morto.
  if (playlistIntegrated() && visible) {
    setMinimumHeight(compactHeight_ + 160);
    setMaximumHeight(QWIDGETSIZE_MAX);
  } else {
    setMinimumHeight(compactHeight_);
    setMaximumHeight(QWIDGETSIZE_MAX);
  }

  if (playlistIntegrated()) {
    // Padrão: a lista é painel da principal e a janela cresce para baixo.
    if (visible) {
      playlistPanel_->show();
      resize(width(), compactHeight_ + integratedHeight_);
    } else {
      if (height() > compactHeight_) {
        integratedHeight_ = std::clamp(height() - compactHeight_, 160, 900);
      }
      playlistPanel_->hide();
      resize(width(), compactHeight_);
    }
  } else if (visible) {
    // Janela própria (preparada para o porte de Windows). O handle é criado só agora: se a
    // principal já estiver mapeada, o Qt envia o xdg_toplevel.set_parent junto com o
    // mapeamento e o KWin trata a janela como dependente.
    auto showExternal = [this] {
      if (playlistWindow_->windowHandle() == nullptr) playlistWindow_->createWinId();
      if (playlistWindow_->windowHandle() != nullptr && windowHandle() != nullptr) {
        playlistWindow_->windowHandle()->setTransientParent(windowHandle());
      }
      playlistWindow_->show();
      if (playlistWindow_->windowHandle() != nullptr && windowHandle() != nullptr) {
        playlistWindow_->windowHandle()->setTransientParent(windowHandle());
      }
      playlistWindow_->raise();
      playlistWindow_->activateWindow();
    };
    if (windowPositionSupported()) {
      showExternal();
    } else {
      // Wayland: mapear no mesmo ciclo em que o painel muda de dono deixa a janela saindo
      // sem o set_parent (o KWin a trata como independente). Um ciclo de espera resolve,
      // e é o mesmo remédio da abertura inicial.
      QTimer::singleShot(200, this, showExternal);
    }
  } else {
    playlistWindow_->hide();
  }

  // Modo externo nunca deixa a principal esticada: a lista está na outra janela.
  if (!playlistIntegrated() && height() != compactHeight_) resize(width(), compactHeight_);

  if (qEnvironmentVariableIsSet("AMPLIA_DEBUG_WINDOWS")) {
    qInfo("janelas: modo=%s playlist visivel=%d principal=%dx%d", qPrintable(playlistMode_),
          playlistOpen_ ? 1 : 0, width(), height());
  }
  saveSettings();
}

void MainWindow::togglePlayPause() {
  engine_->togglePlayPause();
  refresh();
}

void MainWindow::showError(const QString& message) {
  if (display_ != nullptr) {
    display_->setTrackText(tr("Erro: ") + message);
    display_->setToolTip(message);
  }
  statusBar()->showMessage(message, 6000);
}

void MainWindow::updateArtwork(const wamp::TrackInfo& track) {
  artPath_.clear();
  if (track.path.empty()) return;

  // O applet do KDE só sabe carregar uma URL, então a capa vira um arquivo no cache,
  // nomeado por caminho + data de modificação + origem: mudou o arquivo ou a fonte da
  // capa, muda o nome e ninguém vê capa velha.
  const QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/art");
  QDir().mkpath(cacheDir);

  const auto digestFor = [&track](const char* source) {
    const QString key = QString::fromStdString(track.path) + QLatin1Char('|') +
                        QString::number(track.modifiedMs) + QLatin1Char('|') +
                        QString::fromLatin1(source);
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
  };

  // 1) Capa embutida no arquivo (ID3v2 APIC, FLAC picture, MP4 covr).
  std::vector<wamp::TagPicture> pictures;
  std::string error;
  if (wamp::TagEditor::read(track.path, nullptr, nullptr, &pictures, &error) &&
      !pictures.empty() && !pictures.front().data.empty()) {
    const wamp::TagPicture& picture = pictures.front();
    QString suffix = QStringLiteral("jpg");
    if (picture.mime.find("png") != std::string::npos) {
      suffix = QStringLiteral("png");
    } else if (picture.mime.find("webp") != std::string::npos) {
      suffix = QStringLiteral("webp");
    } else if (picture.mime.find("gif") != std::string::npos) {
      suffix = QStringLiteral("gif");
    }
    const QString file = cacheDir + QLatin1Char('/') + digestFor("tag") + QLatin1Char('.') + suffix;
    if (!QFile::exists(file)) {
      QFile out(file);
      if (out.open(QIODevice::WriteOnly)) {
        out.write(picture.data.data(), static_cast<qint64>(picture.data.size()));
        out.close();
      }
    }
    if (QFile::exists(file)) {
      artPath_ = file.toStdString();
      return;
    }
  }

  // 2) Sem tag, a capa da pasta: é assim que a maioria das discografias vem do Windows.
  const QString cover = QString::fromStdString(wamp::TagEditor::folderCover(track.path));
  if (cover.isEmpty()) return;
  QString suffix = QFileInfo(cover).suffix().toLower();
  if (suffix.isEmpty()) suffix = QStringLiteral("jpg");
  const QString file = cacheDir + QLatin1Char('/') + digestFor("folder") + QLatin1Char('.') + suffix;
  if (!QFile::exists(file)) QFile::copy(cover, file);
  if (QFile::exists(file)) artPath_ = file.toStdString();
}

void MainWindow::updateWindowTitle() {
  const wamp::TrackInfo track = engine_->track();
  QString title = QStringLiteral("Amplia");
  // Tocando (ou pausado) mostra a faixa: o painel de tarefas e o applet do KDE usam o
  // título da janela. Parado/ocioso volta ao nome do app.
  if (!track.path.empty() && engine_->state() != wamp::PlaybackState::Idle) {
    const QString display = QString::fromStdString(track.displayTitle());
    if (!display.isEmpty()) title = tr("%1 — Amplia").arg(display);
  }
  if (windowTitle() != title) setWindowTitle(title);
}

void MainWindow::refresh() {
  const wamp::PlaybackState state = engine_->state();
  // Play e pause são botões separados, como na skin clássica; o aceso mostra o estado.
  playButton_->setChecked(state == wamp::PlaybackState::Playing);
  pauseButton_->setChecked(state == wamp::PlaybackState::Paused);

  const wamp::TrackInfo track = engine_->track();
  const std::int64_t duration = engine_->durationMs();

  if (display_ != nullptr) {
    display_->setState(state);
    display_->setPositionMs(engine_->positionMs());
    display_->setDurationMs(duration);
    display_->setFormat(track.bitrate, track.sampleRate, track.channels);
    display_->setEqualizerActive(engine_->equalizerEnabled());
    display_->setRepeatMode(repeatMode_);
    display_->setShuffleActive(shuffle_);
    if (!track.path.empty()) {
      // A duração vai junto do nome, como no original: "Artista - Faixa (4:54)".
      const std::string artist = track.artist.empty() ? track.albumArtist : track.artist;
      display_->setTrackInfo(QString::fromStdString(artist),
                             QString::fromStdString(track.title.empty() ? track.displayTitle()
                                                                        : track.title));
      display_->setToolTip(QString::fromStdString(track.path));
    } else {
      display_->setTrackInfo(QString(), QString());
    }
  }

  updateWindowTitle();

  if (mpris_) {
    mpris_->refreshStatus();
    mpris_->refreshMetadata();
  }
  onPlaybackTick();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void MainWindow::handleDroppedPaths(const QStringList& paths) {
  if (paths.isEmpty()) return;
  addPaths(paths);  // arrastar organiza a lista; não começa a tocar sozinho
}

void MainWindow::dropEvent(QDropEvent* event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (urls.isEmpty()) return;

  QStringList paths;
  int rejected = 0;
  for (const QUrl& url : urls) {
    const QString local = url.toLocalFile();
    if (!local.isEmpty()) {
      paths << local;
    } else {
      ++rejected;
    }
  }
  if (paths.isEmpty()) {
    warnAboutNetworkDrop(this, urls, rejected);
    return;
  }

  event->acceptProposedAction();
  handleDroppedPaths(paths);
  warnAboutNetworkDrop(this, urls, rejected);
}
