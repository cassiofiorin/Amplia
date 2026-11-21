#pragma once

#include <string>

namespace wamp {

/// Tags de ReplayGain vêm como texto nas mais variadas formas: "-6.50 dB", "+3.2 dB",
/// "  -6,50 DB", às vezes com o sinal de menos tipográfico (U+2212). Lê o número e
/// devolve o que estava em dB.
bool parseGainDb(const std::string& text, double* db);

/// Pico declarado (0..1+). Formatos antigos gravam "0.98765"; alguns gravam "98765" em
/// vez de fração — nesse caso dividimos por 100000.
bool parsePeak(const std::string& text, double* peak);

/// Ganho linear de um valor em dB (`10^(dB/20)`).
double replayGainFactor(double gainDb);

/// Mesma coisa, mas limitada pelo pico declarado para não estourar: se o produto
/// `fator × pico` passa de 1, o fator desce até caber.
double replayGainFactor(double gainDb, double peak);

/// Conveniência: lê o texto do ganho e o do pico e devolve o fator final.
/// Texto vazio ou inválido devolve 1.0 (sem alteração).
double replayGainFactor(const std::string& gainText, const std::string& peakText);

}  // namespace wamp
