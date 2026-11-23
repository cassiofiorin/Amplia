// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#include "wamp/radio.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace wamp {
namespace {

constexpr int kMajorCode[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
constexpr int kMinorCode[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};

/// 1.0 para o mesmo pulso, caindo com a distância relativa. Dobra/metade é equivalência
/// (uma faixa de 75 BPM casa com uma de 150), não erro.
double tempoWeight(double a, double b) {
  if (a <= 0.0 || b <= 0.0) return 0.0;
  double ratio = a / b;
  while (ratio < 0.75) ratio *= 2.0;
  while (ratio > 1.5) ratio /= 2.0;
  const double distance = std::abs(std::log2(ratio));
  return std::max(0.0, 1.0 - distance / 0.12);
}

/// Compatibilidade harmônica na roda de Camelot: mesmo código, vizinho e relativo.
double keyWeight(int pcA, int majorA, int pcB, int majorB) {
  if (pcA < 0 || pcB < 0) return 0.0;
  if (pcA == pcB && majorA == majorB) return 1.0;
  const int codeA = majorA != 0 ? kMajorCode[pcA] : kMinorCode[pcA];
  const int codeB = majorB != 0 ? kMajorCode[pcB] : kMinorCode[pcB];
  if (pcA == pcB && majorA != majorB) return 0.7;  // relativo maior/menor
  const int step = std::abs(codeA - codeB);
  if (majorA == majorB && (step == 1 || step == 11)) return 0.6;
  return 0.0;
}

}  // namespace

int radioBpmBucket(double bpm) {
  if (bpm <= 0.0) return -1;
  double value = bpm;
  while (value < 60.0) value *= 2.0;
  while (value >= 180.0) value /= 2.0;
  int bucket = 0;
  for (int i = 0; i < 8; ++i) {
    if (value >= static_cast<double>(kRadioBpmEdges[i])) bucket = i;
  }
  return bucket;
}

std::string radioCamelot(int keyPc, int keyMajor) {
  if (keyPc < 0 || keyPc > 11) return {};
  const int code = keyMajor != 0 ? kMajorCode[keyPc] : kMinorCode[keyPc];
  return std::to_string(code) + (keyMajor != 0 ? "B" : "A");
}

double radioAcceptance(int plays, int skips) {
  // Laplace: uma faixa com um pulo e nenhuma ouvida não é banida, só perde peso.
  return static_cast<double>(1 + std::max(0, plays)) /
         static_cast<double>(2 + std::max(0, plays) + std::max(0, skips));
}

double radioScore(const RadioSeed& seed, const RadioCandidate& candidate) {
  if (candidate.path == seed.path) return 0.0;
  double score = 0.0;

  // Artista e álbum são o sinal mais forte de "mesmo universo".
  if (!seed.artist.empty() && candidate.artist == seed.artist) score += 0.30;
  if (!seed.album.empty() && candidate.album == seed.album) score += 0.25;
  if (!seed.genre.empty() && candidate.genre == seed.genre) score += 0.10;
  if (seed.year > 0 && candidate.year > 0) {
    const int gap = std::abs(seed.year - candidate.year);
    score += 0.10 * std::max(0.0, 1.0 - static_cast<double>(gap) / 12.0);
  }
  score += 0.15 * tempoWeight(seed.bpm, candidate.bpm);
  score += 0.15 * keyWeight(seed.keyPc, seed.keyMajor, candidate.keyPc, candidate.keyMajor);

  // Nota e histórico entram como ajuste, não como eixo: rádio não é playlist de favoritas.
  score += 0.03 * (static_cast<double>(std::clamp(candidate.rating, 0, 5)) / 5.0);
  score += 0.02 * std::min(1.0, static_cast<double>(candidate.plays) / 20.0);

  // Pulos penalizam sem excluir: no clima errado a faixa afunda, no certo ela volta.
  const double acceptance = radioAcceptance(candidate.plays, candidate.skips);
  score *= 0.55 + 0.45 * acceptance;
  return score;
}

std::vector<std::string> radioPick(const RadioSeed& seed,
                                   const std::vector<RadioCandidate>& candidates,
                                   std::size_t wanted, std::size_t discoveryEvery) {
  struct Scored {
    double score;
    const RadioCandidate* candidate;
  };
  std::vector<Scored> scored;
  scored.reserve(candidates.size());
  for (const RadioCandidate& candidate : candidates) {
    const double score = radioScore(seed, candidate);
    if (score > 0.02) scored.push_back({score, &candidate});
  }
  std::sort(scored.begin(), scored.end(),
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

  std::vector<std::string> picked;
  std::vector<std::string> recentArtists;
  std::size_t index = 0;
  while (picked.size() < wanted && index < scored.size()) {
    const RadioCandidate* candidate = scored[index].candidate;
    ++index;
    // Diversidade: no máximo um do mesmo artista a cada 5 escolhas, para não virar
    // "álbum inteiro" quando a semente é de um artista só.
    const bool artistBusy =
        picked.size() < 5 &&
        std::find(recentArtists.begin(), recentArtists.end(), candidate->artist) !=
            recentArtists.end();
    // Descoberta: uma faixa pouco tocada a cada N, para a estação não repetir sempre as
    // mesmas músicas.
    const bool discovery = discoveryEvery > 0 && (picked.size() + 1) % discoveryEvery == 0;
    if (discovery && candidate->plays > 3 && index + 1 < scored.size()) continue;
    if (artistBusy) continue;
    picked.push_back(candidate->path);
    recentArtists.push_back(candidate->artist);
    if (recentArtists.size() > 5) recentArtists.erase(recentArtists.begin());
  }
  return picked;
}

}  // namespace wamp
