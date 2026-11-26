#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace wamp {

/// Lista de reprodução: só caminhos. Metadados são lidos do decoder quando a faixa
/// toca — manter a playlist em memória leve deixa a importação de milhares de
/// arquivos instantânea.
///
/// A fila (queue) é a "JTF" do Winamp: itens que tocam antes de seguir a ordem normal.
class Playlist {
 public:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  std::size_t add(std::string path);
  /// Adiciona vários; devolve o índice do primeiro item inserido (kNone se nenhum).
  std::size_t addAll(const std::vector<std::string>& paths);
  bool removeAt(std::size_t index);
  void clear();

  std::size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const std::string& at(std::size_t index) const { return items_.at(index); }
  const std::vector<std::string>& items() const { return items_; }

  /// Troca a ordem inteira (ordenar/inverter/embaralhar). `current` pode ser `kNone`.
  void setItems(std::vector<std::string> items, std::size_t current);

  /// Inverte a ordem da lista; a faixa atual continua sendo a mesma.
  void reverseOrder();

  /// Embaralha a ordem da lista (isso não é o sorteio de reprodução do botão ⇄).
  void shuffleOrder();

  /// Tira da lista as faixas que não existem mais no disco. Devolve quantas saíram.
  std::size_t removeMissingFiles();

  void setCurrent(std::size_t index);
  std::size_t currentIndex() const { return current_; }
  bool hasCurrent() const { return current_ < items_.size(); }

  /// Próxima/anterior na ordem da playlist, com volta ao início. kNone se vazia.
  std::size_t nextIndex() const;
  std::size_t previousIndex() const;
  bool isLast(std::size_t index) const { return !items_.empty() && index + 1 >= items_.size(); }

  /// Fila: toca antes de seguir a ordem da playlist.
  void enqueue(std::size_t index);
  std::string dequeue();
  /// Primeiro da fila sem consumir ("" quando vazia) — usado para prefetch.
  std::string queueFront() const { return queue_.empty() ? std::string() : queue_.front(); }
  std::size_t queueSize() const { return queue_.size(); }
  bool queueEmpty() const { return queue_.empty(); }

  /// Posição na fila, começando em 1; 0 quando o item não está enfileirado.
  std::size_t queuePosition(std::size_t index) const;
  /// Tira da fila o primeiro item com o caminho do índice dado. false se não estava.
  bool removeFromQueue(std::size_t index);

  /// Próxima faixa com sorteio: `level` 0 é sequencial e 1 sorteia por toda a lista.
  /// A janela de sorteio cresce com o nível — é o "shuffle morph rate" do Winamp.
  std::size_t shuffledNextIndex(double level);
  /// Esquece o histórico recente (ao ligar o sorteio ou trocar a playlist).
  void resetShuffle();

  /// Registra no histórico do sorteio a faixa que começou a tocar. Repetir a mesma faixa
  /// não duplica a entrada — é o que mantém "anterior" coerente depois de voltar uma vez.
  void noteShufflePlay(std::size_t index);

  /// Anterior **na ordem do sorteio**: a faixa que tocou imediatamente antes desta, mesmo
  /// que na lista ela esteja longe. `kNone` quando não há histórico suficiente.
  std::size_t shuffledPreviousIndex() const;

  /// Desfaz a última entrada do histórico (quem chama já vai tocar a anterior).
  void dropLastShufflePlay();

  /// Formato .m3u simples (uma linha por caminho, `#` como comentário). Caminhos
  /// relativos são resolvidos a partir do diretório do arquivo.
  /// Formatos: .m3u/.m3u8, .pls, .b4s e .wpl (ver `playlist_file.h`).
  bool loadPlaylist(const std::string& file, std::string* error);
  bool savePlaylist(const std::string& file, std::string* error) const;

 private:
  bool recentlyPlayed(std::size_t index) const;

  std::vector<std::string> items_;
  std::vector<std::string> queue_;
  std::size_t current_ = kNone;

  std::mt19937 rng_{std::random_device{}()};
  /// Últimas faixas sorteadas, para não repetir logo em seguida.
  std::vector<std::string> recent_;
  /// O caminho que o sorteio percorreu, na ordem em que foi tocado. É a memória do botão
  /// "anterior": com ⇄ ligado, voltar significa voltar no que se ouviu, não na lista.
  std::vector<std::string> history_;
};

}  // namespace wamp
