// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#include "wamp/radio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <utility>

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

namespace {

/// Ordena pela pontuação que vier e aplica as duas regras que não dependem da semente:
/// diversidade de artista no começo e uma "descoberta" de vez em quando.
template <typename ScoreFn>
std::vector<std::string> pickWith(ScoreFn score, const std::vector<RadioCandidate>& candidates,
                                  std::size_t wanted, std::size_t discoveryEvery) {
  struct Scored {
    double score;
    const RadioCandidate* candidate;
  };
  std::vector<Scored> scored;
  scored.reserve(candidates.size());
  for (const RadioCandidate& candidate : candidates) {
    const double value = score(candidate);
    if (value > 0.02) scored.push_back({value, &candidate});
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

}  // namespace

std::vector<std::string> radioPick(const RadioSeed& seed,
                                   const std::vector<RadioCandidate>& candidates,
                                   std::size_t wanted, std::size_t discoveryEvery) {
  return pickWith([&seed](const RadioCandidate& candidate) { return radioScore(seed, candidate); },
                  candidates, wanted, discoveryEvery);
}

bool RadioMix::hasHistory() const {
  return history.bpm > 0.0 || !history.genre.empty() || history.keyPc >= 0 ||
         history.year > 0;
}

RadioSeed radioHistorySeed(const std::vector<RadioSeed>& recent) {
  RadioSeed seed;
  if (recent.empty()) return seed;

  // Gênero e tom modais: o que mais aparece no que venho ouvindo. A ordem de `recent` é
  // da mais recente para a mais antiga, então o empate fica com a memória mais fresca.
  std::map<std::string, int> genres;
  std::map<std::pair<int, int>, int> keys;
  for (const RadioSeed& item : recent) {
    if (!item.genre.empty()) ++genres[item.genre];
    if (item.keyPc >= 0) ++keys[{item.keyPc, item.keyMajor}];
  }
  int best = 0;
  for (const auto& entry : genres) {
    if (entry.second > best) {
      best = entry.second;
      seed.genre = entry.first;
    }
  }
  best = 0;
  for (const auto& entry : keys) {
    if (entry.second > best) {
      best = entry.second;
      seed.keyPc = entry.first.first;
      seed.keyMajor = entry.first.second;
    }
  }

  // Ano: mediana. Um disco antigo isolado não puxa a estação inteira para os anos 80.
  std::vector<int> years;
  for (const RadioSeed& item : recent) {
    if (item.year > 0) years.push_back(item.year);
  }
  if (!years.empty()) {
    std::sort(years.begin(), years.end());
    seed.year = years[(years.size() - 1) / 2];
  }

  // BPM: média geométrica. Dobrar ou metade é equivalência de pulso, então 75 e 150
  // juntos caem num meio-termo que casa com os dois.
  double logSum = 0.0;
  int bpmCount = 0;
  for (const RadioSeed& item : recent) {
    if (item.bpm > 0.0) {
      logSum += std::log(item.bpm);
      ++bpmCount;
    }
  }
  if (bpmCount > 0) seed.bpm = std::exp(logSum / bpmCount);
  return seed;
}

HumorReading radioHumorReading(double bpm, int keyPc, int keyMajor) {
  HumorReading reading;
  if (bpm > 0.0) {
    // 50 BPM → 0 e 190 BPM → 100, em log: dobrou o pulso, andou um tanto constante. Faixas
    // muito lentas ou muito rápidas são dobradas/divididas antes, então 40 e 80 caem no
    // mesmo lugar — é o pulso percebido, não o número da tag.
    constexpr double kLow = 50.0;
    constexpr double kHigh = 190.0;
    double value = bpm;
    while (value < kLow) value *= 2.0;
    while (value > kHigh) value /= 2.0;
    reading.agitation = 100.0 * std::log2(value / kLow) / std::log2(kHigh / kLow);
    reading.hasBpm = true;
  }
  if (keyPc >= 0 && keyPc <= 11) {
    reading.tone = keyMajor != 0 ? 100.0 : 0.0;
    reading.hasTone = true;
  }
  return reading;
}

double radioHumorWidth(double stability) {
  const double clamped = std::clamp(stability, 0.0, 100.0) / 100.0;
  return 0.08 + 0.27 * clamped;
}

double radioScore(const HumorPoint& point, const RadioCandidate& candidate) {
  const HumorReading reading = radioHumorReading(candidate.bpm, candidate.keyPc, candidate.keyMajor);
  const double targetAgitation = std::clamp(point.agitation, 0.0, 100.0);
  const double targetTone = std::clamp(point.tone, 0.0, 100.0);
  // Tom no meio do slider (50 ± 10) é indiferença, não um alvo entre frio e quente: os dois
  // polos são categorias, então "qualquer modo" precisa de uma regra própria.
  const bool toneMatters = std::abs(targetTone - 50.0) >= 10.0;

  const int known =
      (reading.hasBpm ? 1 : 0) + (reading.hasTone && toneMatters ? 1 : 0);

  // Neutro quando nada foi medido: a estação não pode nascer vazia só porque a biblioteca
  // ainda não foi analisada. Fica abaixo de um casamento decente e acima de um desastre —
  // assim uma faixa medida e errada não ganha de uma ainda não medida.
  constexpr double kNeutral = 0.25;
  constexpr double kFloor = 0.08;
  double score = kNeutral;
  if (known > 0) {
    double squared = 0.0;
    if (reading.hasBpm) {
      const double delta = (reading.agitation - targetAgitation) / 100.0;
      squared += delta * delta;
    }
    if (reading.hasTone && toneMatters) {
      const double delta = (reading.tone - targetTone) / 100.0;
      squared += delta * delta;
    }
    const double width = radioHumorWidth(point.stability);
    const double gaussian = kFloor + (1.0 - kFloor) * std::exp(-squared / (width * width));
    // Um eixo só informa menos que os dois: fica mais perto do neutro.
    score = known == 2 ? gaussian : kNeutral + 0.65 * (gaussian - kNeutral);
  }

  score += 0.03 * (static_cast<double>(std::clamp(candidate.rating, 0, 5)) / 5.0);
  score += 0.02 * std::min(1.0, static_cast<double>(candidate.plays) / 20.0);
  const double acceptance = radioAcceptance(candidate.plays, candidate.skips);
  score *= 0.55 + 0.45 * acceptance;
  return score;
}

double radioGenreLean(const std::string& genres, const std::string& genre) {
  if (genres.empty()) return 1.0;
  if (genre.empty()) return 0.85;  // arquivo sem gênero: não dá para dizer que está fora

  auto lower = [](std::string text) {
    for (char& character : text) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
  };
  const std::string haystack = lower(genre);
  std::size_t start = 0;
  while (start <= genres.size()) {
    const std::size_t comma = genres.find(',', start);
    const std::size_t end = comma == std::string::npos ? genres.size() : comma;
    std::string term = lower(genres.substr(start, end - start));
    while (!term.empty() && std::isspace(static_cast<unsigned char>(term.front()))) term.erase(term.begin());
    while (!term.empty() && std::isspace(static_cast<unsigned char>(term.back()))) term.pop_back();
    if (!term.empty() && haystack.find(term) != std::string::npos) return 1.0;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return 0.6;
}

std::vector<HumorPreset> factoryHumorPresets() {
  // Presets fixos: o usuário parte deles e salva variações com nome próprio (D-121). O
  // léxico repete grafias com e sem acento porque comparar gênero é comparação de texto.
  std::vector<HumorPreset> presets;
  auto add = [&presets](const char* name, double agitation, double tone, double stability,
                        const char* genres) {
    HumorPreset preset;
    preset.name = name;
    preset.point = {agitation, tone, stability};
    preset.genres = genres;
    presets.push_back(std::move(preset));
  };
  add("Treino", 90.0, 0.0, 55.0, "metal, hardcore, punk, rock, hip-hop, hip hop, eletrônico, eletronico, drum and bass");
  add("Alegre", 70.0, 100.0, 55.0, "pop, dance, ska, reggae, disco, funk, soul");
  add("Dia de sol", 55.0, 100.0, 60.0, "pop, folk, mpb, bossa, samba, sertanejo, soul, reggae");
  add("Trabalho duro", 80.0, 0.0, 45.0, "industrial, rock, metal, alternativo, alternative, grunge");
  add("Dia frio", 25.0, 0.0, 50.0, "folk, acústico, acustico, indie, shoegaze, ambient, jazz");
  add("Pra baixo", 15.0, 0.0, 40.0, "slowcore, emo, blues, indie, post-rock, post rock");
  add("Foco", 40.0, 50.0, 30.0, "ambient, lofi, lo-fi, clássico, classico, jazz, eletrônica, eletronica");
  return presets;
}

double radioScore(const RadioMix& mix, const RadioCandidate& candidate) {
  // A faixa da semente nunca se sugere: sem isso o clima do histórico daria nota a ela.
  if (!mix.track.path.empty() && candidate.path == mix.track.path) return 0.0;
  if (mix.hasHumor) {
    const double humor = radioScore(mix.humor, candidate) * radioGenreLean(mix.genres, candidate.genre);
    if (mix.blend <= 0.0 || !mix.hasHistory()) return humor;
    const double weight = std::min(1.0, mix.blend);
    return (1.0 - weight) * humor + weight * radioScore(mix.history, candidate);
  }
  const double base = radioScore(mix.track, candidate);
  if (mix.blend <= 0.0 || !mix.hasHistory()) return base;
  const double weight = std::min(1.0, mix.blend);
  return (1.0 - weight) * base + weight * radioScore(mix.history, candidate);
}

std::vector<std::string> radioPick(const RadioMix& mix,
                                   const std::vector<RadioCandidate>& candidates,
                                   std::size_t wanted, std::size_t discoveryEvery) {
  return pickWith([&mix](const RadioCandidate& candidate) { return radioScore(mix, candidate); },
                  candidates, wanted, discoveryEvery);
}

}  // namespace wamp
