#pragma once

#include <QDialog>
#include <memory>

#include "wamp/scrobble_types.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

namespace wamp {
class Scrobbler;
}

/// Preferências do player: sorteio (ligado/desligado e nível, o "shuffle morph rate" do
/// Winamp) e scrobbling (Last.fm e ListenBrainz).
class PreferencesDialog final : public QDialog {
  Q_OBJECT

 public:
  PreferencesDialog(bool shuffle, int level, bool closeToTray, bool restartOnPlay,
                    bool visualEnabled, int visualFps, double costP95Ms, double cpuPercent,
                    const wamp::ScrobbleConfig& scrobble, const QString& language,
                    QWidget* parent = nullptr);
  ~PreferencesDialog() override;

  bool shuffleEnabled() const;

  /// Fechar a janela esconde para a bandeja (o "agente") em vez de encerrar.
  bool closeToTray() const;
  bool restartOnPlay() const;
  int shuffleLevel() const;

  /// Visualizador: ligado e quantos quadros por segundo.
  bool visualEnabled() const;
  int visualFps() const;
  double visualCostP95Ms() const;
  double visualCpuPercent() const;

  wamp::ScrobbleConfig scrobbleConfig() const;

  /// Idioma escolhido ("sistema", "pt_BR", "en"). Aplicar exige reabrir o app: o Qt não
  /// retraduz widgets que já nasceram.
  QString language() const;

 signals:
  /// O botão "Calibrar…" pede a medição; quem mede é a janela, que tem o painel de verdade.
  void calibrateRequested();

 public slots:
  /// Resultado da calibração. O consumo em CPU é derivado do fps atual — o custo de desenhar
  /// um quadro não muda com a taxa, mas quantos quadros por segundo cabem nele muda.
  void setCalibrationResult(int fps, double costP95Ms);

 private:
  void updateLevelLabel();
  void updateScrobbleState();
  void connectLastfm();
  void testListenbrainz();

  class QComboBox* languageBox_ = nullptr;
  QCheckBox* shuffleBox_ = nullptr;
  QCheckBox* trayBox_ = nullptr;
  QCheckBox* restartBox_ = nullptr;
  QSlider* levelSlider_ = nullptr;
  QLabel* levelLabel_ = nullptr;
  QLabel* hintLabel_ = nullptr;

  QCheckBox* nowPlayingBox_ = nullptr;
  QCheckBox* lastfmBox_ = nullptr;
  QLineEdit* lastfmKey_ = nullptr;
  QLineEdit* lastfmSecret_ = nullptr;
  QLabel* lastfmStatus_ = nullptr;
  QPushButton* lastfmButton_ = nullptr;
  QString lastfmToken_;       // token pendente entre "Conectar" e "Já autorizei"
  std::string lastfmSession_; // session key já obtida nesta sessão do diálogo

  QCheckBox* listenbrainzBox_ = nullptr;
  QLineEdit* listenbrainzToken_ = nullptr;
  QLabel* listenbrainzStatus_ = nullptr;

  QCheckBox* visualBox_ = nullptr;
  QSlider* fpsSlider_ = nullptr;
  QLabel* fpsLabel_ = nullptr;
  QLabel* calibrationStatus_ = nullptr;
  double costP95Ms_ = 0.0;

  std::unique_ptr<wamp::Scrobbler> net_;  // só para autenticar/testar, não envia scrobbles
};
