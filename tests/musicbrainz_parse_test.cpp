// Teste do analisador de resposta do MusicBrainz: JSON gravado, sem rede.
//
// O que importa aqui é que a conversão não invente campo: o que o serviço não manda fica vazio,
// e o que ele manda chega no candidato com ano, faixa, disco e duração.

#include "musicbrainz.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("  FALHOU: %s\n", what);
    ++failures;
  }
}

const char* kRecordingJson = R"({
  "created": "2026-11-26T19:00:00.000Z",
  "count": 1,
  "recordings": [
    {
      "id": "11111111-2222-3333-4444-555555555555",
      "title": "All My Life",
      "length": 260800,
      "artist-credit": [
        { "name": "Foo Fighters", "artist": { "id": "a", "name": "Foo Fighters" } }
      ],
      "releases": [
        {
          "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
          "title": "One by One",
          "date": "2002-10-22",
          "artist-credit": [ { "name": "Foo Fighters" } ],
          "media": [
            {
              "position": 1,
              "track": [ { "number": "1", "title": "All My Life", "length": 260800 } ]
            }
          ]
        }
      ]
    }
  ]
})";

const char* kEmptyJson = R"({ "count": 0, "recordings": [] })";

void testRecordingParsing() {
  QString error;
  const auto candidates = wamp::ui::parseRecordingSearch(QByteArray(kRecordingJson), &error);
  check(error.isEmpty(), "JSON válido não devolve erro");
  check(candidates.size() == 1, "um candidato é lido");

  const wamp::TagCandidate& candidate = candidates.front();
  check(candidate.title == "All My Life", "título vem do serviço");
  check(candidate.artist == "Foo Fighters", "artista vem do crédito");
  check(candidate.album == "One by One", "álbum vem do release");
  check(candidate.albumArtist == "Foo Fighters", "artista do álbum vem do release");
  check(candidate.year == 2002, "ano sai da data");
  check(candidate.trackNo == 1, "número de faixa vem da mídia");
  check(candidate.discNo == 1, "número de disco vem da mídia");
  check(candidate.durationMs == 260800, "duração em milissegundos");
  check(!candidate.id.empty(), "o candidato carrega o identificador");
}

void testEmptyIsValidAnswer() {
  QString error;
  const auto candidates = wamp::ui::parseRecordingSearch(QByteArray(kEmptyJson), &error);
  check(candidates.empty(), "nada encontrado devolve lista vazia");
  check(error.isEmpty(), "nada encontrado não é erro");
}

void testBrokenJsonReportsError() {
  QString error;
  const auto candidates = wamp::ui::parseRecordingSearch(QByteArray("{ nao é json"), &error);
  check(candidates.empty(), "JSON inválido não produz candidato");
  check(!error.isEmpty(), "JSON inválido devolve mensagem");
}

void testReleaseParsing() {
  const char* json = R"({
    "releases": [
      {
        "id": "bbbbbbbb-cccc-dddd-eeee-ffffffffffff",
        "title": "One by One",
        "date": "2002",
        "artist-credit": [ { "name": "Foo Fighters" } ],
        "media": [ { "position": 1, "track": [ { "number": "1", "title": "All My Life", "length": 260000 } ] } ]
      }
    ]
  })";

  QString error;
  const auto candidates = wamp::ui::parseReleaseSearch(QByteArray(json), &error);
  check(error.isEmpty(), "release válido não devolve erro");
  check(candidates.size() == 1, "um release é lido");
  check(candidates.front().album == "One by One", "release traz o título do álbum");
  check(candidates.front().year == 2002, "release traz o ano");
  check(candidates.front().trackNo == 1, "release traz a faixa de referência");
}

}  // namespace

int main() {
  std::printf("musicbrainz: análise de resposta\n");
  testRecordingParsing();
  testEmptyIsValidAnswer();
  testBrokenJsonReportsError();
  testReleaseParsing();

  if (failures == 0) {
    std::printf("musicbrainz: todos os casos passaram\n");
    return 0;
  }
  std::printf("musicbrainz: %d falha(s)\n", failures);
  return 1;
}
