// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#pragma once

#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace wamp {

/// FFT radix-2 in-place (potência de dois). Compartilhada pelo visualizador e pela
/// análise de faixa — as duas precisam do mesmo transform, sem dependência externa.
inline void fftInPlace(std::vector<std::complex<float>>& data) {
  constexpr double kPi = 3.14159265358979323846;
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

}  // namespace wamp
