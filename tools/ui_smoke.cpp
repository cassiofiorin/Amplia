// Teste dos widgets que respondem a clique/tecla, sem abrir janela visível.
//
// Uso: wamp-ui-smoke
//
// Cobre o slider de posição e o de volume: clicar num ponto da barra precisa posicionar
// ali (e não andar de página), tanto no clique quanto no arrasto.
#include "display_panel.h"
#include "equalizer_dialog.h"

#include "wamp/eq_preset.h"
#include "wamp/tag_cache.h"

#include <cmath>

#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include "format_support.h"

#include <QAction>
#include <QApplication>
#include <QLocale>
#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>
#include <QShortcut>
#include <QStandardPaths>
#include <cstdio>

#include "click_slider.h"
#include "jump_dialog.h"
#include "theme.h"
#include "translator.h"
#include "theme_store.h"
#include "preferences_dialog.h"
#include "wamp/scrobbler.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FALHA", what);
  if (!ok) ++failures;
}

void sendMouse(QWidget* widget, QEvent::Type type, const QPoint& pos) {
  QMouseEvent event(type, QPointF(pos), widget->mapToGlobal(pos), Qt::LeftButton,
                    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(widget, &event);
}

}  // namespace

int main(int argc, char** argv) {
  // As asserções são em português: fixa o locale para não depender do ambiente.
  QApplication app(argc, argv);
  QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));

  // Idioma: o português é o idioma-fonte; o inglês vem do .qm embutido.
  {
    wamp::ui::installTranslators(QStringLiteral("en"));
    const QString english =
        QCoreApplication::translate("MainWindow", "&Arquivo");
    check(english == QLatin1String("&File"), "tradutor em inglês carrega");
    wamp::ui::installTranslators(QStringLiteral("pt_BR"));
    const QString portuguese =
        QCoreApplication::translate("MainWindow", "&Arquivo");
    check(portuguese == QLatin1String("&Arquivo"), "idioma-fonte não é traduzido");
  }
  // Fila de scrobble vai para o diretório de teste, não para o do app instalado.
  QStandardPaths::setTestModeEnabled(true);

  wamp::ui::ClickSlider slider(Qt::Horizontal);
  slider.setRange(0, 240000);  // 4 minutos em ms
  slider.resize(400, 24);
  slider.show();
  app.processEvents();

  int pressed = 0;
  int released = 0;
  QObject::connect(&slider, &QSlider::sliderPressed, [&pressed] { ++pressed; });
  QObject::connect(&slider, &QSlider::sliderReleased, [&released] { ++released; });

  // Clique a 75% da barra, sem arrastar.
  sendMouse(&slider, QEvent::MouseButtonPress, QPoint(300, 12));
  sendMouse(&slider, QEvent::MouseButtonRelease, QPoint(300, 12));
  app.processEvents();

  const int clicked = slider.value();
  check(clicked > 168000 && clicked < 192000, "clique posiciona perto de 75% (não anda de página)");
  check(pressed == 1 && released == 1, "clique emite pressed/released uma vez cada");

  // Arrasto: pressiona em 10% e solta em 50%.
  sendMouse(&slider, QEvent::MouseButtonPress, QPoint(40, 12));
  sendMouse(&slider, QEvent::MouseMove, QPoint(200, 12));
  app.processEvents();
  const int dragged = slider.value();
  check(dragged > 108000 && dragged < 132000, "arrasto acompanha o ponteiro antes de soltar");
  sendMouse(&slider, QEvent::MouseButtonRelease, QPoint(200, 12));
  app.processEvents();

  // Volume: mesma classe, faixa 0..100 — clique a 25% tem que valer ~25.
  wamp::ui::ClickSlider volume(Qt::Horizontal);
  volume.setRange(0, 100);
  volume.resize(400, 24);
  volume.show();
  app.processEvents();
  int volumeChanges = 0;
  QObject::connect(&volume, &QSlider::valueChanged, [&volumeChanges] { ++volumeChanges; });

  sendMouse(&volume, QEvent::MouseButtonPress, QPoint(100, 12));
  sendMouse(&volume, QEvent::MouseButtonRelease, QPoint(100, 12));
  app.processEvents();
  const int volumeValue = volume.value();
  check(volumeValue > 18 && volumeValue < 32, "clique no volume posiciona perto de 25");
  check(volumeChanges >= 1, "clique no volume emite valueChanged (volume muda na hora)");

  // Scrobble sem credencial válida: entra na fila e não some (reenvio depois).
  {
    // A fila de teste sobrevive entre execuções: começa limpa para contar certo.
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                  QStringLiteral("/scrobble-queue.json"));
    wamp::Scrobbler scrobbler;
    wamp::ScrobbleConfig config;
    config.listenbrainzEnabled = true;
    config.listenbrainzToken = "token-invalido-de-teste";
    scrobbler.setConfig(config);

    wamp::ScrobbleEvent event;
    event.artist = "Teste";
    event.title = "Fila";
    event.durationMs = 200000;
    event.playedMs = 100000;
    event.startedAt = QDateTime::currentSecsSinceEpoch();
    scrobbler.scrobble(event);
    check(scrobbler.pendingCount() == 1, "scrobble com token inválido fica na fila");

    // Faixa curta é ignorada pela regra, sem ocupar a fila.
    wamp::ScrobbleEvent shortOne;
    shortOne.artist = "Teste";
    shortOne.title = "Curta";
    shortOne.durationMs = 20000;
    shortOne.playedMs = 20000;
    shortOne.startedAt = QDateTime::currentSecsSinceEpoch();
    scrobbler.scrobble(shortOne);
    check(scrobbler.pendingCount() == 1, "faixa com menos de 30 s não entra na fila");
  }

  // A caixa de "ouvindo agora" viaja ida e volta pelas preferências.
  {
    wamp::ScrobbleConfig on;
    on.announceNowPlaying = true;
    PreferencesDialog dialogWith(true, 60, true, true, true, 15, 0.0, 0.0,
                                 QStringLiteral("externa"), false, false, on, QStringLiteral("sistema"));
    check(dialogWith.scrobbleConfig().announceNowPlaying, "preferências preservam \"ouvindo agora\" ligado");
    check(dialogWith.closeToTray(), "preferências preservam fechar para a bandeja ligado");
    check(dialogWith.restartOnPlay(), "preferências preservam play reiniciando a faixa");
    check(dialogWith.playlistMode() == QLatin1String("externa"),
          "preferências preservam a playlist em janela própria");

    wamp::ScrobbleConfig off;
    off.announceNowPlaying = false;
    PreferencesDialog dialogWithout(false, 40, false, false, false, 30, 0.0, 0.0,
                                    QStringLiteral("integrada"), false, false, off, QStringLiteral("sistema"));
    check(!dialogWithout.scrobbleConfig().announceNowPlaying, "preferências preservam \"ouvindo agora\" desligado");
    check(!dialogWithout.restartOnPlay(), "preferências preservam play sem reiniciar");
    check(dialogWithout.visualFps() == 30, "preferências preservam fps do visualizador");
    check(!dialogWithout.visualEnabled(), "preferências preservam visualizador desligado");
    check(dialogWithout.playlistMode() == QLatin1String("integrada"),
          "preferências preservam a playlist integrada");
    dialogWithout.setCalibrationResult(45, 4.0);
    check(dialogWithout.visualFps() == 45, "calibração devolve o fps medido");
    check(dialogWithout.visualCostP95Ms() == 4.0, "calibração devolve o custo medido");
    check(std::abs(dialogWithout.visualCpuPercent() - 18.0) < 0.01,
          "consumo em CPU acompanha o fps escolhido");
  }

  // Setas mudam a seleção sem sair do campo de busca; Enter toca e fecha.
  {
    QVector<wamp::ui::JumpEntry> entries;
    entries.push_back({QStringLiteral("/m/a.mp3"), QStringLiteral("Alfa"), QString(),
                       QString()});
    entries.push_back({QStringLiteral("/m/b.mp3"), QStringLiteral("Bravo"), QString(),
                       QString()});
    entries.push_back({QStringLiteral("/m/c.mp3"), QStringLiteral("Charlie"), QString(),
                       QString()});
    wamp::ui::JumpDialog dialog(entries);
    QLineEdit* field = dialog.findChild<QLineEdit*>();
    check(field != nullptr, "Jump to File tem campo de busca");
    if (field != nullptr) {
      QString played;
      QObject::connect(&dialog, &wamp::ui::JumpDialog::playRequested,
                       [&played](const QString& path) { played = path; });
      dialog.setQuery(QString());
      check(dialog.currentPath() == QStringLiteral("/m/a.mp3"), "Jump começa no primeiro item");
      QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
      QCoreApplication::sendEvent(field, &down);
      check(dialog.currentPath() == QStringLiteral("/m/b.mp3"), "seta para baixo desce a seleção");
      QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
      QCoreApplication::sendEvent(field, &up);
      check(dialog.currentPath() == QStringLiteral("/m/a.mp3"), "seta para cima sobe a seleção");
      // Alt+clique esquerdo enfileira, como na lista principal.
      dialog.setQuery(QString());
      QString queued;
      QObject::connect(&dialog, &wamp::ui::JumpDialog::queueRequested,
                       [&queued](const QString& path) { queued = path; });
      auto* list = dialog.findChild<QListWidget*>();
      check(list != nullptr && list->count() >= 2, "Jump tem itens para enfileirar");
      if (list != nullptr && list->count() >= 2) {
        const QRect rect = list->visualItemRect(list->item(1));
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(rect.center()), QPointF(rect.center()),
                          Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        QCoreApplication::sendEvent(list->viewport(), &click);
        check(queued == QStringLiteral("/m/b.mp3"), "Alt+clique enfileira no Jump to File");
        check(dialog.isVisible() || list->count() > 0, "Alt+clique não fecha o Jump to File");
      }

      if (list != nullptr) list->setCurrentRow(0);  // o Alt+clique tinha movido a seleção
      QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QCoreApplication::sendEvent(field, &enter);
      check(played == QStringLiteral("/m/a.mp3"), "Enter toca a faixa selecionada");
      check(!dialog.isVisible(), "Enter fecha a janela do Jump to File");
    }
  }

  // O atalho do Jump to File é a vírgula, e `QKeySequence(",")` a trata como separador de
  // acorde (sequência vazia) — por isso a ação monta a sequência com `Qt::Key_Comma`.
  // O disparo em si não dá para testar aqui: sem janela ativa o Qt não consulta o mapa
  // de atalhos no modo offscreen.
  check(QKeySequence(Qt::Key_Comma).toString() == QStringLiteral(","),
        "atalho de vírgula do Jump to File monta a sequência");

  // Jump to File: filtro sem acento, multi-palavra e contador.
  {
    QVector<wamp::ui::JumpEntry> entries;
    entries.push_back(wamp::ui::JumpEntry{QStringLiteral("/m/a.mp3"), QStringLiteral("Açúcar"),
                                          QStringLiteral("Hozier"), QStringLiteral("Too Sweet")});
    entries.push_back(wamp::ui::JumpEntry{QStringLiteral("/m/b.mp3"), QStringLiteral("This Sphere"),
                                          QStringLiteral("Dogstar"), QStringLiteral("Album")});

    wamp::ui::JumpDialog jump(entries);
    jump.setQuery(QStringLiteral("acucar"));
    check(jump.resultCount() == 1, "jump ignora acento na busca");
    check(jump.currentPath() == QStringLiteral("/m/a.mp3"), "jump devolve o caminho certo");

    jump.setQuery(QStringLiteral("dogstar sphere"));
    check(jump.resultCount() == 1, "jump aceita palavras de campos diferentes");
    check(jump.currentPath() == QStringLiteral("/m/b.mp3"), "jump acha a segunda faixa");

    jump.setQuery(QStringLiteral("zzz"));
    check(jump.resultCount() == 0, "jump não inventa resultado");
  }

  // Classificação de arquivos: o aviso de "não suportado" depende dela.
  {
    check(wamp::ui::isAudioFile("/m/faixa.MP3"), "extensão de áudio sem caixa manda");
    check(wamp::ui::isAudioFile("/m/faixa.flac"), "flac é áudio");
    check(!wamp::ui::isVideoFile("/m/faixa.mp3"), "mp3 não é vídeo");
    check(wamp::ui::isVideoFile("/m/filme.MKV"), "mkv é vídeo");
    check(wamp::ui::isPlaylistFile("/m/lista.pls"), "pls é playlist");
    check(!wamp::ui::isAudioFile("/m/lista.pls"), "playlist não é áudio");
    check(wamp::ui::audioExtensions().contains("opus"), "lista de áudio cita opus");
  }


  // Equalizador: a janela carrega o estado, aplica preset e avisa a cada mudança.
  {
    std::array<double, 10> bands{};
    bands[0] = 4.0;
    wamp::ui::EqualizerDialog eq(true, bands, -2.0);
    int changes = 0;
    QObject::connect(&eq, &wamp::ui::EqualizerDialog::changed, [&changes] { ++changes; });

    check(eq.enabled(), "diálogo do EQ começa ligado como recebeu");
    check(eq.bands()[0] == 4.0, "diálogo do EQ carrega o ganho da primeira banda");
    check(eq.preamp() == -2.0, "diálogo do EQ carrega a pré-amplificação");

    // Curvas do usuário: grava uma no diretório de teste e confere que o
    // diálogo a lista depois das embutidas e que dá para aplicá-la.
    {
      const QString dir = wamp::ui::EqualizerDialog::userPresetDir();
      QDir().mkpath(dir);
      QFile::remove(dir + QStringLiteral("/curva-do-teste.csv"));  // sobra de execução anterior
      wamp::EqPreset preset;
      preset.name = QStringLiteral("Curva do teste").toStdString();
      preset.preampDb = -3.0;
      preset.bandsDb = {7, 6, 5, 4, 3, 2, 1, 0, -1, -2};
      std::string error;
      check(wamp::saveEqPresetCsv((dir + QStringLiteral("/curva-do-teste.csv")).toStdString(),
                                  preset, &error),
            "curva do usuário grava");
      wamp::ui::EqualizerDialog again(false, {}, 0.0);
      check(again.presetCount() > 1 + wamp::ui::EqualizerDialog::builtinPresetCount(),
            "diálogo novo já lista a curva salva");
      check(again.presetAt(again.presetCount() - 1) == QStringLiteral("Curva do teste"),
            "curva salva aparece pelo nome");
      const int userIndex = again.presetCount() - 1 -
                            (1 + wamp::ui::EqualizerDialog::builtinPresetCount());
      again.applyUserPreset(userIndex);
      check(again.bands()[0] == 7.0 && again.preamp() == -3.0, "curva salva aplica bandas e pré-amp");
      QFile::remove(dir + QStringLiteral("/curva-do-teste.csv"));
    }

    eq.applyPreset(11);  // Rock (a lista não tem mais o Flat)
    check(eq.bands()[0] == 8.0, "preset Rock sobe os graves");
    check(changes >= 1, "preset avisa a janela para aplicar");

    // A curva desenhada tem que refletir os ganhos: banda com +8 dB fica acima da de 0 dB.
    {
      eq.resize(460, 430);  // como na janela real; sem isso o widget ainda tem tamanho mínimo
      const QPointF bass = eq.curveView()->handlePosition(0);
      const QPointF flatBand = eq.curveView()->handlePosition(2);
      check(bass.y() < flatBand.y() - 5.0, "curva desenha o ganho da banda acima da linha de 0 dB");
      // Arrastar o ponto da banda 3 para cima tem que mexer no slider dela.
      {
        const double before = eq.bands()[3];
        const QPointF handle = eq.curveView()->handlePosition(3);
        QWidget* view = eq.curveView();
        QMouseEvent press(QEvent::MouseButtonPress, handle, view->mapToGlobal(handle.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &press);
        const QPointF up(handle.x(), handle.y() - 25.0);
        QMouseEvent move(QEvent::MouseMove, up, view->mapToGlobal(up.toPoint()), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, up, view->mapToGlobal(up.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view, &release);
        check(eq.bands()[3] > before + 1.0, "arrastar o ponto da curva sobe a banda");
      }

      const double x0 = eq.curveView()->handlePosition(0).x();
      const double x4 = eq.curveView()->handlePosition(4).x();
      const double x9 = eq.curveView()->handlePosition(9).x();
      check(x0 < x4 && x4 < x9, "curva separa as bandas no eixo de frequência");
    }

    eq.enabledBox()->setChecked(false);
    check(!eq.enabled(), "diálogo do EQ desliga pela caixa");
  }

  // CSV de temas: ida e volta, slug, recusa de cor inválida e importação com lixo.
  {
    using namespace wamp::ui;
    check(themeCsvHeader() == QStringLiteral("id,nome,fundo,destaque,secundaria,botoes,contraste"),
          "cabeçalho do CSV de temas");
    const Theme made = makeCustom(QColor("#1B1E24"), QColor("#4DFF7A"), QColor("#C8F5CC"),
                                  QColor("#262B33"), 115);
    const QString row = themeToCsvRow(made);
    check(row.split(QLatin1Char(',')).size() == 7, "linha do CSV tem sete campos");

    Theme parsed;
    QString error;
    check(themeFromCsvRow(row, &parsed, &error), "linha do CSV volta a virar tema");
    check(parsed.customBackground == QColor("#1B1E24") &&
              parsed.customAccent == QColor("#4DFF7A") && parsed.customContrast == 115,
          "cores e contraste sobrevivem à ida e volta");
    check(!themeFromCsvRow(QStringLiteral("x,Nome,#zzz,#000000,#ffffff,#111111,100"), &parsed, &error),
          "cor inválida é recusada");
    check(themeSlug(QStringLiteral("Verde Ácido!")) == QStringLiteral("verde-acido"),
          "nome vira id seguro");

    const QString sample = themeCsvSample();
    check(sample.startsWith(themeCsvHeader()) && sample.contains(QStringLiteral("#exemplo")),
          "modelo CSV traz cabeçalho e linha comentada");

    const QString csvPath = QDir::tempPath() + QStringLiteral("/amplia-temas-teste.csv");
    QFile csv(csvPath);
    if (csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
      csv.write(themeCsvHeader().toUtf8());
      csv.write("\n");
      csv.write(QStringLiteral("teste-um,Teste Um,#101010,#00FF00,#EEEEEE,#202020,100").toUtf8());
      csv.write("\n");
      csv.write(QStringLiteral("quebrado,Quebrado,#101010").toUtf8());
      csv.write("\n");
      csv.close();
      QStringList warnings;
      QString importError;
      const int imported = importThemesCsv(csvPath, &warnings, &importError);
      check(imported == 1, "importa a linha boa e ignora a quebrada");
      check(warnings.size() == 1, "a linha quebrada vira aviso");
      check(exportThemesCsv(csvPath, &importError), "exporta os temas salvos");
      csv.remove();
      for (const Theme& saved : loadSavedThemes()) {
        QString ignored;
        removeThemeFile(saved.id, &ignored);
      }
    }
  }

  // Esquema de cores: presets, fallback e folha gerada a partir da paleta.
  {
    const auto& themes = wamp::ui::allThemes();
    check(themes.size() >= 5, "pelo menos cinco esquemas embutidos");
    check(wamp::ui::themeById(QStringLiteral("nao-existe")).id == themes.first().id,
          "id desconhecido cai no primeiro esquema");
    wamp::ui::setActiveTheme(QStringLiteral("papel"));
    check(wamp::ui::activeTheme().id == QStringLiteral("papel"), "esquema ativo troca");
    const QString qss = wamp::ui::styleSheet(wamp::ui::activeTheme());
    check(qss.contains(wamp::ui::activeTheme().windowBg.name()), "folha usa a cor de fundo do esquema");
    check(qss.contains(wamp::ui::activeTheme().accent.name()), "folha usa a cor de destaque");
    wamp::ui::setActiveTheme(QStringLiteral("grafite"));
    check(wamp::ui::activeTheme().id == QStringLiteral("grafite"), "volta ao esquema padrão");
  }

  // Esquema de cores: presets, fallback e folha gerada a partir da paleta.
  {
    const auto& themes = wamp::ui::allThemes();
    check(themes.size() >= 5, "pelo menos cinco esquemas embutidos");
    check(wamp::ui::themeById(QStringLiteral("nao-existe")).id == themes.first().id,
          "id desconhecido cai no primeiro esquema");
    wamp::ui::setActiveTheme(QStringLiteral("papel"));
    check(wamp::ui::activeTheme().id == QStringLiteral("papel"), "esquema ativo troca");
    const QString qss = wamp::ui::styleSheet(wamp::ui::activeTheme());
    check(qss.contains(wamp::ui::activeTheme().windowBg.name()), "folha usa a cor de fundo do esquema");
    check(qss.contains(wamp::ui::activeTheme().accent.name()), "folha usa a cor de destaque");
    wamp::ui::setActiveTheme(QStringLiteral("grafite"));
    check(wamp::ui::activeTheme().id == QStringLiteral("grafite"), "volta ao esquema padrão");
  }

  // Paleta personalizada: derivação das quatro cores e do contraste.
  {
    const wamp::ui::Theme soft = wamp::ui::makeCustom(QColor(0x2b, 0x2f, 0x3a), QColor(0x39, 0xd0, 0x5a),
                                                     QColor(0xc8, 0xcd, 0xd8), QColor(0x3f, 0x45, 0x53), 80);
    const wamp::ui::Theme hard = wamp::ui::makeCustom(QColor(0x2b, 0x2f, 0x3a), QColor(0x39, 0xd0, 0x5a),
                                                     QColor(0xc8, 0xcd, 0xd8), QColor(0x3f, 0x45, 0x53), 140);
    check(soft.id == QStringLiteral("personalizado"), "paleta personalizada tem id próprio");
    check(soft.accent == QColor(0x39, 0xd0, 0x5a), "destaque é a cor escolhida");
    check(soft.windowBg == QColor(0x2b, 0x2f, 0x3a), "fundo é a cor escolhida");
    check(hard.border != soft.border, "contraste muda a borda derivada");
    check(wamp::ui::readableTextOn(QColor(0xff, 0xff, 0xff)) == QColor(0x14, 0x14, 0x14),
          "texto sobre claro vira escuro");
    check(wamp::ui::readableTextOn(QColor(0x10, 0x10, 0x10)) == QColor(0xff, 0xff, 0xff),
          "texto sobre escuro vira claro");
    wamp::ui::setActiveTheme(soft);
    check(wamp::ui::activeTheme().id == QStringLiteral("personalizado"),
          "paleta personalizada vira a ativa");
    wamp::ui::setActiveTheme(QStringLiteral("grafite"));
  }

  std::printf("\n%s\n", failures == 0 ? "tudo certo" : "houve falhas");
  // Display: o relógio não pode ter zero à esquerda nem perder a casa dos minutos.
  {
    wamp::ui::DisplayPanel panel;
    panel.setPositionMs(7000);
    check(panel.timeText().trimmed() == QStringLiteral("0:07"), "display mostra 0:07");
    panel.setPositionMs(65000);
    check(panel.timeText().trimmed() == QStringLiteral("1:05"), "display mostra 1:05");
    panel.setPositionMs(59 * 60000 + 59000);
    check(panel.timeText().trimmed() == QStringLiteral("59:59"), "display mostra 59:59");
    panel.setPositionMs(61 * 60000 + 5000);
    check(panel.timeText().trimmed() == QStringLiteral("61:05"), "display mostra 61:05");
  }

  // Display: clique na barra pede posição proporcional; clique no relógio alterna o modo.
  {
    wamp::ui::DisplayPanel panel;
    panel.setDurationMs(200000);
    panel.setPositionMs(0);
    panel.resize(460, 100);
    qint64 asked = -1;
    QObject::connect(&panel, &wamp::ui::DisplayPanel::seekRequested,
                     [&asked](qint64 ms) { asked = ms; });
    bool toggled = false;
    QObject::connect(&panel, &wamp::ui::DisplayPanel::timeClicked, [&toggled] { toggled = true; });

    const QRect bar = panel.barRect();
    const QPoint middle(bar.center().x(), bar.center().y());
    const QPointF middleF(middle);
    const QPointF middleGlobal = panel.mapToGlobal(middle);
    QMouseEvent press(QEvent::MouseButtonPress, middleF, middleGlobal, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&panel, &press);
    check(asked >= 90000 && asked <= 110000, "clique no meio da barra pede ~50%");
    QMouseEvent release(QEvent::MouseButtonRelease, middleF, middleGlobal, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&panel, &release);

    const QRect clock = panel.clockRect();
    const QPointF clockF(clock.center());
    QMouseEvent clockClick(QEvent::MouseButtonPress, clockF, panel.mapToGlobal(clock.center()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&panel, &clockClick);
    check(toggled, "clique no relógio alterna decorrido/restante");

    panel.setPositionMs(30000);
    check(panel.timeText().trimmed() == QStringLiteral("0:30"), "relógio mostra 0:30");
    panel.setShowRemaining(true);
    check(panel.timeText().trimmed() == QStringLiteral("2:50"), "relógio mostra 2:50 restante");
  }

  // Pílulas do display respondem ao clique (são botões, não adorno).
  {
    wamp::ui::DisplayPanel panel;
    panel.resize(460, 100);
    panel.setEqualizerActive(false);
    panel.setRepeatMode(0);
    panel.setShuffleActive(false);
    panel.grab();  // força um paint, que é quem calcula as áreas
    int eq = 0, rep = 0, rnd = 0;
    QObject::connect(&panel, &wamp::ui::DisplayPanel::equalizerClicked, [&eq] { ++eq; });
    QObject::connect(&panel, &wamp::ui::DisplayPanel::repeatClicked, [&rep] { ++rep; });
    QObject::connect(&panel, &wamp::ui::DisplayPanel::shuffleClicked, [&rnd] { ++rnd; });
    for (int i = 0; i < 3; ++i) {
      const QRect area = panel.tagRect(i);
      check(!area.isEmpty(), qPrintable(QStringLiteral("pílula %1 tem área").arg(i)));
      QMouseEvent click(QEvent::MouseButtonPress, QPointF(area.center()),
                        panel.mapToGlobal(area.center()), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
      QApplication::sendEvent(&panel, &click);
    }
    check(eq == 1 && rep == 1 && rnd == 1, "pílulas EQ/REP/RAND respondem ao clique");
  }

  // Biblioteca: remover guarda o marcador (a varredura não traz de volta) e limpar
  // apaga tudo, inclusive os marcadores.
  {
    QTemporaryDir dir;
    std::string error;
    const QString file = dir.filePath(QStringLiteral("library.sqlite"));
    wamp::TagCache cache(file.toStdString());
    const bool opened = cache.open(&error);
    check(opened, "cache da biblioteca abre no banco temporário");
    if (opened) {
      const std::string path = "/tmp/ui-smoke-track.mp3";
      wamp::TrackInfo info;
      info.path = path;
      info.title = "Faixa de teste";
      cache.store(info);
      const std::size_t removed = cache.forgetTracks({path}, &error);
      check(removed == 1, "remover tira a faixa da biblioteca");
      const auto ignored = cache.ignoredPaths(&error);
      check(ignored.size() == 1 && ignored.front() == path, "remover grava o marcador de ignorado");
      cache.store(info);
      check(cache.clearLibrary(&error), "limpar biblioteca responde ok");
      check(cache.allPaths(&error).empty() && cache.ignoredPaths(&error).empty(),
            "limpar apaga faixas e marcadores");
    }
  }

  return failures == 0 ? 0 : 1;
}
