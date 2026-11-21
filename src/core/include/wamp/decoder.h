#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "wamp/types.h"

namespace wamp {

/// Decodificador de um arquivo/stream. Sempre entrega o formato interno (D-004):
/// F32 intercalado, 48 kHz, estéreo — independente do que estiver no arquivo.
class IDecoder {
 public:
  virtual ~IDecoder() = default;

  virtual bool open(const std::string& path, std::string* error) = 0;
  virtual void close() = 0;

  /// Lê até `frames` frames. Retorna frames escritos, 0 no fim, -1 em erro.
  virtual int read(float* dst, int frames) = 0;

  virtual bool seek(std::int64_t positionMs) = 0;

  virtual std::int64_t durationMs() const = 0;
  virtual TrackInfo info() const = 0;
};

std::unique_ptr<IDecoder> makeFFmpegDecoder();

}  // namespace wamp
