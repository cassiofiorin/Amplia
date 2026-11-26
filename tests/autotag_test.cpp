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

  if (failures == 0) {
    std::printf("autotag: todos os casos passaram\n");
    return 0;
  }
  std::printf("autotag: %d falha(s)\n", failures);
  return 1;
}
