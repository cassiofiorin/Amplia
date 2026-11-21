#include "wamp/audio_analyzer.h"

#include <algorithm>
#include <cmath>

namespace wamp {
namespace {

constexpr double kPi = 3.14159265358979323846;

void fftInPlace(std::vector<std::complex<float>>& data) {
  const std::size_t n = data.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(data[i], data[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double angle = -2.0 * kPi / static_cast<double>(len);
    const std::complex<float> step(static_cast<float>(std::cos(angle)),
                                   static_cast<float>(std::sin(angle)));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const std::complex<float> u = data[i + k];
        const std::complex<float> v = data[i + k + len / 2] * w;
        data[i + k] = u + v;
        data[i + k + len / 2] = u - v;
        w *= step;
      }
    }
  }
}

float toUnit(float value) { return std::clamp(value, 0.0f, 1.0f); }

/// Frequência central de cada banda: log de 25 Hz a 16 kHz, como o visualizador do
/// original (que usava barras proporcionais em oitavas).
float bandCenter(int index) {
  const double low = 25.0;
  const double high = 16000.0;
  const double t = (index + 0.5) / AudioAnalyzer::kBands;
  return static_cast<float>(low * std::pow(high / low, t));
}

}  // namespace

void AudioAnalyzer::setSampleRate(int hz) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (hz > 0) sampleRate_ = hz;
}

void AudioAnalyzer::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  resetLocked();
}

void AudioAnalyzer::resetLocked() {
  mono_.assign(kHistory, 0.0f);
  left_.assign(kHistory, 0.0f);
  right_.assign(kHistory, 0.0f);
  writeIndex_ = 0;
  pushedFrames_ = 0;
  smoothed_.fill(0.0f);
  levelL_ = 0.0f;
  levelR_ = 0.0f;
  peakL_ = 0.0f;
  peakR_ = 0.0f;
  peaks_.fill(0.0f);
  beat_ = 0.0f;
  lowAverage_ = 0.0f;
  sinceBeat_ = 1.0;
}

void AudioAnalyzer::push(const float* interleaved, int frames, int channels) {
  if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (mono_.size() != static_cast<std::size_t>(kHistory)) resetLocked();

  for (int i = 0; i < frames; ++i) {
    const float* frame = interleaved + static_cast<std::size_t>(i) * channels;
    float sum = 0.0f;
    for (int c = 0; c < channels; ++c) sum += frame[c];
    const std::size_t slot = static_cast<std::size_t>(writeIndex_ % kHistory);
    mono_[slot] = sum / static_cast<float>(channels);
    left_[slot] = frame[0];
    right_[slot] = channels > 1 ? frame[1] : frame[0];
    ++writeIndex_;
    ++pushedFrames_;
  }
}

std::int64_t AudioAnalyzer::pushedFrames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pushedFrames_;
}

AudioAnalyzer::Frame AudioAnalyzer::analyze(double dtSeconds, std::int64_t latencyFrames) {
  Frame out;
  const double dt = std::clamp(dtSeconds, 0.001, 1.0);

  std::vector<std::complex<float>> buffer(kFftSize);
  float sumL = 0.0f;
  float sumR = 0.0f;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mono_.size() != static_cast<std::size_t>(kHistory)) return out;
    // A janela termina no sample que está saindo agora, não no último que entrou.
    const std::int64_t maxLatency = kHistory - kFftSize - 1;
    const std::int64_t latency = std::clamp<std::int64_t>(latencyFrames, 0, maxLatency);
    const std::int64_t start = writeIndex_ - latency - kFftSize;
    for (int i = 0; i < kFftSize; ++i) {
      const std::int64_t at = start + i;
      if (at < 0) continue;
      const std::size_t slot = static_cast<std::size_t>(at % kHistory);
      const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFftSize - 1));
      buffer[i] = std::complex<float>(static_cast<float>(mono_[slot] * window), 0.0f);
      sumL += left_[slot] * left_[slot];
      sumR += right_[slot] * right_[slot];
    }
  }

  fftInPlace(buffer);

  const float binHz = static_cast<float>(sampleRate_) / static_cast<float>(kFftSize);
  const float reference = static_cast<float>(kFftSize) * 0.25f;  // ganho da janela de Hann

  for (int band = 0; band < kBands; ++band) {
    const float center = bandCenter(band);
    int lowBin = static_cast<int>(std::floor(center / std::pow(2.0f, 1.0f / 4.0f) / binHz));
    int highBin = static_cast<int>(std::ceil(center * std::pow(2.0f, 1.0f / 4.0f) / binHz));
    lowBin = std::max(1, lowBin);
    highBin = std::max(lowBin + 1, std::min(highBin, kFftSize / 2 - 1));

    float magnitude = 0.0f;
    for (int bin = lowBin; bin <= highBin; ++bin) {
      magnitude = std::max(magnitude, std::abs(buffer[static_cast<std::size_t>(bin)]));
    }
    const float db = 20.0f * std::log10(magnitude / reference + 1e-9f);
    const float unit = toUnit((db + 70.0f) / 70.0f);

    // Ataque instantâneo e queda com constante de tempo — igual em qualquer fps.
    smoothed_[band] = unit > smoothed_[band]
                          ? unit
                          : unit + (smoothed_[band] - unit) * static_cast<float>(std::exp(-dt / 0.12));
    peaks_[band] = std::max(smoothed_[band], peaks_[band] - static_cast<float>(dt * 0.6));
    out.bands[band] = smoothed_[band];
    out.peaks[band] = peaks_[band];
  }

  const float rmsL = std::sqrt(sumL / static_cast<float>(kFftSize));
  const float rmsR = std::sqrt(sumR / static_cast<float>(kFftSize));
  const float rawL = toUnit((20.0f * std::log10(rmsL + 1e-9f) + 60.0f) / 60.0f);
  const float rawR = toUnit((20.0f * std::log10(rmsR + 1e-9f) + 60.0f) / 60.0f);
  // Medidor com queda de "limitador": sobe na hora, desce com constante de tempo,
  // igual à queda das barras do espectro.
  levelL_ = rawL > levelL_ ? rawL : std::max(rawL, levelL_ - static_cast<float>(dt * 0.9));
  levelR_ = rawR > levelR_ ? rawR : std::max(rawR, levelR_ - static_cast<float>(dt * 0.9));
  peakL_ = std::max(levelL_, peakL_ - static_cast<float>(dt * 0.5));
  peakR_ = std::max(levelR_, peakR_ - static_cast<float>(dt * 0.5));
  out.levelL = levelL_;
  out.levelR = levelR_;
  out.peakL = peakL_;
  out.peakR = peakR_;

  // Batida: energia das bandas graves comparada com a média lenta delas mesmas.
  const float low = (out.bands[0] + out.bands[1] + out.bands[2]) / 3.0f;
  lowAverage_ = lowAverage_ + (low - lowAverage_) * static_cast<float>(dt / 0.35);
  sinceBeat_ += dt;
  if (low > lowAverage_ * 1.35f && low > 0.25f && sinceBeat_ > 0.22) {
    beat_ = 1.0f;
    sinceBeat_ = 0.0;
  } else {
    beat_ = std::max(0.0f, beat_ - static_cast<float>(dt * 4.0));
  }
  out.beat = beat_;

  return out;
}

}  // namespace wamp
