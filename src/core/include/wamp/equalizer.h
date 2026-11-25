#pragma once

#include <array>
#include <atomic>
#include <mutex>

namespace wamp {

/// Equalizador de 10 bandas no estilo do Winamp: as mesmas bandas do EQ10 (ressonador
/// somado à entrada, com larguras diferentes para reforço e corte), em float, mais
/// pré-amplificação e o limitador dinâmico do original. Roda no caminho de decode, antes do ring buffer —
/// o mesmo ponto onde o gapless já mexe, e por isso o EQ veio antes da visualização.
///
/// A thread de áudio nunca bloqueia: ela tenta pegar a tabela de coeficientes nova com
/// `try_lock` e, se a UI estiver no meio de um ajuste, continua com a anterior.
class Equalizer {
 public:
  /// Um biquad da cascata (coeficientes já normalizados por a0).
  struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    /// |H| em dB na frequência pedida — é o que a curva desenhada mostra.
    double magnitudeDb(double frequencyHz, double sampleRate) const;
  };

  static constexpr int kBands = 10;
  /// Centros de banda do Winamp original.
  static constexpr std::array<double, kBands> kFrequencies = {70.0,   180.0,  320.0,  600.0,
                                                              1000.0, 3000.0, 6000.0, 12000.0,
                                                              14000.0, 16000.0};
  /// Mesmo limitador do EQ10 do Winamp: pico com queda lenta, teto em −0,6 dB.
  static constexpr double kLimiterThreshold = 0.930;
  static constexpr double kLimiterReleaseSeconds = 0.700;
  static constexpr double kMinGainDb = -12.0;
  static constexpr double kMaxGainDb = 12.0;

  Equalizer();

  void setSampleRate(double sampleRate);
  void setEnabled(bool enabled);
  bool enabled() const;

  /// Ganhos em dB (-12..+12), na ordem de `kFrequencies`.
  void setGains(const std::array<double, kBands>& gainsDb);
  std::array<double, kBands> gains() const;
  void setPreamp(double preampDb);
  double preamp() const;

  /// Zera o estado dos filtros; chamar em seek, troca de faixa e stop.
  /// A UI chama da thread principal, o `process()` roda na thread de controle:
  /// o pedido fica numa flag atomica e e atendido pela thread de audio.
  void reset();

  /// Processa amostras intercaladas, in-place.
  void process(float* samples, int frames, int channels);

  /// Coeficientes da banda no formato do EQ10: ressonador somado à entrada.
  /// Público porque a janela do equalizador desenha a resposta com os mesmos números.
  static Biquad bandBiquad(double sampleRate, double centerHz, double gainDb);

  /// Resposta em dB da cascata das dez bandas em `frequencyHz` (sem pré-amplificação).
  static double responseDb(const std::array<double, kBands>& gainsDb, double sampleRate,
                           double frequencyHz);

 private:
  struct Coefs {
    std::array<Biquad, kBands> bands{};
    std::array<bool, kBands> active{};
    double preamp = 1.0;
  };

  void rebuild(const std::array<double, kBands>& gainsDb, double preampDb);

  std::atomic<bool> resetRequested_{false};
  mutable std::mutex mutex_;  // protege `pending_`
  Coefs pending_;
  Coefs ready_;               // cópia da thread de áudio
  bool readyValid_ = false;

  std::array<double, kBands> gains_{};
  double limiterDetect_ = 0.0;
  double limiterDecay_ = 0.999;
  double preampDb_ = 0.0;
  double sampleRate_ = 48000.0;
  std::atomic<bool> enabled_{false};  // UI escreve, thread de áudio lê

  // Estado dos filtros por banda e por canal (até estéreo, como o resto do app).
  std::array<std::array<double, 2>, kBands> z1_{};
  std::array<std::array<double, 2>, kBands> z2_{};
};

}  // namespace wamp
