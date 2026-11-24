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

/// Ponto no plano Agitação × Tom, com a largura do alvo. Ver D-121.
/// Agitação: 0 = calmo e lento, 100 = agitado e rápido (vem do BPM).
/// Tom: 0 = frio (menor), 100 = quente (maior) — a valência que a gente consegue medir.
/// Estabilidade: 0 = alvo estreito, 100 = alvo largo, com deriva ancorada na geração.
struct HumorPoint {
  double agitation = 50.0;
  double tone = 50.0;
  double stability = 50.0;
};

/// Onde uma faixa cai no plano. Sem BPM ou sem tom a leitura é neutra (50) e o eixo é
/// dado como desconhecido — o alvo não pode punir o que ainda não foi analisado.
struct HumorReading {
  double agitation = 50.0;
  double tone = 50.0;
  bool hasBpm = false;
  bool hasTone = false;
};

HumorReading radioHumorReading(double bpm, int keyPc, int keyMajor);

/// Largura (desvio-padrão) do decaimento em torno do alvo, a partir da estabilidade.
double radioHumorWidth(double stability);

/// Pontuação de clima: decaimento gaussiano no plano. Sem nenhum eixo conhecido a faixa
/// fica no neutro (0.5) em vez de sumir — senão, sem análise, a estação nasce vazia.
double radioScore(const HumorPoint& point, const RadioCandidate& candidate);

/// Um preset é um ponto + largura + o léxico de gênero que dá identidade à estação.
/// Os de fábrica são fixos; a variação do usuário é salva como preset próprio (D-121).
struct HumorPreset {
  int id = 0;            ///< 0 nos de fábrica; id do banco nos salvos
  bool factory = true;
  std::string name;
  HumorPoint point;
  double blend = 0.5;    ///< 0 = só o clima, 1 = só o histórico
  std::string genres;    ///< léxico separado por vírgula; vazio = qualquer gênero
};

/// Presets de fábrica. São dados, não código por clima: a geração só lê a tabela.
std::vector<HumorPreset> factoryHumorPresets();

/// Ajuste do léxico do preset sobre o gênero da faixa: 1.0 quando casa, 0.6 quando o gênero
/// é conhecido e está fora e 0.85 quando não há gênero para comparar (não punir o que o
/// arquivo não declara). Léxico vazio = sem ajuste.
double radioGenreLean(const std::string& genres, const std::string& genre);

/// Semente composta: a faixa que está tocando e o "clima de agora" (o que venho ouvindo)
/// entram numa média ponderada. `blend` 0 = só a faixa; 1 = só o clima.
struct RadioMix {
  RadioSeed track;
  RadioSeed history;  ///< centroide do histórico; sem campos = não há histórico
  double blend = 0.0;
  /// Quando true, a estação é de clima: o alvo substitui a faixa da semente e o `blend`
  /// passa a misturar clima com histórico.
  bool hasHumor = false;
  HumorPoint humor;
  std::string genres;  ///< léxico do preset; vazio = sem ajuste de gênero

  bool hasHistory() const;
};

/// Centroide do que vem sendo ouvido: gênero e tom modais, ano mediano, BPM médio
/// geométrico. Artista e álbum ficam de fora de propósito — identidade não é clima.
RadioSeed radioHistorySeed(const std::vector<RadioSeed>& recent);

/// A mesma pontuação, misturada na proporção `blend` com o clima do histórico. A faixa da
/// semente continua descartada mesmo com o clima puxando a nota dela para cima.
double radioScore(const RadioMix& mix, const RadioCandidate& candidate);

/// Igual ao `radioPick` acima, pontuando pela mistura.
std::vector<std::string> radioPick(const RadioMix& mix,
                                   const std::vector<RadioCandidate>& candidates,
                                   std::size_t wanted, std::size_t discoveryEvery = 7);

/// Aceitação aprendida a partir dos pulsos registrados naquele contexto (Laplace).
double radioAcceptance(int plays, int skips);

}  // namespace wamp
