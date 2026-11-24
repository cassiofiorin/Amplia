#pragma once

#include <chrono>

#include <QColor>
#include <QVector>
#include <QMainWindow>

#include "click_slider.h"
#include "display_panel.h"
#include "theme.h"

#include <cstddef>
#include <map>
#include <set>
#include <functional>
#include <memory>

#include <string>

#include "wamp/audio_analyzer.h"
#include "wamp/tag_cache.h"
#include "wamp/types.h"

class PlaylistDelegate;
class QAction;
class QActionGroup;
class QLabel;
class QListWidget;
class QPushButton;

namespace wamp::ui {
class LibraryWindow;
class RoundButton;
}
class QMenu;
class QSlider;
class QSystemTrayIcon;
class ThemeDialog;
class QTimer;
class QVBoxLayout;


namespace wamp::ui {
class DisplayPanel;
class EqualizerDialog;
class JumpDialog;
class FileInfoDialog;
}

namespace wamp {
class MetadataScanner;
class TagCache;
class MprisServer;
class PlayerEngine;
class Playlist;
class Scrobbler;
}

/// Transporte + editor de playlist. A skin clássica é assunto da F5; aqui interessa
/// o comportamento: fila, próxima/anterior e a playlist sobrevivendo à reprodução.
class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr, bool restoreSession = true);
  ~MainWindow() override;

  /// Abre e toca um arquivo. Também usado pelo argumento de linha de comando.
  void openFile(const QString& path);

  /// Lê .m3u/.pls/.b4s/.wpl e acrescenta as faixas à playlist.
  bool appendPlaylistFile(const QString& path, std::string* error);

  /// Adiciona arquivos à playlist sem tocar (demais argumentos da linha de comando).
  void addFiles(const QStringList& paths);

  /// Abre direto com o editor de playlist visível (`--playlist` na linha de comando).
  void showPlaylist(bool visible);

  /// Instalado pelo `main`: recria a janela no mesmo processo (troca de idioma). Chamado
  /// depois de `saveSettings()`, então o estado já está no disco.
  void setRestartHandler(std::function<void()> handler);

  /// Grava a INI na hora (o reinício da interface chama antes de trocar de janela).
  void saveSettingsNow();

  /// Recria a janela com o idioma da INI (o mesmo caminho do botão "Reiniciar agora").
  void restartForLanguage();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void showEvent(QShowEvent* event) override;
  /// Agenda a abertura da playlist pedida pela INI (idempotente).
  void schedulePendingPlaylistOpen();
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void buildUi();
  void buildMenus();
  void wireShortcuts();

  void openFileDialog();
  void addFilesDialog();
  void loadPlaylistDialog();
  void savePlaylistDialog();
  void newPlaylist();
  void addFolderDialog();
  void selectAllItems();
  void selectNone();
  void invertSelection();
  void cropSelected();
  void clearPlaylist();
  void removeMissing();
  void sortPlaylist(int mode);
  /// Jump to File (tecla ","): busca na playlist com o que já se sabe das faixas.
  void openJumpToFile();
  void jumpPlay(const QString& path);
  void jumpQueue(const QString& path);
  /// File Info (Alt+3): abre as tags da faixa atual/selecionada.
  void openFileInfo();
  void onFileSaved(const QString& path);
  void reverseList();
  void shuffleList();
  /// Ordenar exige metadados de todos: se a fila ainda está lendo, avisa e não mexe.
  bool metadataReady();
  // autoplay=false carrega a faixa e deixa em pausa (usado ao organizar a lista).
  bool openPath(const QString& path, bool autoplay = true);
  void togglePlayPause();

  void addPaths(const QStringList& paths);
  /// Explica por que o arquivo não toca; só em abertura explícita de um arquivo.
  void warnUnsupportedPath(const QString& path);
  void playIndex(std::size_t index);
  void playNext();
  void playPrevious();
  /// Próxima faixa respeitando fila, sorteio e fim de lista.
  std::size_t nextTrackIndex();
  /// Decide a próxima faixa uma vez por troca e entrega ao engine (gapless).
  void updateNextUp();
  /// Transição gapless já audível: acompanha a playlist sem recarregar nada.
  void applyGaplessTrackChange(const QString& path);
  void toggleShuffle();
  /// Repetir a lista inteira (o mesmo botão do agente do Winamp).
  void toggleRepeat();
  void toggleMute();
  void toggleTimeMode();
  void setPlaylistFontSize(int px);
  void jumpTracks(int delta);
  void toggleAlwaysOnTop();
  void toggleStopAfter();
  void openLocation();
  void focusPlaylist();
  void focusEqualizer();
  void cycleWindows();
  void applyEasyMove(bool on);
  void setRepeatMode(int mode);
  /// Ícone na bandeja: o "agente" — fecha para lá e continua tocando.
  void buildTray();
  void updateTray();
  Q_INVOKABLE void openPreferences();
  /// Janela do equalizador (Alt+G): a mudança vale na hora, o áudio não para.
  void openEqualizer();
  void applyEqualizer();
  void updateVisual();
  void applyVisualSettings();
  void calibrateVisual(class PreferencesDialog* dialog);
  void applyReplayGain(const wamp::TrackInfo& track);
  void setReplayGainMode(int mode);
  void loadEqualizer();
  void saveEqualizer();
  void toggleQueue(int row);
  void showListMenu(const QPoint& globalPos);
  void loadSettings();
  void applyThemeFromSettings();
  void setTheme(const QString& id);
  void openThemeDialog();
  void applyCustomTheme(const QColor& background, const QColor& accent, const QColor& secondary,
                        const QColor& buttons, int contrastPercent);
  void saveCustomTheme(const QColor& background, const QColor& accent,
                       const QColor& secondary, const QColor& buttons, int contrastPercent);
  void syncThemeChecks();
  void refreshThemeDialog();
  void applyThemeById(const QString& id);
  void saveThemeSample();
  void importThemes();
  void exportThemes();
  void saveCurrentThemeAs();
  QColor customBackground() const;
  QColor customAccent() const;
  QColor customSecondary() const;
  QColor customButtons() const;
  int customContrast() const;
  void beginScrobble(const wamp::TrackInfo& track);
  void finishScrobble();
  void onPlaybackTick();
  /// Título da janela = faixa tocando (o painel do KDE mostra o título da janela).
  void updateWindowTitle();
  /// Capa embutida → arquivo em ~/.cache para o `mpris:artUrl` do applet.
  void updateArtwork(const wamp::TrackInfo& track);
  void saveSettings();
  void queueSelected();
  void removeSelected();
  void onTrackEnded();
  /// Arquivos soltos na janela ou na lista: adiciona, ou toca se a lista está vazia.
  void handleDroppedPaths(const QStringList& paths);

  void rebuildPlaylist();
  void refreshQueue();
  void onMetadata(const std::string& path, const wamp::TrackInfo& info, bool ok);
  /// Contador fixo da fila de metadados na barra de status.
  void updateMetadataStatus();
  /// Grava o estado 700 ms depois do último movimento/redimensionamento. Sem isso, um
  /// fechamento pela bandeja, um kill ou uma queda voltam com a janela no lugar antigo.
  void scheduleSettingsSave();
  /// Reinício de verdade (novo processo): a plataforma só pode ser trocada antes do
  /// QApplication, então reconstruir a janela no mesmo processo não mudaria nada.
  void restartWithPlatformChange();
  void persistXwaylandChoice(bool value);
  void armPlaycount(const std::string& path);
  void resumePendingScan();
  void openLibrary();
  void refreshLibrary();
  void saveTrackAnalysis();
  QString librarySummary(const std::vector<wamp::LibraryEntry>& rows) const;
  void forgetLibraryTracks(const QStringList& paths);
  void clearLibrary();
  void rescanLibrary();
  void addLibraryFolder();
  void playFromLibrary(const QString& path);

  /// Rádio local: escolhe 20 faixas a partir da semente (faixa tocando, senão a indicada)
  /// e enfileira no fim da playlist — nunca substitui a lista do usuário.
  void startRadioStation(const QString& seedPath);

  /// Semente composta: a faixa (tocando ou indicada) mais o centroide das últimas 5
  /// tocadas, na proporção de `radio/seedBlend`. `pool` recebe as candidatas quando vem.
  wamp::RadioMix stationMix(const QString& seedPath,
                            std::vector<wamp::RadioCandidate>* pool = nullptr);

  /// Prévia do nó "Rádio local": as mesmas 20 faixas que a estação escolheria agora.
  std::vector<wamp::LibraryEntry> stationRows();

  /// Humor: a estação de um ponto do plano (agitação × tom), com a mistura do histórico.
  void refreshHumorPresets();
  void onHumorPointChanged(bool dirty);
  void applyHumorStation(bool autoplay);
  void startHumorStation(bool autoplay);
  void saveHumorPreset();
  /// Núcleo do "Salvar como…": o diálogo pergunta o nome, os testes chamam direto.
  bool saveHumorPresetNamed(const QString& name);
  void removeHumorPreset(int presetId);
  /// Remove sem confirmar (a confirmação é da UI; os testes chamam direto).
  void removeHumorPresetNow(int presetId);
  /// Mistura com o ponto atual: `hasHumor`, o léxico do preset aberto e o histórico.
  wamp::RadioMix humorMix();
  /// Prévia do nó de humor: as 20 faixas que a estação escolheria agora.
  std::vector<wamp::LibraryEntry> humorRows();

  /// Hook de teste (`AMPLIA_HUMOR_TEST=Nome`): imprime as escolhas de um preset de clima,
  /// com a pontuação e as leituras de agitação/tom. Serve para calibrar a tabela sem UI.
  void humorTestDump(const QString& presetName);

  /// Registra pulo quando a faixa atual é interrompida cedo (ver D-116).
  void recordSkipIfEarly(std::size_t nextIndex);

  /// Uma fila só, alimentada por qualquer origem: playlist (0), biblioteca (1) e
  /// estação/rádio (2). Os itens não entram na playlist.
  struct QueueEntry {
    std::string path;
    int origin = 0;
    std::string seed;
  };
  std::vector<QueueEntry> pathQueue_;
  QWidget* queuePanel_ = nullptr;
  QLabel* queueHeader_ = nullptr;
  QListWidget* queueList_ = nullptr;
  QPushButton* queueToggle_ = nullptr;
  bool queuePanelUserHidden_ = false;

  /// Acrescenta à fila (sem tocar na playlist) e atualiza o bloco/contagem.
  void enqueuePaths(const QStringList& paths, int origin, const std::string& seed = {});

  /// Bloco "Na fila": mostra/esconde e redesenha a partir de pathQueue_.
  void toggleQueuePanel();
  void refreshQueuePanel();

  /// Remove da fila; quando veio da rádio, grava o evento de recusa (kind=2).
  void rejectQueuedItem(std::size_t index);
  QString rowLabel(const std::string& path, bool current) const;
  QString durationText(const std::string& path) const;
  void startReading();
  /// Metadados do cache em disco, se o arquivo não mudou desde a última leitura.
  bool cachedInfo(const std::string& path, wamp::TrackInfo* info);
  bool cachedInfoFast(const std::string& path, wamp::TrackInfo* info);
  /// Só as faixas que o cache não tem — são essas que a fila precisa ler.
  std::vector<std::string> missesFor(const std::vector<std::string>& paths);
  void setMissing(const std::string& path, bool missing);
  void setPlaylistVisible(bool visible);

  /// Onde a playlist mora: "integrada" (painel da janela principal, padrão) ou "externa"
  /// (janela própria). A troca vale na hora e a lista não é recriada.
  void setPlaylistMode(const QString& mode);
  bool playlistIntegrated() const;
  void showError(const QString& message);

  void refresh();

  std::unique_ptr<wamp::PlayerEngine> engine_;
  std::unique_ptr<wamp::Playlist> playlist_;
  std::unique_ptr<wamp::MetadataScanner> scanner_;
  std::unique_ptr<wamp::ui::EqualizerDialog> eqDialog_;
  QTimer* saveStateTimer_ = nullptr;
  QTimer* playcountTimer_ = nullptr;
  std::string playcountPath_;
  std::string analysedPath_;  ///< último caminho com BPM/tonalidade gravados
  QTimer* eqSaveTimer_ = nullptr;
  QTimer* visualTimer_ = nullptr;
  bool visualEnabled_ = true;
  int visualFps_ = 15;
  wamp::AudioAnalyzer::Frame lastVisual_{};  ///< último quadro bom, para congelar/desvanecer
  bool haveVisual_ = false;
  double visualCostP95Ms_ = 0.0;
  double visualCpuPercent_ = 0.0;
  std::chrono::steady_clock::time_point visualLast_{};
  std::unique_ptr<wamp::TagCache> cache_;
  /// MPRIS2: nome no barramento de sessão para o controle de mídia do sistema.
  std::unique_ptr<wamp::MprisServer> mpris_;
  std::unique_ptr<wamp::Scrobbler> scrobbler_;

  /// Arquivo local da capa da faixa atual ("" = sem capa).
  std::string artPath_;

  /// Faixa que o engine vai emendar quando a atual acabar ("" = nenhuma).
  std::string nextUpPath_;
  bool nextUpFromQueue_ = false;

  /// Tags já lidas, por caminho: `rebuildPlaylist()` desenha a lista sem esperar a fila.
  std::map<std::string, wamp::TrackInfo> metadata_;
  /// Caminhos que não abriram ao tocar: linha ganha o aviso e é pulada.
  std::set<std::string> missingFiles_;
  std::size_t metadataTotal_ = 0;
  class QLabel* queueLabel_ = nullptr;
  class QLabel* versionLabel_ = nullptr;

  wamp::ui::DisplayPanel* display_ = nullptr;
  wamp::ui::RoundButton* playButton_ = nullptr;
  wamp::ui::RoundButton* pauseButton_ = nullptr;
  wamp::ui::RoundButton* stopButton_ = nullptr;
  wamp::ui::RoundButton* prevButton_ = nullptr;
  wamp::ui::RoundButton* nextButton_ = nullptr;
  wamp::ui::RoundButton* mlButton_ = nullptr;
  wamp::ui::RoundButton* playlistButton_ = nullptr;
  wamp::ui::RoundButton* shuffleButton_ = nullptr;
  wamp::ui::RoundButton* repeatButton_ = nullptr;
  wamp::ui::RoundButton* muteButton_ = nullptr;
  int lastVolume_ = 80;
  /// Play com a faixa andando volta ao início (comportamento do original).
  bool restartOnPlay_ = true;
  QAction* shuffleAction_ = nullptr;
  QAction* showPathAction_ = nullptr;
  QAction* actNewPlaylist_ = nullptr;
  QAction* actOpenPlaylist_ = nullptr;
  QAction* actAddFiles_ = nullptr;
  QAction* actAddFolder_ = nullptr;
  QAction* actLibrary_ = nullptr;
  std::unique_ptr<wamp::ui::LibraryWindow> library_;
  std::vector<std::string> libraryRoots_;
  QAction* actSavePlaylist_ = nullptr;
  wamp::ui::JumpDialog* jumpDialog_ = nullptr;
  wamp::ui::ClickSlider* volumeSlider_ = nullptr;
  QListWidget* list_ = nullptr;
  PlaylistDelegate* delegate_ = nullptr;
  QWidget* playlistWindow_ = nullptr;
  QWidget* playlistPanel_ = nullptr;
  class QVBoxLayout* mainLayout_ = nullptr;
  QString playlistMode_ = QStringLiteral("integrada");
  int integratedHeight_ = 300;
  bool playlistOpen_ = false;

  /// Reencaixa o painel da playlist no dono do modo atual (principal ou janela própria).
  void applyPlaylistMode();
  /// O editor vive em janela própria (como no Winamp): a principal não muda de tamanho.
  QTimer* timer_ = nullptr;
  bool remaining_ = false;
  bool playlistAutoShown_ = false;
  bool restoreSession_ = true;
  /// Só grava a lista no fim se ela veio da sessão ou foi mexida nesta execução.
  bool sessionRestored_ = true;
  bool playlistDirty_ = false;
  bool showPath_ = false;
  int playlistFontPx_ = 11;
  /// 0 = a estação parte só da faixa; 100 = só do que venho ouvindo (`radio/seedBlend`).
  int seedBlend_ = 0;
  /// Humor aplicado (não o rascunho dos sliders): ponto, léxico do preset e mistura.
  wamp::HumorPoint humorPoint_;
  std::string humorGenres_;
  double humorBlend_ = 0.5;
  bool stopAfterCurrent_ = false;
  bool easyMove_ = false;
  bool libraryReopen_ = false;
  qint64 metadataBatchStartMs_ = 0;  ///< início do lote atual (para o ETA)
  qint64 metadataLastMs_ = 0;
  qint64 metadataLastDone_ = 0;
  double metadataRate_ = 0.0;        ///< faixas por segundo (média móvel)  ///< reabrir a Biblioteca ao iniciar (D-112)
  QAction* stopAfterAction_ = nullptr;
  QAction* easyMoveAction_ = nullptr;
  /// 0 = desligado, 1 = por faixa, 2 = por álbum.
  int replayGainMode_ = 0;
  bool settingsLoaded_ = false;
  // Verdadeiro entre "desligar a posição" e o restart: o save que o restart dispara não
  // pode regravar a geometria que a opção acabou de apagar.
  bool forgetGeometry_ = false;
  // Estado da playlist no momento de fechar: a janela auxiliar é fechada ANTES de
  // salvar, então ler isVisible() ali sempre daria false.
  bool playlistOpenAtExit_ = false;
  bool pendingPlaylistVisible_ = false;
  bool playlistShowScheduled_ = false;
  // Durante o fechamento a auxiliar já foi escondida, mas o estado que vale é o de antes.
  bool closing_ = false;
  wamp::TrackInfo scrobbleTrack_;
  qint64 scrobbleStartUnix_ = 0;
  std::int64_t scrobblePlayedMs_ = 0;
  std::int64_t scrobbleLastPos_ = 0;
  bool scrobbleSent_ = false;
  /// Altura da janela com a playlist fechada (medida uma vez, no layout compacto).
  int compactHeight_ = 178;
  bool scrobbleAnnounced_ = false;
  std::unique_ptr<QSystemTrayIcon> tray_;
  std::unique_ptr<QMenu> trayMenu_;
  QActionGroup* themeGroup_ = nullptr;
  QAction* customThemeAction_ = nullptr;
  QVector<wamp::ui::Theme> savedThemes_;
  ThemeDialog* themeDialog_ = nullptr;
  wamp::ui::Theme themeBeforeEditor_;
  QAction* repeatAction_ = nullptr;
  QAction* visualAction_ = nullptr;
  QAction* trayPlayAction_ = nullptr;
  QAction* trayNextAction_ = nullptr;
  QAction* trayPrevAction_ = nullptr;
  QAction* trayShuffleAction_ = nullptr;
  QAction* trayRepeatAction_ = nullptr;
  QAction* trayShowAction_ = nullptr;
  // 0 = desligado, 1 = repetir a lista, 2 = repetir a faixa.
  int repeatMode_ = 0;
  bool repeatActive() const { return repeatMode_ != 0; }
  /// Fechar a janela esconde para a bandeja em vez de encerrar.
  bool closeToTray_ = false;
  /// "Sair" do menu da bandeja: deixa o closeEvent encerrar de verdade.
  bool quitting_ = false;
  std::function<void()> restartHandler_;
  bool trayNoticeShown_ = false;
  bool shuffle_ = false;
  /// Nível do sorteio, 0..1 (o "morph rate" do Winamp).
  double shuffleLevel_ = 0.5;
};
