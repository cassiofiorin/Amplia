// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace wamp {

/// Resultado da análise local de uma faixa (BPM e tonalidade).
struct TrackAnalysis {
  bool valid = false;      ///< áudio suficiente para valer a pena gravar
  float bpm = 0.0f;        ///< 0 = não deu para estimar
  int keyPitchClass = -1;  ///< 0 = C … 11 = B; -1 = desconhecida
  int keyMajor = 0;        ///< 1 = maior, 0 = menor
  double seconds = 0.0;    ///< quanto áudio já entrou
};

/// Análise "preguiçosa": consome o PCM que já passa pelo decode, sem abrir o arquivo de
/// novo. Roda na thread de controle (barato por bloco) e entrega o resultado quando a UI
/// pedir — é o que permite analisar só o que toca, sem varrer a biblioteca.
///
/// - **BPM**: envelope de energia em janelas de ~10 ms, autocorrelação das diferenças
///   positivas (ataques) para 60–200 BPM, acumulada num histograma entre janelas e
///   suavizada no fim. É o mesmo princípio dos detectores simples, sem custo de FFT por
///   bloco.
/// - **Tonalidade**: croma por FFT (4096 amostras, janela de Hann) e correlação com os
///   perfis de Krumhansl–Kessler (maior e menor, 12 rotações).
class TrackAnalyzer {
 public:
  void reset();
  void setSampleRate(int hz);
  void push(const float* interleaved, int frames, int channels);
  TrackAnalysis result() const;

 private:
  void analyzeWindow();

  mutable std::mutex mutex_;
  int sampleRate_ = 48000;
  std::int64_t hopFrames_ = 512;

  // Envelope de energia (um valor por hop), em anel.
  std::vector<float> envelope_;
  std::size_t envelopeWrite_ = 0;
  std::size_t envelopeFilled_ = 0;
  double hopSum_ = 0.0;
  int hopCount_ = 0;
  int hopsSinceWindow_ = 0;

  // Amostras mono recentes para o croma (anel de 4096).
  std::vector<float> mono_;
  std::size_t monoWrite_ = 0;

  // Histograma de BPM acumulado (60..200 em passos de 0,5).
  std::vector<double> tempoWeights_;
  int tempoWindows_ = 0;
  double chroma_[12] = {};
  double seconds_ = 0.0;
};

}  // namespace wamp
