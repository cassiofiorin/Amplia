// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#include "wamp/track_analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#include "wamp/dsp_fft.h"

namespace wamp {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kEnvelopeRing = 4096;   // ~44 s de hops de 512 a 48 kHz
constexpr std::size_t kMonoRing = 8192;       // janela do croma (resolução de ~6 Hz)
constexpr int kWindowHops = 47;               // ~0,5 s entre análises
constexpr int kCorrelationHops = 1024;        // ~11 s de histórico
constexpr float kMinBpm = 60.0f;
constexpr float kMaxBpm = 200.0f;
constexpr float kBinStep = 0.5f;

// Perfis de Krumhansl–Kessler (1982), os mesmos usados por quase todo detector de
// tonalidade: quão típica cada classe de altura é numa tonalidade maior/menor.
constexpr double kMajorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                      2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                      2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double correlation(const double* a, const double* b) {
  double meanA = 0.0;
  double meanB = 0.0;
  for (int i = 0; i < 12; ++i) {
    meanA += a[i];
    meanB += b[i];
  }
  meanA /= 12.0;
  meanB /= 12.0;
  double num = 0.0;
  double denA = 0.0;
  double denB = 0.0;
  for (int i = 0; i < 12; ++i) {
    const double da = a[i] - meanA;
    const double db = b[i] - meanB;
    num += da * db;
    denA += da * da;
    denB += db * db;
  }
  if (denA <= 0.0 || denB <= 0.0) return 0.0;
  return num / std::sqrt(denA * denB);
}

}  // namespace

void TrackAnalyzer::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  envelope_.assign(kEnvelopeRing, 0.0f);
  envelopeWrite_ = 0;
  envelopeFilled_ = 0;
  hopSum_ = 0.0;
  hopCount_ = 0;
  hopsSinceWindow_ = 0;
  mono_.assign(kMonoRing, 0.0f);
  monoWrite_ = 0;
  tempoWeights_.assign(
      static_cast<std::size_t>((kMaxBpm - kMinBpm) / kBinStep) + 1, 0.0);
  tempoWindows_ = 0;
  std::fill(std::begin(chroma_), std::end(chroma_), 0.0);
  seconds_ = 0.0;
}

void TrackAnalyzer::setSampleRate(int hz) {
  std::lock_guard<std::mutex> lock(mutex_);
  sampleRate_ = hz > 0 ? hz : 48000;
  hopFrames_ = std::max<std::int64_t>(128, static_cast<std::int64_t>(sampleRate_ / 94));
}

void TrackAnalyzer::push(const float* interleaved, int frames, int channels) {
  if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
  std::lock_guard<std::mutex> lock(mutex_);

  seconds_ += static_cast<double>(frames) / static_cast<double>(sampleRate_);

  bool newHop = false;
  for (int i = 0; i < frames; ++i) {
    const float* frame = interleaved + static_cast<std::size_t>(i) * channels;
    float mono = 0.0f;
    for (int c = 0; c < channels; ++c) mono += frame[c];
    mono /= static_cast<float>(channels);

    mono_[monoWrite_] = mono;
    monoWrite_ = (monoWrite_ + 1) % kMonoRing;

    hopSum_ += std::fabs(static_cast<double>(mono));
    if (++hopCount_ >= hopFrames_) {
      envelope_[envelopeWrite_] = static_cast<float>(hopSum_ / static_cast<double>(hopCount_));
      envelopeWrite_ = (envelopeWrite_ + 1) % kEnvelopeRing;
      envelopeFilled_ = std::min(envelopeFilled_ + 1, kEnvelopeRing);
      hopSum_ = 0.0;
      hopCount_ = 0;
      newHop = true;
    }
  }

  // Uma janela a cada ~0,5 s de hops novos: dá tempo de acumular estatística sem
  // gastar CPU em cada bloco de áudio.
  if (newHop && ++hopsSinceWindow_ >= kWindowHops) {
    hopsSinceWindow_ = 0;
    analyzeWindow();
  }
}

void TrackAnalyzer::analyzeWindow() {
  if (envelopeFilled_ < static_cast<std::size_t>(kCorrelationHops)) return;

  // Histórico em ordem cronológica (o anel começa em envelopeWrite_).
  std::array<double, kCorrelationHops> onset{};
  double previous = 0.0;
  for (int i = 0; i < kCorrelationHops; ++i) {
    const std::size_t index = (envelopeWrite_ + kEnvelopeRing - kCorrelationHops + i) % kEnvelopeRing;
    const double value = envelope_[index];
    onset[i] = std::max(0.0, value - previous);  // ataque = aumento de energia
    previous = value;
  }

  const double hopSeconds = static_cast<double>(hopFrames_) / static_cast<double>(sampleRate_);
  int bestLag = 0;
  double bestValue = 0.0;
  for (int lag = 1; lag < kCorrelationHops / 2; ++lag) {
    const double bpm = 60.0 / (hopSeconds * static_cast<double>(lag));
    if (bpm < kMinBpm || bpm > kMaxBpm) continue;
    double num = 0.0;
    double denA = 0.0;
    double denB = 0.0;
    for (int i = lag; i < kCorrelationHops; ++i) {
      num += onset[i] * onset[i - lag];
      denA += onset[i] * onset[i];
      denB += onset[i - lag] * onset[i - lag];
    }
    if (denA <= 1e-9 || denB <= 1e-9) continue;
    const double value = num / std::sqrt(denA * denB);
    if (value > bestValue) {
      bestValue = value;
      bestLag = lag;
    }
  }
  if (bestLag > 0 && bestValue > 0.12) {
    const double bpm = 60.0 / (hopSeconds * static_cast<double>(bestLag));
    const std::size_t bin = static_cast<std::size_t>((bpm - kMinBpm) / kBinStep + 0.5);
    if (bin < tempoWeights_.size()) tempoWeights_[bin] += bestValue;
    ++tempoWindows_;
  }

  // Croma da janela mais recente.
  // 8192 amostras: com 1024 bins de ~47 Hz os harmônicos de um acorde caíam em classes
  // de altura erradas (Lá maior virava outra tonalidade no teste).
  std::vector<std::complex<float>> buffer(kMonoRing);
  std::array<double, 12> window{};
  const double binHz = static_cast<double>(sampleRate_) / static_cast<double>(buffer.size());
  for (int block = 0; block < 1; ++block) {
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      const std::size_t index = (monoWrite_ + kMonoRing - buffer.size() + i) % kMonoRing;
      const double windowValue = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                                      (static_cast<double>(buffer.size()) - 1.0));
      buffer[i] = std::complex<float>(static_cast<float>(mono_[index] * windowValue), 0.0f);
    }
    fftInPlace(buffer);
    for (std::size_t b = 2; b < buffer.size() / 2; ++b) {
      const double freq = static_cast<double>(b) * binHz;
      if (freq < 65.0 || freq > 2000.0) continue;
      const double magnitude = static_cast<double>(std::abs(buffer[b]));
      if (magnitude <= 0.0) continue;
      const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
      const int pitchClass = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
      window[pitchClass] += magnitude * magnitude;
    }
  }
  for (int i = 0; i < 12; ++i) chroma_[i] += window[i];
}

TrackAnalysis TrackAnalyzer::result() const {
  std::lock_guard<std::mutex> lock(mutex_);
  TrackAnalysis analysis;
  analysis.seconds = seconds_;
  analysis.valid = seconds_ >= 12.0;

  if (tempoWindows_ >= 2) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < tempoWeights_.size(); ++i) {
      if (tempoWeights_[i] > tempoWeights_[best]) best = i;
    }
    double total = 0.0;
    for (double weight : tempoWeights_) total += weight;
    if (total > 0.0 && tempoWeights_[best] / total > 0.2) {
      // Média ponderada da vizinhança: o pico de 0,5 BPM oscila entre janelas.
      double num = 0.0;
      double den = 0.0;
      for (std::size_t i = (best >= 2 ? best - 2 : 0); i <= std::min(best + 2, tempoWeights_.size() - 1);
           ++i) {
        num += (kMinBpm + static_cast<double>(i) * kBinStep) * tempoWeights_[i];
        den += tempoWeights_[i];
      }
      if (den > 0.0) analysis.bpm = static_cast<float>(num / den);
    }
  }

  double chromaSum = 0.0;
  for (double value : chroma_) chromaSum += value;
  if (analysis.valid && chromaSum > 0.0) {
    double normalized[12];
    for (int i = 0; i < 12; ++i) normalized[i] = chroma_[i] / chromaSum;
    double bestScore = -2.0;
    int bestKey = -1;
    int bestMajor = 0;
    for (int root = 0; root < 12; ++root) {
      double rotated[12];
      for (int i = 0; i < 12; ++i) rotated[i] = normalized[(root + i) % 12];
      const double major = correlation(rotated, kMajorProfile);
      if (major > bestScore) {
        bestScore = major;
        bestKey = root;
        bestMajor = 1;
      }
      const double minor = correlation(rotated, kMinorProfile);
      if (minor > bestScore) {
        bestScore = minor;
        bestKey = root;
        bestMajor = 0;
      }
    }
    analysis.keyPitchClass = bestKey;
    analysis.keyMajor = bestMajor;
  }
  return analysis;
}

}  // namespace wamp
