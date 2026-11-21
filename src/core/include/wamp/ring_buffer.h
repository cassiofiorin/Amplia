#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace wamp {

/// Ring buffer SPSC de float intercalado: a thread de decode escreve, a thread de
/// áudio lê. Sem locks — só os dois índices atômicos (ver ARCHITECTURE §4).
///
/// Capacidade em frames; internamente conta amostras para evitar divisão no caminho
/// de áudio. `head_` e `tail_` só crescem, então `head - tail` funciona mesmo após
/// o wrap do size_t.
class FloatRing {
 public:
  FloatRing(std::size_t capacityFrames, int channels)
      : capacity_(capacityFrames * static_cast<std::size_t>(channels)),
        channels_(channels),
        buffer_(capacity_ + framesPerChannel(channels), 0.0f) {}

  /// Escreve até `frames`; devolve quantos foram aceitos (pode ser menos se cheio).
  std::size_t write(const float* src, std::size_t frames) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    std::size_t samples = std::min(frames * channels_, capacity_ - (head - tail));
    samples -= samples % channels_;
    if (samples == 0) return 0;

    const std::size_t start = head % capacity_;
    const std::size_t first = std::min(samples, capacity_ - start);
    std::memcpy(buffer_.data() + start, src, first * sizeof(float));
    if (samples > first) {
      std::memcpy(buffer_.data(), src + first, (samples - first) * sizeof(float));
    }
    head_.store(head + samples, std::memory_order_release);
    return samples / channels_;
  }

  /// Lê até `frames`; devolve quantos foram entregues. Falta é silêncio do chamador.
  std::size_t read(float* dst, std::size_t frames) {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    std::size_t samples = std::min(frames * channels_, head - tail);
    samples -= samples % channels_;
    if (samples == 0) return 0;

    const std::size_t start = tail % capacity_;
    const std::size_t first = std::min(samples, capacity_ - start);
    std::memcpy(dst, buffer_.data() + start, first * sizeof(float));
    if (samples > first) {
      std::memcpy(dst + first, buffer_.data(), (samples - first) * sizeof(float));
    }
    tail_.store(tail + samples, std::memory_order_release);
    return samples / channels_;
  }

  std::size_t availableFrames() const {
    return (head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)) /
           channels_;
  }

  std::size_t freeFrames() const {
    const std::size_t used =
        head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    return (capacity_ - used) / channels_;
  }

  std::size_t capacityFrames() const { return capacity_ / channels_; }

  /// Só pode ser chamado com a saída de áudio parada (ou aceitando um clique).
  void clear() { tail_.store(head_.load(std::memory_order_acquire)); }

  int channels() const { return channels_; }

 private:
  static std::size_t framesPerChannel(int channels) {
    return static_cast<std::size_t>(channels);
  }

  const std::size_t capacity_;  // em amostras
  const int channels_;
  std::vector<float> buffer_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace wamp
