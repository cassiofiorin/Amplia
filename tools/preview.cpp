// Renderiza a lista da playlist num PNG, sem abrir janela — serve para conferir o
// desenho das colunas (número da faixa à esquerda, posição da fila à direita) sem
// depender de olhar a tela.
//
// Uso: wamp-ui-preview [saida.png] [--equalizer]
#include <QApplication>
#include <QListWidget>
#include <QPixmap>
#include <QString>

#include <cstdio>

#include "display_panel.h"
#include "theme.h"
#include "translator.h"
#include "equalizer_dialog.h"
#include "file_info_dialog.h"
#include "preferences_dialog.h"
#include "theme_editor.h"
#include "theme_store.h"
#include "icons.h"
#include "playlist_delegate.h"

#include <QImage>
#include <QPainter>

#include <array>
#include <iterator>

int main(int argc, char** argv) {
  // Prévia respeita o idioma: AMPLIA_LANG=en renderiza a tela em inglês.
  QApplication app(argc, argv);
  wamp::ui::installTranslators(wamp::ui::languageFromSettings());

  // A prévia usa o esquema pedido (AMPLIA_THEME) para mostrar o desenho com as cores reais.
  const QString themeId = qEnvironmentVariable("AMPLIA_THEME", QStringLiteral("grafite"));
  wamp::ui::setActiveTheme(themeId);
  app.setStyleSheet(wamp::ui::styleSheet(wamp::ui::activeTheme()));

  const QString out =
      argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/playlist-preview.png");

  // A janela do equalizador também é conferida sem tela: nada de capturar a tela do
  // usuário, que já pegou o navegador dele uma vez.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--equalizer")) {
      std::array<double, 10> bands{};
      bands[0] = 6.0;
      bands[4] = -3.0;
      bands[9] = 4.0;
      wamp::ui::EqualizerDialog dialog(true, bands, -2.0);
      dialog.setStyleSheet(QStringLiteral(
          "QDialog { background: #1e1e1e; } QLabel { color: #d0d0d0; }"
          "QCheckBox, QComboBox, QPushButton { color: #e0e0e0; }"));
      dialog.resize(dialog.sizeHint());
      dialog.show();
      app.processEvents();
      dialog.grab().save(out);
      std::printf("imagem do equalizador em %s\n", qPrintable(out));
      return 0;
    }
  }

  // Editor de cores: confere o diálogo sem abrir janela.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--theme-editor")) {
      ThemeDialog dialog(QColor(0x2b, 0x2f, 0x3a), QColor(0x39, 0xd0, 0x5a),
                         QColor(0xc8, 0xcd, 0xd8), QColor(0x3f, 0x45, 0x53), 110);
      dialog.setThemes(wamp::ui::loadSavedThemes(), QStringLiteral("classico"));
      dialog.resize(700, 430);
      dialog.show();
      app.processEvents();
      dialog.grab().save(out);
      std::printf("imagem do editor de cores em %s\n", qPrintable(out));
      return 0;
    }
  }

  // Preferências: confere o diálogo (abas, agrupamentos) sem abrir janela.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--fileinfo")) {
      // Amostra do Alt+3 com o grupo "Análise local" preenchido, para conferir o desenho
      // sem precisar de arquivo real nem de banco.
      wamp::ui::FileInfoDialog dialog({std::string("/tmp/amostra-faixa-exemplo.mp3")}, 0, false);
      dialog.setAnalysis(true, 117, 7, 1);
      dialog.resize(dialog.sizeHint());
      dialog.show();
      app.processEvents();
      dialog.grab().save(out);
      std::printf("imagem do File Info em %s\n", qPrintable(out));
      return 0;
    }
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--prefs")) {
      wamp::ScrobbleConfig scrobble;
      PreferencesDialog dialog(true, 50, false, true, true, 15, 0.0, 0.0, QStringLiteral("integrada"),
                           false, scrobble, QStringLiteral("sistema"));
      dialog.resize(dialog.sizeHint());
      dialog.show();
      app.processEvents();
      dialog.grab().save(out);
      std::printf("imagem das preferências em %s\n", qPrintable(out));
      return 0;
    }
  }

  // Folha dos ícones: confere o desenho sem capturar tela nenhuma.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--icons")) {
      const QIcon icons[] = {wamp::ui::previousIcon(), wamp::ui::playIcon(), wamp::ui::pauseIcon(),
                             wamp::ui::stopIcon(), wamp::ui::nextIcon(), wamp::ui::shuffleIcon(),
                             wamp::ui::repeatIcon(), wamp::ui::repeatOneIcon()};
      const int count = static_cast<int>(std::size(icons));
      QImage sheet(count * 44, 44, QImage::Format_ARGB32_Premultiplied);
      sheet.fill(QColor(0x1e, 0x1e, 0x1e));
      QPainter painter(&sheet);
      for (int i = 0; i < count; ++i) {
        painter.drawPixmap(i * 44 + 2, 2, icons[i].pixmap(40, 40));
      }
      painter.end();
      sheet.save(out);
      return 0;
    }
  }

  // Folha de conferência dos dez dígitos: cada linha é um segundo diferente.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--digits")) {
      QImage sheet(460, 100 * 10, QImage::Format_ARGB32_Premultiplied);
      sheet.fill(Qt::black);
      QPainter painter(&sheet);
      for (int second = 0; second < 10; ++second) {
        wamp::ui::DisplayPanel panel;
        panel.setTrackText(QStringLiteral("segundo %1").arg(second));
        panel.setState(wamp::PlaybackState::Playing);
        panel.setFormat(320, 48000, 2);
        panel.setPositionMs(second * 1000 + 7000);
        panel.setDurationMs(203000);
        panel.resize(460, 100);
        panel.show();
        app.processEvents();
        painter.drawPixmap(0, second * 100, panel.grab());
      }
      painter.end();
      sheet.save(out);
      std::printf("folha de digitos em %s\n", qPrintable(out));
      return 0;
    }
  }

  // O display do player: relógio de sete segmentos + informações + título + posição.
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--display")) {
      wamp::ui::DisplayPanel panel;
      panel.setStyleSheet(QStringLiteral("QWidget { font-family: sans-serif; }"));
      panel.setTrackText(QStringLiteral("The Warning - Ritual (3:23)"));
      panel.setState(wamp::PlaybackState::Playing);
      panel.setFormat(320, 48000, 2);
      panel.setEqualizerActive(true);
      panel.setRepeatMode(0);
      panel.setShuffleActive(false);
      panel.setPositionMs(7000);
      panel.setDurationMs(203000);
      panel.resize(460, 100);
      panel.show();
      app.processEvents();
      panel.grab().save(out);
      std::printf("imagem do display em %s\n", qPrintable(out));
      return 0;
    }
  }

  QListWidget list;
  list.resize(400, 132);
  list.setStyleSheet(QStringLiteral(
      "QListWidget { background: #1a1a1a; border: 1px solid #3a3a3a; color: #d0d0d0;"
      " font-family: monospace; font-size: 11px; }"
      "QListWidget::item:selected { background: #4a3d1a; color: #ffffff; }"));

  auto* delegate = new PlaylistDelegate(&list);
  list.setItemDelegate(delegate);

  const char* labels[] = {
      "Dogstar - This Sphere",
      "Title Fight - Safe in Your Skin",
      "Bôa - Duvet",
      "Slowdive - Alison",
      "Nada Surf - Popular",
  };
  const char* durations[] = {"3:16", "3:02", "3:17", "3:47", "3:41"};
  for (int i = 0; i < 5; ++i) {
    auto* item = new QListWidgetItem(QString::fromUtf8(labels[i]), &list);
    item->setData(kDurationRole, QString::fromUtf8(durations[i]));
  }

  delegate->setTrackCount(5);
  list.item(1)->setData(kQueueRole, 1);
  list.item(3)->setData(kQueueRole, 2);
  list.setCurrentRow(0);

  list.show();
  app.processEvents();

  if (!list.grab().save(out)) {
    std::fprintf(stderr, "não consegui salvar %s\n", out.toLocal8Bit().constData());
    return 1;
  }
  std::printf("%s\n", out.toLocal8Bit().constData());
  return 0;
}
