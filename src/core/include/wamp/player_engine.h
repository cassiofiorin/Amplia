#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "wamp/audio_analyzer.h"
#include "wamp/types.h"

namespace wamp {

/// Motor de reprodução: dono do decoder, do ring buffer e da saída de áudio.
/// Toda a API é segura para chamar da thread da UI; os callbacks são disparados
/// na thread de controle e o chamador deve fazer a marshaling (Qt: QMetaObject).
class PlayerEngine {
 public:
  struct Callbacks {
    std::function<void()> onEnded;
    std::function<void(const std::string&)> onError;
    /// Troca de faixa sem interrupção do áudio (gapless): o engine já está tocando
    /// `track` e a UI só precisa acompanhar o índice da playlist.
    std::function<void(const TrackInfo&)> onTrackChanged;
  };

  PlayerEngine();
  ~PlayerEngine();

  PlayerEngine(const PlayerEngine&) = delete;
  PlayerEngine& operator=(const PlayerEngine&) = delete;

  void setCallbacks(Callbacks callbacks);

  /// Define a faixa que entra sem gap quando a atual terminar ("" = nenhuma).
  /// A UI recalcula isso a cada mudança de faixa, de fila ou de sorteio.
  void setNextTrack(const std::string& path);

  /// Abre o arquivo e deixa em Paused. Retorna false e preenche lastError() se falhar.
  bool load(const std::string& path);

  void play();
  void pause();
  void togglePlayPause();
  void stop();
  void seek(std::int64_t positionMs);

  PlaybackState state() const;
  std::int64_t positionMs() const;
  std::int64_t durationMs() const;
  TrackInfo track() const;
  std::string lastError() const;

  /// 0.0 a 1.0. Aplicado na thread de áudio com suavização (ARCHITECTURE §4).
  void setVolume(float volume);

  /// ReplayGain: fator linear aplicado junto com o volume (1.0 = sem ganho).
  void setReplayGainFactor(double factor);
  double replayGainFactor() const;
  float volume() const;

  /// Equalizador de 10 bandas (D-044). Os ganhos são em dB, -12..+12.
  void setEqualizerEnabled(bool enabled);
  bool equalizerEnabled() const;
  void setEqualizerBands(const std::array<double, 10>& gainsDb);
  std::array<double, 10> equalizerBands() const;
  void setEqualizerPreamp(double preampDb);
  double equalizerPreamp() const;

  /// Amostras do que está saindo (pós-EQ), para o visualizador. A UI chama
  /// `analyzer().analyze(dt)` no ritmo da pintura; o áudio não espera por isso.
  AudioAnalyzer& analyzer();

  /// Quantos frames já foram empurrados para a saída e ainda não saíram na caixa.
  /// A UI usa isto para olhar o passado do analisador, não o futuro.
  std::int64_t analyzerLatencyFrames() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wamp
