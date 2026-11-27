// Teste do casamento do autotag: função pura, sem rede e sem arquivo.
//
// O que está sendo defendido aqui é a regra de `docs/ROADMAP.md` (F4): só alta confiança e sem
// disputa grava sozinho; o resto vira revisão; nada encontrado é resposta válida.

#include "wamp/autotag.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("  FALHOU: %s\n", what);
    ++failures;
  }
}

bool near(double a, double b, double epsilon = 1e-6) { return std::fabs(a - b) <= epsilon; }

wamp::TagQuery exactQuery() {
  wamp::TagQuery query;
  query.artist = "Foo Fighters";
  query.album = "One by One";
  query.title = "All My Life";
  query.durationMs = 260000;
  query.trackNo = 1;
  query.discNo = 1;
  query.year = 2002;
  return query;
}

wamp::TagCandidate exactCandidate() {
  wamp::TagCandidate candidate;
  candidate.id = "mbid-1";
  candidate.artist = "Foo Fighters";
  candidate.albumArtist = "Foo Fighters";
  candidate.album = "One by One";
  candidate.title = "All My Life";
  candidate.year = 2002;
  candidate.trackNo = 1;
  candidate.discNo = 1;
  candidate.durationMs = 260800;  // 0,8 s de diferença: dentro da tolerância
  return candidate;
}

void testExactMatchIsAuto() {
  std::vector<std::string> reasons;
  const double score = wamp::autotagScore(exactQuery(), exactCandidate(), &reasons);
  check(score > 0.99, "casamento exato pontua quase 1");
  check(reasons.size() == 4, "os quatro eixos aparecem como motivo");

  const auto ranked = wamp::rankCandidates(exactQuery(), {exactCandidate()});
  check(wamp::classifyTagMatch(ranked) == wamp::TagDecision::AutoApply,
        "candidato único e exato pode ser gravado sozinho");
}

void testAmbiguityAsksForReview() {
  wamp::TagCandidate other = exactCandidate();
  other.id = "mbid-2";
  other.album = "Greatest Hits";
  other.durationMs = 261000;

  const auto ranked = wamp::rankCandidates(exactQuery(), {exactCandidate(), other});
  check(ranked.size() == 2, "os dois candidatos voltam");
  check(ranked.front().score > ranked.back().score, "o exato fica na frente");
  check(ranked.front().margin < 0.15, "com segundo colado, a margem é pequena");
  check(wamp::classifyTagMatch(ranked) == wamp::TagDecision::Review,
        "disputa vira revisão, não gravação silenciosa");
}

void testWrongArtistIsNotAuto() {
  wamp::TagCandidate wrong = exactCandidate();
  wrong.artist = "Nirvana";
  wrong.albumArtist = "Nirvana";

  const auto ranked = wamp::rankCandidates(exactQuery(), {wrong});
  const double score = ranked.front().score;
  check(score < 0.90, "artista errado derruba a nota abaixo do corte");
  check(wamp::classifyTagMatch(ranked) != wamp::TagDecision::AutoApply,
        "artista errado não é gravado sozinho");
}

void testQueryWithoutAlbumRedistributesWeight() {
  wamp::TagQuery loose = exactQuery();
  loose.album.clear();

  wamp::TagCandidate candidate = exactCandidate();
  candidate.album = "Outra Coisa";

  const double score = wamp::autotagScore(loose, candidate);
  check(score > 0.99, "sem álbum na consulta, o álbum do candidato não penaliza");
}

void testDurationOutsideToleranceDecays() {
  wamp::TagQuery query = exactQuery();
  wamp::TagCandidate candidate = exactCandidate();
  candidate.durationMs = 260000 + 3000;  // no limite: ainda casa
  check(wamp::autotagScore(query, candidate) > 0.99, "3 s de diferença ainda casa");

  candidate.durationMs = 260000 + 12000;  // 12 s além da tolerância: nota de duração some
  const double score = wamp::autotagScore(query, candidate);
  check(score < 0.95 && score > 0.80, "duração muito diferente pesa, mas não zera o resto");
}

void testPlanFillsOnlyBlankFields() {
  wamp::TagQuery current = exactQuery();
  current.album.clear();
  current.year = 0;
  current.trackNo = 0;

  const wamp::TagFill fill = wamp::planTagFill(current, exactCandidate());
  check(fill.album, "álbum em branco é preenchido");
  check(fill.albumArtist, "artista do álbum acompanha o álbum");
  check(fill.year, "ano em branco é preenchido");
  check(fill.trackNo, "faixa em branco é preenchida");
  check(!fill.title, "título existente nunca é tocado");
  check(!fill.artist, "artista existente nunca é tocado");

  const wamp::TagFill nothing = wamp::planTagFill(exactQuery(), exactCandidate());
  check(!nothing.any(), "tag completa não gera preenchimento nenhum");
}

void testNoCandidatesMeansNone() {
  const auto ranked = wamp::rankCandidates(exactQuery(), {});
  check(ranked.empty(), "lista vazia continua vazia");
  check(wamp::classifyTagMatch(ranked) == wamp::TagDecision::None,
        "nada encontrado é resposta válida");
}

void testWorthSearching() {
  wamp::TagQuery empty;
  check(!wamp::tagWorthSearching(empty), "sem título nem artista não há o que buscar");

  wamp::TagQuery onlyArtist;
  onlyArtist.artist = "Foo Fighters";
  check(wamp::tagWorthSearching(onlyArtist), "só o artista já permite uma busca");
}

void testRemasterSuffixDoesNotBreakMatch() {
  wamp::TagQuery query = exactQuery();
  query.title = "All My Life (Remastered 2022)";

  const double score = wamp::autotagScore(query, exactCandidate());
  check(near(score, 1.0, 1e-9) || score > 0.99, "sufixo de remaster não atrapalha o casamento");
}


wamp::TagCandidate edition(int year, const std::string& album, int track) {
  wamp::TagCandidate candidate = exactCandidate();
  candidate.year = year;
  candidate.album = album;
  candidate.trackNo = track;
  return candidate;
}

void testConsensusFillsBlankFieldsWhenEditionsAgree() {
  // Faixa que foi para revisão por empate, mas todas as edições dizem o mesmo álbum.
  wamp::TagQuery query = exactQuery();
  query.album.clear();
  query.year = 0;
  std::vector<wamp::TagCandidate> candidates{
      edition(1973, "Ring Ring", 1), edition(2013, "Ring Ring", 1), edition(1990, "Ring Ring", 1)};
  const auto fill = wamp::consensusFill(query, candidates, 0.99);
  check(fill.album, "consenso preenche álbum em que todas as edições concordam");
  check(fill.albumArtist, "consenso preenche artista do álbum junto");
  check(fill.year, "consenso preenche o ano pela exceção do mais antigo");
  check(!fill.title && !fill.artist, "campo já preenchido no arquivo não entra");
}

void testConsensusYearIsTheOldest() {
  wamp::TagQuery query = exactQuery();
  query.year = 0;
  std::vector<wamp::TagCandidate> candidates{edition(2013, "Ring Ring", 1),
                                             edition(1973, "Ring Ring", 1),
                                             edition(1990, "Ring Ring", 1)};
  const auto fill = wamp::consensusFill(query, candidates, 0.99);
  check(fill.year, "ano entra quando a defasagem é de edição");
}

void testConsensusRefusesHugeYearSpread() {
  wamp::TagQuery query = exactQuery();
  query.year = 0;
  std::vector<wamp::TagCandidate> candidates{edition(1950, "Ring Ring", 1),
                                             edition(2020, "Ring Ring", 1)};
  // 70 anos de diferença no mesmo álbum: dado absurdo, não edição defasada.
  const auto fill = wamp::consensusFill(query, candidates, 0.99);
  check(!fill.year, "defasagem acima do limite deixa o ano para a revisão");
}

void testConsensusIgnoresYearFromOtherAlbum() {
  wamp::TagQuery query = exactQuery();
  query.year = 0;
  std::vector<wamp::TagCandidate> candidates{edition(0, "Ring Ring", 1),
                                             edition(1970, "Outra Coisa", 1)};
  const auto fill = wamp::consensusFill(query, candidates, 0.99);
  check(!fill.year, "ano de álbum diferente não preenche");
}

void testConsensusTakesTrackOnlyWhenExact() {
  wamp::TagQuery query = exactQuery();
  query.trackNo = 0;
  std::vector<wamp::TagCandidate> candidates{edition(1973, "Ring Ring", 1),
                                             edition(2013, "Ring Ring", 2)};
  const auto fill = wamp::consensusFill(query, candidates, 0.99);
  check(!fill.trackNo, "número de faixa divergente não entra pela exceção do ano");
}

void testConsensusNeedsPlausibleScore() {
  wamp::TagQuery query = exactQuery();
  query.album.clear();
  std::vector<wamp::TagCandidate> candidates{edition(1973, "Ring Ring", 1)};
  const auto fill = wamp::consensusFill(query, candidates, 0.40);
  check(!fill.any(), "abaixo da nota de plausibilidade o consenso não preenche nada");
}

}  // namespace

int main() {
  std::printf("autotag: casos de casamento\n");
  testExactMatchIsAuto();
  testAmbiguityAsksForReview();
  testWrongArtistIsNotAuto();
  testQueryWithoutAlbumRedistributesWeight();
  testDurationOutsideToleranceDecays();
  testPlanFillsOnlyBlankFields();
  testNoCandidatesMeansNone();
  testWorthSearching();
  testRemasterSuffixDoesNotBreakMatch();
  testConsensusFillsBlankFieldsWhenEditionsAgree();
  testConsensusYearIsTheOldest();
  testConsensusRefusesHugeYearSpread();
  testConsensusIgnoresYearFromOtherAlbum();
  testConsensusTakesTrackOnlyWhenExact();
  testConsensusNeedsPlausibleScore();

  if (failures == 0) {
    std::printf("autotag: todos os casos passaram\n");
    return 0;
  }
  std::printf("autotag: %d falha(s)\n", failures);
  return 1;
}
