// Autotag — o casamento entre a tag do arquivo e os candidatos de um catálogo.
//
// Aqui não há rede, banco nem Qt: entra uma consulta (o que o arquivo diz), entram candidatos
// (o que o catálogo devolve) e sai uma pontuação com o motivo. É a parte que decide se vale
// perguntar, se vale gravar sozinho e o que exatamente seria gravado — e por ser pura, roda
// num teste sem servidor e sem biblioteca.
//
// As regras estão em docs/ROADMAP.md (F4): só campo em branco é preenchido, nada é apagado,
// e a gravação silenciosa exige casamento alto **e** sem disputa.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wamp {

/// O que o arquivo diz — já sem invenção: campo vazio é campo vazio.
struct TagQuery {
  std::string artist;
  std::string album;
  std::string title;
  std::int64_t durationMs = 0;
  /// Número de faixa, de disco e ano atuais; zero quer dizer "em branco".
  int trackNo = 0;
  int discNo = 0;
  int year = 0;
};

/// O que o catálogo devolve. `id` identifica o candidato na origem (MBID, no MusicBrainz).
struct TagCandidate {
  std::string id;
  std::string artist;
  std::string albumArtist;
  std::string album;
  std::string title;
  int year = 0;
  int trackNo = 0;
  int discNo = 0;
  /// Quantas faixas o lançamento tem; só a busca por releases preenche. Serve para preferir a
  /// edição do tamanho da pasta antes de abrir uma por uma.
  int trackCount = 0;
  std::int64_t durationMs = 0;
  /// Release de onde veio (um mesmo candidato pode aparecer em várias edições).
  std::string releaseId;
};

/// Candidato com nota, margem e os motivos que a explicam.
struct ScoredCandidate {
  TagCandidate candidate;
  double score = 0.0;
  /// Distância para o segundo colocado da lista (o primeiro tem a maior).
  double margin = 0.0;
  /// Eixos que casaram, na ordem de peso: "title", "artist", "album", "duration".
  std::vector<std::string> reasons;
};

/// Como o caso se resolve sozinho (ou não).
enum class TagDecision {
  None,       // nada encontrado: o arquivo fica exatamente como está
  Review,     // há candidato, mas não com confiança para gravar sozinho
  AutoApply,  // casamento alto e sem disputa: pode preencher os campos em branco
};

/// Nota de um candidato contra a consulta, de 0 a 1.
///
/// O peso de cada eixo é redistribuído entre os eixos que a consulta realmente tem: um arquivo
/// sem álbum não é penalizado por não casar álbum, senão faixa solta nunca pontuaria.
double autotagScore(const TagQuery& query, const TagCandidate& candidate,
                    std::vector<std::string>* reasons = nullptr);

/// Ordena por nota (desc) e preenche a margem de cada um contra o seguinte.
/// Devolve no máximo `limit` candidatos.
std::vector<ScoredCandidate> rankCandidates(const TagQuery& query,
                                            const std::vector<TagCandidate>& candidates,
                                            std::size_t limit = 20);

/// A regra da gravação silenciosa: melhor nota ≥ 0,90 **e** margem ≥ 0,15 sobre o segundo.
TagDecision classifyTagMatch(const std::vector<ScoredCandidate>& ranked);

/// Campos do arquivo que estão em branco e que o candidato preencheria.
///
/// Campo preenchido no arquivo nunca entra: a regra é não sobrescrever. Capa não está aqui —
/// ela é arquivo, não campo de texto, e tem decisão própria.
struct TagFill {
  bool title = false;
  bool artist = false;
  bool album = false;
  bool albumArtist = false;
  bool trackNo = false;
  bool discNo = false;
  bool year = false;

  bool any() const {
    return title || artist || album || albumArtist || trackNo || discNo || year;
  }
};

/// Calcula o que seria gravado: só campo vazio no arquivo e não vazio no candidato.
TagFill planTagFill(const TagQuery& current, const TagCandidate& candidate);

/// O que a revisão pode preencher sozinha, sem marca do usuário.
///
/// Regra do consenso: entre os até três candidatos plausíveis (nota ≥ `kConsensusScore`), um
/// campo de texto só entra se **todos** concordam no mesmo valor — empate de nota não é dúvida
/// quando as edições dizem a mesma coisa. O ano é exceção combinada com o autor: vale o mais
/// antigo, desde que o candidato que o doa tenha o mesmo álbum do consenso — é o álbum igual que
/// separa "edição defasada" de "outra obra", não a distância entre os anos (um deluxe de 2013 de
/// um disco de 1973 tem 40 anos de diferença e é exatamente o caso certo). O limite de
/// `kYearSpreadLimit` só existe para pegar absurdo. Faixa e disco só por consenso exato, nunca
/// pela exceção do ano.
inline constexpr double kConsensusScore = 0.55;
inline constexpr int kYearSpreadLimit = 60;

TagFill consensusFill(const TagQuery& current, const std::vector<TagCandidate>& candidates,
                      double bestScore);

/// Quantos candidatos o consenso olha (os melhores, com nota ≥ `kConsensusScore`).
inline constexpr std::size_t kConsensusDepth = 3;

/// Verdadeiro quando a tag atual está incompleta o bastante para valer uma busca.
/// Sem título e sem artista não há consulta possível.
bool tagWorthSearching(const TagQuery& query);

}  // namespace wamp
