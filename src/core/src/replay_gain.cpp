#include "wamp/replay_gain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace wamp {
namespace {

/// Lê o número inicial do texto sem depender do locale: `strtod` respeita a
/// vírgula decimal do sistema (e o app roda com locale pt_BR), o que fazia
/// "0.900000" virar 0. Aqui o separador decimal é sempre o ponto.
bool firstNumber(const std::string& text, double* value) {
  std::string cleaned;
  cleaned.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    // U+2212 em UTF-8: E2 88 92 — vira um '-' comum.
    if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x88 &&
        static_cast<unsigned char>(text[i + 2]) == 0x92) {
      cleaned += '-';
      i += 2;
      continue;
    }
    cleaned += (c == ',') ? '.' : static_cast<char>(c);
  }

  std::size_t i = 0;
  while (i < cleaned.size() && std::isdigit(static_cast<unsigned char>(cleaned[i])) == 0 &&
         cleaned[i] != '-' && cleaned[i] != '+' && cleaned[i] != '.') {
    ++i;
  }
  if (i >= cleaned.size()) return false;

  double sign = 1.0;
  if (cleaned[i] == '-') {
    sign = -1.0;
    ++i;
  } else if (cleaned[i] == '+') {
    ++i;
  }

  double number = 0.0;
  bool sawDigit = false;
  while (i < cleaned.size() && std::isdigit(static_cast<unsigned char>(cleaned[i])) != 0) {
    number = number * 10.0 + (cleaned[i] - '0');
    sawDigit = true;
    ++i;
  }
  if (i < cleaned.size() && cleaned[i] == '.') {
    ++i;
    double scale = 0.1;
    while (i < cleaned.size() && std::isdigit(static_cast<unsigned char>(cleaned[i])) != 0) {
      number += (cleaned[i] - '0') * scale;
      scale /= 10.0;
      sawDigit = true;
      ++i;
    }
  }
  if (!sawDigit) return false;

  *value = sign * number;
  return true;
}

}  // namespace

bool parseGainDb(const std::string& text, double* db) {
  if (db == nullptr) return false;
  if (text.empty()) return false;
  return firstNumber(text, db);
}

bool parsePeak(const std::string& text, double* peak) {
  if (peak == nullptr) return false;
  double value = 0.0;
  if (!firstNumber(text, &value)) return false;
  if (value <= 0.0) return false;
  if (value >= 100.0) value /= 100000.0;  // pico gravado como inteiro (ex.: 98765)
  *peak = value;
  return true;
}

double replayGainFactor(double gainDb) {
  const double factor = std::pow(10.0, gainDb / 20.0);
  return std::clamp(factor, 0.0, 16.0);
}

double replayGainFactor(double gainDb, double peak) {
  double factor = replayGainFactor(gainDb);
  if (peak > 0.0) {
    const double ceiling = 1.0 / peak;
    if (factor > ceiling) factor = ceiling;
  }
  return factor;
}

double replayGainFactor(const std::string& gainText, const std::string& peakText) {
  double gainDb = 0.0;
  if (!parseGainDb(gainText, &gainDb)) return 1.0;

  double peak = 0.0;
  const bool hasPeak = parsePeak(peakText, &peak);
  return hasPeak ? replayGainFactor(gainDb, peak) : replayGainFactor(gainDb);
}

}  // namespace wamp
