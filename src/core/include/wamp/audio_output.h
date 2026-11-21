#pragma once

#include <functional>
#include <memory>
#include <string>

namespace wamp {

/// Saída de áudio. Uma implementação por backend (PipeWire, e futuramente ALSA).
/// O contrato é simples: alguém pede N frames intercalados, a saída entrega ao
/// dispositivo.
class IAudioOutput {
 public:
  /// Chamado na thread de áudio. Deve preencher `frames` frames intercalados;
  /// o que não for preenchido vira silêncio. Não alocar, não travar, não logar.
  using PullFn = std::function<void(float* dst, int frames)>;

  virtual ~IAudioOutput() = default;

  virtual bool start(PullFn pull, std::string* error) = 0;
  virtual void stop() = 0;

  /// Silencia a saída sem desconectar o stream (pause). A thread de áudio continua
  /// rodando e drenando o ring? Não: ela somente emite silêncio.
  virtual void setPaused(bool paused) = 0;

  virtual int sampleRate() const = 0;
  virtual int channels() const = 0;
};

std::unique_ptr<IAudioOutput> makePipeWireOutput();

}  // namespace wamp
