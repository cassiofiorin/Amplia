#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <mutex>
#include <vector>

namespace wamp {

/// Amostras do áudio que está saindo, para o visualizador da F2C.
///
/// A thread de controle empurra os blocos depois do EQ (é o sinal que o ouvinte
/// escuta) e a UI lê a janela mais recente quando quiser pintar — 15 vezes por
/// segundo por padrão. O `push` só faz uma soma mono e um memcpy curto; a análise
/// (FFT) roda inteira na thread da UI.
class AudioAnalyzer {
 public:
  static constexpr int kFftSize = 1024;
  static constexpr int kBands = 20;
  static constexpr int kHistory = 144000;  // 3 s a 48 kHz: o ring cheio (2 s) cabe
                                           // com folga para a janela da FFT

  struct Frame {
    std::array<float, kBands> bands{};   ///< 0..1, já suavizado
    std::array<float, kBands> peaks{};   ///< 0..1, segura o topo
    float levelL = 0.0f;                 ///< RMS do canal esquerdo, 0..1
    float levelR = 0.0f;
    float peakL = 0.0f;                  ///< topo segurado do medidor
    float peakR = 0.0f;
    float beat = 0.0f;                   ///< 0..1, pulso que decai
  };

  /// A thread de controle chama isto com o bloco intercalado que acabou de decodificar.
  void push(const float* interleaved, int frames, int channels);

  /// A UI chama isto no ritmo da pintura. `dtSeconds` mantém a suavização igual em
  /// qualquer taxa de quadros. `latencyFrames` é quanto o som empurrado está à frente do
  /// que já saiu na caixa — sem isso o espectro mostra o futuro do ring (F2C).
  Frame analyze(double dtSeconds, std::int64_t latencyFrames = 0);

  /// Quantas amostras já entraram desde `reset()` (monotônico, em frames de saída).
  std::int64_t pushedFrames() const;

  void reset();
  void setSampleRate(int hz);

 private:
  void resetLocked();

  mutable std::mutex mutex_;
  std::vector<float> mono_;
  std::vector<float> left_;
  std::vector<float> right_;
  std::int64_t writeIndex_ = 0;
  std::int64_t pushedFrames_ = 0;
  int sampleRate_ = 48000;

  std::array<float, kBands> smoothed_{};
  std::array<float, kBands> peaks_{};
  float levelL_ = 0.0f;
  float levelR_ = 0.0f;
  float peakL_ = 0.0f;
  float peakR_ = 0.0f;
  float beat_ = 0.0f;
  float lowAverage_ = 0.0f;
  double sinceBeat_ = 1.0;
};

}  // namespace wamp
