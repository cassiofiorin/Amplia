#include "wamp/equalizer.h"

#include <algorithm>
#include <cmath>

namespace wamp {
namespace {
constexpr double kPi = 3.14159265358979323846;
/// Q global do EQ10; o reforço usa o dobro e o corte a metade.
constexpr double kBandQ = 1.41;
/// Abaixo disto a banda não muda nada audível e pode ser pulada (economia no caso flat).
constexpr double kActiveThresholdDb = 0.05;
}  // namespace

double Equalizer::Biquad::magnitudeDb(double frequencyHz, double sampleRate) const {
  const double rate = std::clamp(sampleRate, 4000.0, 384000.0);
  const double w = 2.0 * kPi * std::clamp(frequencyHz, 1.0, rate * 0.4999) / rate;
  // H(e^jw) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), com z^-1 = e^-jw.
  const double cos1 = std::cos(w);
  const double sin1 = std::sin(w);
  const double cos2 = std::cos(2.0 * w);
  const double sin2 = std::sin(2.0 * w);

  const double numRe = b0 + b1 * cos1 + b2 * cos2;
  const double numIm = -(b1 * sin1 + b2 * sin2);
  const double denRe = 1.0 + a1 * cos1 + a2 * cos2;
  const double denIm = -(a1 * sin1 + a2 * sin2);

  const double num = std::hypot(numRe, numIm);
  const double den = std::hypot(denRe, denIm);
  if (den <= 0.0 || num <= 0.0) return -120.0;
  return 20.0 * std::log10(num / den);
}

double Equalizer::responseDb(const std::array<double, kBands>& gainsDb, double sampleRate,
                             double frequencyHz) {
  double total = 0.0;
  for (int band = 0; band < kBands; ++band) {
    const double gain = gainsDb[static_cast<std::size_t>(band)];
    if (std::fabs(gain) < 0.05) continue;  // banda em 0 dB não muda a resposta
    const Biquad coef = bandBiquad(sampleRate, kFrequencies[static_cast<std::size_t>(band)], gain);
    total += coef.magnitudeDb(frequencyHz, sampleRate);
  }
  return total;
}

Equalizer::Equalizer() {
  limiterDecay_ = std::pow(0.001, 1.0 / (sampleRate_ * kLimiterReleaseSeconds));
  rebuild(gains_, preampDb_);
}

Equalizer::Biquad Equalizer::bandBiquad(double sampleRate, double centerHz, double gainDb) {
  // O EQ10 soma a saída de um ressonador à entrada: out = in + g * A(z) * in,
  // com g = 10^(dB/20) - 1 (o desvio em relação a 1.0, porque A(z) devolve a
  // diferença de banda). Reforço usa Q dobrado e corte Q pela metade — são as
  // duas tabelas de coeficientes que dão o timbre do original. Reescrevemos a
  // soma como um único biquad (mesma transferência, estrutura mais simples).
  const double q = (gainDb >= 0.0) ? kBandQ * 2.0 : kBandQ * 0.5;
  const double rate = std::clamp(sampleRate, 4000.0, 384000.0);
  const double freq = std::clamp(centerHz, 20.0, rate * 0.499);
  const double angle = 2.0 * kPi * freq / rate;
  const double alpha = std::sin(angle) / (2.0 * q);
  const double oneOver = 1.0 / (1.0 + alpha);
  const double alphaGain = oneOver * alpha;
  const double r1 = oneOver * 2.0 * std::cos(angle);
  const double r2 = oneOver * (alpha - 1.0);
  const double diffGain = std::pow(10.0, gainDb / 20.0) - 1.0;

  Biquad coef;
  // H(z) = (1 - r1 z^-1 - r2 z^-2 + g a0 (1 - z^-2)) / (1 - r1 z^-1 - r2 z^-2)
  coef.b0 = 1.0 + diffGain * alphaGain;
  coef.b1 = -r1;
  coef.b2 = -r2 - diffGain * alphaGain;
  coef.a1 = -r1;
  coef.a2 = -r2;
  return coef;
}

void Equalizer::rebuild(const std::array<double, kBands>& gainsDb, double preampDb) {
  Coefs next;
  next.preamp = std::pow(10.0, preampDb / 20.0);
  for (int band = 0; band < kBands; ++band) {
    const double gain = std::clamp(gainsDb[static_cast<std::size_t>(band)], kMinGainDb, kMaxGainDb);
    next.active[static_cast<std::size_t>(band)] = std::abs(gain) >= kActiveThresholdDb;
    next.bands[static_cast<std::size_t>(band)] =
        bandBiquad(sampleRate_, kFrequencies[static_cast<std::size_t>(band)], gain);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  pending_ = next;
}

void Equalizer::setSampleRate(double sampleRate) {
  if (sampleRate <= 0.0) return;
  sampleRate_ = sampleRate;
  // O detector cai a 0,1% em kLimiterReleaseSeconds, amostra a amostra.
  limiterDecay_ = std::pow(0.001, 1.0 / (sampleRate * kLimiterReleaseSeconds));
  rebuild(gains_, preampDb_);
  reset();
}

void Equalizer::setEnabled(bool enabled) { enabled_.store(enabled); }

bool Equalizer::enabled() const { return enabled_.load(); }

void Equalizer::setGains(const std::array<double, kBands>& gainsDb) {
  gains_ = gainsDb;
  rebuild(gains_, preampDb_);
}

std::array<double, Equalizer::kBands> Equalizer::gains() const { return gains_; }

void Equalizer::setPreamp(double preampDb) {
  preampDb_ = std::clamp(preampDb, kMinGainDb, kMaxGainDb);
  rebuild(gains_, preampDb_);
}

double Equalizer::preamp() const { return preampDb_; }

void Equalizer::reset() { resetRequested_.store(true); }

void Equalizer::process(float* samples, int frames, int channels) {
  if (!enabled_.load(std::memory_order_relaxed) || samples == nullptr || frames <= 0) return;
  if (channels <= 0) return;
  if (channels > 2) channels = 2;

  // Pedido de reset veio da UI (seek, troca de faixa, stop): zera aqui, na thread
  // que mexe no estado dos filtros, em vez de a UI escrever no meio do process().
  if (resetRequested_.exchange(false)) {
    for (auto& band : z1_) band = {0.0, 0.0};
    for (auto& band : z2_) band = {0.0, 0.0};
    limiterDetect_ = 0.0;
  }

  // Pega a tabela mais nova se ninguém estiver escrevendo nela; senão usa a anterior.
  if (mutex_.try_lock()) {
    ready_ = pending_;
    readyValid_ = true;
    mutex_.unlock();
  }
  if (!readyValid_) return;

  const Coefs& coefs = ready_;
  for (int frame = 0; frame < frames; ++frame) {
    const std::size_t base = static_cast<std::size_t>(frame) * static_cast<std::size_t>(channels);
    for (int channel = 0; channel < channels; ++channel) {
      double sample = static_cast<double>(samples[base + static_cast<std::size_t>(channel)]) *
                      coefs.preamp;
      for (int band = 0; band < kBands; ++band) {
        const std::size_t b = static_cast<std::size_t>(band);
        if (!coefs.active[b]) continue;
        const Biquad& q = coefs.bands[b];
        const std::size_t c = static_cast<std::size_t>(channel);
        const double out = q.b0 * sample + z1_[b][c];
        z1_[b][c] = q.b1 * sample - q.a1 * out + z2_[b][c];
        z2_[b][c] = q.b2 * sample - q.a2 * out;
        sample = out;
      }
      // Limitador do EQ10: o pico "segura" o ganho e volta devagar, então
      // reforço de banda (ou pré-amp alto) nunca estoura a saída.
      const double magnitude = std::abs(sample);
      if (magnitude > limiterDetect_) limiterDetect_ = magnitude;
      if (limiterDetect_ > kLimiterThreshold) {
        sample *= kLimiterThreshold / limiterDetect_;
      }
      samples[base + static_cast<std::size_t>(channel)] = static_cast<float>(sample);
    }
    // A queda do limitador e por quadro, nao por amostra de canal: em estereo o
    // decaimento (por amostra) rodava duas vezes e soltava o pico na metade do tempo.
    limiterDetect_ *= limiterDecay_;
  }
}

}  // namespace wamp
