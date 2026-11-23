// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#pragma once

#include <string>
#include <vector>


namespace wamp {

/// Contexto de onde uma faixa foi sugerida: é o que dá sentido ao pulo. Uma acústica
/// pulada no meio de um álbum agitado pode ser exatamente o que se quer num clima calmo.
struct RadioContext {
  int bpmBucket = -1;      ///< 0–7, faixa de BPM percebido (60 em 60… ver kRadioBpmEdges)
  std::string keyCamelot;  ///< "9B" quando conhecido
  std::string genre;
  std::string artist;

  bool empty() const { return bpmBucket < 0 && keyCamelot.empty() && genre.empty(); }
};

constexpr int kRadioBpmEdges[] = {60, 80, 95, 110, 125, 140, 160, 180};

int radioBpmBucket(double bpm);
std::string radioCamelot(int keyPc, int keyMajor);

/// Candidata já resolvida: o que a pontuação precisa saber, sem tocar mais no banco.
struct RadioCandidate {
  std::string path;
  std::string artist;
  std::string album;
  std::string genre;
  int year = 0;
  double bpm = 0.0;      ///< percebido quando houver, senão o da tag
  int keyPc = -1;
  int keyMajor = 0;
  int skips = 0;
  int plays = 0;
  int rating = 0;
};

struct RadioSeed {
  std::string path;
  std::string artist;
  std::string album;
  std::string genre;
  int year = 0;
  double bpm = 0.0;
  int keyPc = -1;
  int keyMajor = 0;
};

/// Pontuação de uma candidata em relação à semente. Quanto maior, melhor; 0 = descartada
/// (mesma faixa da semente).
double radioScore(const RadioSeed& seed, const RadioCandidate& candidate);

/// Escolhe até `wanted` faixas, evitando repetir artista/álbum em sequência próxima e
/// injetando uma "descoberta" a cada `discoveryEvery` escolhas.
std::vector<std::string> radioPick(const RadioSeed& seed,
                                   const std::vector<RadioCandidate>& candidates,
                                   std::size_t wanted, std::size_t discoveryEvery = 7);

/// Aceitação aprendida a partir dos pulsos registrados naquele contexto (Laplace).
double radioAcceptance(int plays, int skips);

}  // namespace wamp
