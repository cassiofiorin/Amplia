#pragma once

#include <cstdint>
#include <string>

namespace wamp {

/// Tamanho em bytes do arquivo (0 se não existir ou não for acessível).
std::int64_t fileSizeBytes(const std::string& path);

/// Data de modificação em milissegundos desde 1970 (0 se não existir).
/// Usada pelo cache de tags para decidir se o metadado guardado ainda vale.
std::int64_t fileModifiedMs(const std::string& path);

}  // namespace wamp
