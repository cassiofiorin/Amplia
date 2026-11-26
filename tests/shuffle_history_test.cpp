// A memória do sorteio: com ⇄ ligado, "anterior" tem de andar no que foi ouvido, não na
// ordem da lista. Este teste não abre áudio, janela nem rede — só a playlist.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "wamp/playlist.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (condition) return;
  ++failures;
  std::cerr << "FALHOU: " << what << "\n";
}

std::vector<std::string> paths(const char* prefix, int count) {
  std::vector<std::string> out;
  for (int i = 0; i < count; ++i) {
    out.push_back(std::string("/musica/") + prefix + "/" + std::to_string(i) + ".mp3");
  }
  return out;
}

}  // namespace

int main() {
  {
    wamp::Playlist playlist;
    playlist.addAll(paths("a", 5));

    // Nada tocado ainda: não há para onde voltar.
    check(playlist.shuffledPreviousIndex() == wamp::Playlist::kNone,
          "sem histórico, anterior é nenhum");

    // O sorteio tocou 3 e depois 1. "Anterior" é a faixa 3, não a 0 da lista.
    playlist.noteShufflePlay(3);
    check(playlist.shuffledPreviousIndex() == wamp::Playlist::kNone,
          "uma faixa só no histórico não dá anterior");
    playlist.noteShufflePlay(1);
    check(playlist.shuffledPreviousIndex() == 3, "anterior é a última que tocou");

    // Voltar uma vez e tocar de novo não pode duplicar a entrada: senão "anterior" fica
    // preso na mesma faixa para sempre.
    playlist.dropLastShufflePlay();
    check(playlist.shuffledPreviousIndex() == wamp::Playlist::kNone,
          "depois de desfazer, o histórico volta a uma faixa");
    playlist.noteShufflePlay(1);  // voltou a tocar a 1 — mesma do topo, não duplica
    check(playlist.shuffledPreviousIndex() == 3, "a faixa repetida continua com a 3 atrás");
  }

  {
    // A lista pode mudar de ordem (ordenar, arrastar) depois de tocar: o histórico guarda
    // caminhos, então "anterior" continua achando a faixa certa no índice novo.
    wamp::Playlist playlist;
    playlist.addAll(paths("b", 4));
    playlist.noteShufflePlay(0);
    playlist.noteShufflePlay(3);
    std::vector<std::string> reversed = playlist.items();
    std::reverse(reversed.begin(), reversed.end());
    playlist.setItems(reversed, wamp::Playlist::kNone);
    const std::size_t previous = playlist.shuffledPreviousIndex();
    check(previous != wamp::Playlist::kNone, "acha a anterior mesmo com a lista reordenada");
    check(playlist.at(previous) == "/musica/b/0.mp3", "e é a faixa que tocou, não o índice");
  }

  {
    // Faixa removida do disco: o histórico aponta para um caminho que já não está na lista.
    wamp::Playlist playlist;
    playlist.addAll(paths("c", 3));
    playlist.noteShufflePlay(1);
    playlist.noteShufflePlay(2);
    playlist.removeAt(1);
    check(playlist.shuffledPreviousIndex() == wamp::Playlist::kNone,
          "autoria anterior removida da lista devolve nenhum (quem chama cai na ordem)");

    // resetShuffle (ligar o sorteio) começa uma memória nova.
    playlist.resetShuffle();
    check(playlist.shuffledPreviousIndex() == wamp::Playlist::kNone,
          "reset limpa a memória do sorteio");
  }

  if (failures == 0) {
    std::cout << "shuffle history: todos os casos passaram\n";
    return EXIT_SUCCESS;
  }
  std::cerr << failures << " caso(s) falharam\n";
  return EXIT_FAILURE;
}
