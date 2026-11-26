// Teste da identidade da obra (`norm/v1`). Puro: sem Qt, sem áudio, sem rede, sem banco.
//
// A regra que o teste protege é a de docs/COMPARTILHAMENTO.md §2: a mesma obra tem que cair na
// mesma chave em rip, remaster e pasta diferente; gravação diferente (ao vivo, remix, demo) tem
// que cair em chave diferente.
#include "wamp/work_key.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FALHA", what);
  if (!ok) ++failures;
}

std::string key(const char* artist, const char* album, const char* title) {
  return wamp::workKey(artist, album, title);
}

std::string norm(wamp::WorkField field, const char* value) {
  return wamp::normalizeWorkField(field, value);
}

}  // namespace

int main() {
  // SHA-1 contra os vetores conhecidos: se o hash estiver errado, todo o dataset muda.
  expect(wamp::sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 de 'abc'");
  expect(wamp::sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 de vazio");

  // Unicode: acento e caixa não criam obra nova.
  expect(norm(wamp::WorkField::Artist, "José") == "jose", "acento sai do artista");
  expect(norm(wamp::WorkField::Title, "ESTAÇÃO") == "estacao", "acento e caixa saem do título");
  expect(norm(wamp::WorkField::Title, "  Música   Nova\t") == "musica nova", "espaços colapsam");
  expect(key("Björk", "Homogénic", "Jóga") == key("Bjork", "Homogenic", "Joga"), "Björk cai na mesma chave");

  // Mesma obra em tiragens e cópias diferentes.
  const std::string base = key("a-ha", "Analogue", "Celice");
  expect(base == key("a-ha", "Analogue", "Celice (Remastered 2015)"), "remaster com ano");
  expect(base == key("a-ha", "Analogue", "Celice - Remastered"), "remaster depois de traço");
  expect(base == key("a-ha", "Analogue", "Celice [Deluxe Edition]"), "deluxe entre colchetes");
  expect(base == key("a-ha", "Analogue", "01 Celice"), "número de faixa com espaço");
  expect(base == key("a-ha", "Analogue", "1. Celice"), "número de faixa com ponto");
  expect(base == key("a-ha", "Analogue", "03- Celice"), "número de faixa com traço");
  expect(base == key("a-ha", "Analogue", "Celice (feat. Fulano)"), "convidado entre parênteses");
  expect(base == key("a-ha", "Analogue", "Celice feat. Fulano"), "convidado solto");
  expect(base == key("a-ha", "Analogue", "Celice."), "pontuação final");
  expect(base == key("a-ha", "Analogue", "Celice (Explicit)"), "explicit");
  expect(base == key("a-ha", "Analogue (CD1)", "Celice"), "marcador cd entre parênteses");
  expect(base == key("a-ha", "Analogue (disc 2)", "Celice"), "marcador disc entre parênteses");
  expect(base == key("a-ha", "Analogue cd1", "Celice"), "marcador cd solto");
  expect(base == key("A-HA", " ANALOGUE ", "celice"), "caixa e espaços");

  // Gravação diferente é obra diferente — isso não pode regredir.
  expect(base != key("a-ha", "Analogue", "Celice (Live)"), "live");
  expect(base != key("a-ha", "Analogue", "Celice (Ao Vivo)"), "ao vivo");
  expect(base != key("a-ha", "Analogue", "Celice (Acoustic)"), "acústico");
  expect(base != key("a-ha", "Analogue", "Celice (Remix)"), "remix");
  expect(base != key("a-ha", "Analogue", "Celice (Demo)"), "demo");
  expect(base != key("a-ha", "Analogue", "Celice (Radio Edit)"), "radio edit");
  expect(base != key("a-ha", "Analogue", "Celice (Instrumental)"), "instrumental");
  expect(base != key("a-ha", "Analogue", "Celice (Reprise)"), "reprise");

  // Apóstrofo e & ficam como estão: mudam o nome da obra.
  expect(key("She & Him", "Volume One", "Why Do You Let Me Stay Here?") !=
             key("She and Him", "Volume One", "Why Do You Let Me Stay Here?"),
         "& não vira 'and'");
  expect(key("Guns N' Roses", "Appetite", "Sweet Child O' Mine") !=
             key("Guns N Roses", "Appetite", "Sweet Child O Mine"),
         "apóstrofo fica");
  expect(norm(wamp::WorkField::Artist, "Fulano feat. Beltrano") == "fulano feat. beltrano",
         "o artista guarda o 'feat.'");

  // Duração fica fora da chave, em segundos cheios.
  expect(wamp::workDurationSeconds(221048) == 221, "duração em segundos");
  expect(wamp::workDurationSeconds(0) == 0, "duração ausente");
  expect(wamp::kWorkKeyVersion == std::string("norm/v1"), "versão da regra");

  if (failures != 0) {
    std::printf("\n%d falha(s)\n", failures);
    return 1;
  }
  std::printf("\ntudo ok\n");
  return 0;
}
