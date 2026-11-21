#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "wamp/types.h"

namespace wamp {

/// Fila de leitura de metadados, como o Winamp original fazia: a playlist entra
/// imediatamente (só com o nome do arquivo) e as tags chegam depois, uma faixa por vez,
/// numa thread própria. Ler em ordem mantém a leitura sequencial no disco e permite
/// cancelar o que ainda não começou quando a playlist é trocada.
class MetadataScanner {
 public:
  /// Chamado da thread de leitura; a UI deve devolver ao thread principal.
  using Callback = std::function<void(const std::string& path, const TrackInfo& info, bool ok)>;

  MetadataScanner();
  ~MetadataScanner();

  MetadataScanner(const MetadataScanner&) = delete;
  MetadataScanner& operator=(const MetadataScanner&) = delete;

  void setCallback(Callback callback);
  void enqueue(const std::string& path);
  void enqueueAll(const std::vector<std::string>& paths);

  /// Esquece o que ainda não começou (o item em leitura termina normalmente).
  void clearPending();

  /// Itens na fila, incluindo o que está sendo lido.
  std::size_t pending() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wamp
