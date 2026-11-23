#include "wamp/player_engine.h"

#include "wamp/equalizer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "wamp/audio_output.h"
#include "wamp/decoder.h"
#include "wamp/ring_buffer.h"

namespace wamp {
namespace {

constexpr int kChunkFrames = 1024;                        ///< por leitura do decoder
constexpr std::size_t kRingFrames = kSampleRate * 2;      ///< 2 s de buffer
constexpr std::size_t kPrebufferFrames = kSampleRate / 5; ///< 200 ms antes do play
constexpr std::int64_t kGaplessLeadMs = 2000;             ///< abre a próxima faixa 2 s antes

std::uint64_t framesForMs(std::int64_t ms) {
  if (ms <= 0) return 0;
  return static_cast<std::uint64_t>(ms) * kSampleRate / 1000;
}

std::int64_t msForFrames(std::uint64_t frames) {
  return static_cast<std::int64_t>(frames * 1000 / kSampleRate);
}

}  // namespace

struct PlayerEngine::Impl {
  mutable std::mutex mutex;
  std::condition_variable cv;

  std::thread worker;
  bool running = true;

  std::unique_ptr<IAudioOutput> output;
  std::unique_ptr<IDecoder> decoder;
  FloatRing ring{kRingFrames, kChannels};
  // O EQ roda aqui, na thread de controle, antes do ring: a thread de áudio continua
  // só copiando amostras.
  Equalizer equalizer;
  AudioAnalyzer analyzer;

  std::atomic<PlaybackState> state{PlaybackState::Idle};
  std::atomic<std::uint64_t> playedFrames{0};
  std::atomic<std::int64_t> durationMs{0};
  std::atomic<float> gainTarget{1.0f};
  std::atomic<float> replayGain{1.0f};
  std::atomic<bool> eof{false};

  // Protege o `decoder` e o seu uso. Separado do `mutex` de estado de proposito: a
  // leitura do arquivo e I/O e pode demorar (NAS), e nao pode segurar o mutex que a
  // UI usa para play/pause/volume.
  std::mutex decoderMutex;

  TrackInfo track;
  std::string error;
  Callbacks callbacks;

  // Estado da thread de controle.
  std::vector<float> chunk = std::vector<float>(kChunkFrames * kChannels, 0.0f);
  // Escrita pela thread de controle, lida pela UI (play/pause/stop): atomico para
  // nao ser um data race — o valor e so um "ja liberei o audio".
  std::atomic<bool> prebuffered{false};

  // Gapless: a próxima faixa é decodificada antes do fim da atual e entra no ring
  // sem parar o PipeWire. Os metadados só trocam quando o áudio dela começa a
  // soar — até lá a UI continua mostrando a faixa que está tocando.
  std::string nextPath;
  std::unique_ptr<IDecoder> nextDecoder;
  TrackInfo nextTrackInfo;
  std::int64_t nextDurationMs = 0;
  std::atomic<std::uint64_t> switchAfterFrames{0};
  std::atomic<bool> switchPending{false};

  // Estado da thread de áudio.
  float smoothGain = 1.0f;

  // ---- thread de áudio -------------------------------------------------

  void pull(float* dst, int frames) {
    const std::size_t got = ring.read(dst, static_cast<std::size_t>(frames));
    if (got < static_cast<std::size_t>(frames)) {
      std::memset(dst + got * kChannels, 0,
                  (static_cast<std::size_t>(frames) - got) * kChannels * sizeof(float));
    }
    applyGain(dst, frames);
    playedFrames.fetch_add(got, std::memory_order_relaxed);

    if (got > 0) {
      for (;;) {
        std::uint64_t left = switchAfterFrames.load(std::memory_order_relaxed);
        if (left == 0) break;
        const std::uint64_t take = std::min<std::uint64_t>(left, got);
        if (switchAfterFrames.compare_exchange_weak(left, left - take,
                                                    std::memory_order_relaxed)) {
          if (left == take) switchPending.store(true, std::memory_order_relaxed);
          break;
        }
      }
    }
  }

  void applyGain(float* dst, int frames) {
    const float target = gainTarget.load(std::memory_order_relaxed) *
                         replayGain.load(std::memory_order_relaxed);
    if (std::fabs(target - smoothGain) < 1e-4f) {
      smoothGain = target;
      if (target != 1.0f) {
        for (int i = 0; i < frames * kChannels; ++i) dst[i] *= target;
      }
      return;
    }
    const float step = (target - smoothGain) / static_cast<float>(frames);
    for (int f = 0; f < frames; ++f) {
      smoothGain += step;
      dst[f * kChannels + 0] *= smoothGain;
      dst[f * kChannels + 1] *= smoothGain;
    }
  }

  // ---- thread de controle ----------------------------------------------

  void fail(std::string message) {
    Callbacks cb;
    {
      std::lock_guard<std::mutex> lock(mutex);
      error = message;
      state.store(PlaybackState::Error);
      if (output) output->setPaused(true);
      cb = callbacks;
    }
    if (cb.onError) cb.onError(message);
    cv.notify_all();
  }

  /// Anuncia a faixa que começou a soar depois de uma transição gapless.
  void applyPendingSwitch() {
    TrackInfo info;
    Callbacks cb;
    {
      std::lock_guard<std::mutex> lock(mutex);
      track = nextTrackInfo;
      durationMs.store(nextDurationMs);
      playedFrames.store(0);
      info = track;
      cb = callbacks;
    }
    if (cb.onTrackChanged) cb.onTrackChanged(info);
  }

  /// Abre o decoder da próxima faixa (idempotente). Chamada da thread de controle.
  bool openNextDecoder() {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!nextDecoder && !nextPath.empty()) path = nextPath;
    }
    if (path.empty()) {
      std::lock_guard<std::mutex> lock(mutex);
      return nextDecoder != nullptr;
    }

    auto decoder = makeFFmpegDecoder();
    std::string openError;
    if (!decoder->open(path, &openError)) {
      std::lock_guard<std::mutex> lock(mutex);
      if (nextPath == path) nextPath.clear();
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (nextPath != path) return false;  // a UI trocou a próxima enquanto abria
    nextDecoder = std::move(decoder);
    return true;
  }

  void maybePrefetch() {
    const std::int64_t total = durationMs.load();
    if (total <= 0) return;  // sem duração confiável: só na virada do EOF
    if (msForFrames(playedFrames.load(std::memory_order_relaxed)) + kGaplessLeadMs < total) return;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (nextDecoder || nextPath.empty()) return;
    }
    openNextDecoder();
  }

  /// Troca o decoder no EOF; a troca de metadados fica pendente até o áudio virar.
  bool beginNextTrack() {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (nextPath.empty()) return false;
      path = nextPath;
    }
    if (!openNextDecoder()) return false;

    std::lock_guard<std::mutex> lock(mutex);
    if (!nextDecoder || nextPath != path) return false;
    decoder = std::move(nextDecoder);
    nextPath.clear();
    nextTrackInfo = decoder->info();
    nextDurationMs = decoder->durationMs();
    eof.store(false);
    const std::uint64_t queued = ring.availableFrames();
    switchAfterFrames.store(queued);
    if (queued == 0) switchPending.store(true);
    return true;
  }

  void notifyEnded() {
    Callbacks cb;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (output) output->setPaused(true);
      state.store(PlaybackState::Idle);
      prebuffered = false;
      cb = callbacks;
    }
    if (cb.onEnded) cb.onEnded();
  }

  void workerLoop() {
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] {
          return !running || state.load() == PlaybackState::Playing;
        });
        if (!running) return;
      }

      if (!prebuffered) {
        if (ring.availableFrames() < kPrebufferFrames && !eof.load()) {
          // ainda enchendo: decode abaixo cuida disso
        } else {
          prebuffered = true;
          if (output && state.load() == PlaybackState::Playing) output->setPaused(false);
        }
      }

      if (switchPending.exchange(false)) applyPendingSwitch();

      if (!eof.load()) maybePrefetch();

      if (eof.load()) {
        if (ring.availableFrames() == 0) {
          notifyEnded();
          continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }

      if (ring.freeFrames() < kChunkFrames) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));  // backpressure
        continue;
      }

      int got = 0;
      {
        std::lock_guard<std::mutex> lock(decoderMutex);
        if (decoder == nullptr) continue;
        got = decoder->read(chunk.data(), kChunkFrames);
      }

      if (got > 0) {
        equalizer.process(chunk.data(), got, kChannels);
        analyzer.push(chunk.data(), got, kChannels);
        ring.write(chunk.data(), static_cast<std::size_t>(got));
      } else if (got == 0) {
        if (!beginNextTrack()) eof.store(true);
      } else {
        // Decoder quebrado: marca EOF para não insistir; a thread continua viva.
        eof.store(true);
        fail("erro ao decodificar");
      }
    }
  }

  void startOutput() {
    std::string startError;
    output = makePipeWireOutput();
    if (!output->start([this](float* dst, int frames) { pull(dst, frames); }, &startError)) {
      error = startError;
      output.reset();
      state.store(PlaybackState::Error);
    }
  }
};

PlayerEngine::PlayerEngine() : impl_(std::make_unique<Impl>()) {
  impl_->startOutput();
  impl_->worker = std::thread([impl = impl_.get()] { impl->workerLoop(); });
}

PlayerEngine::~PlayerEngine() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
    impl_->state.store(PlaybackState::Idle);
  }
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->output) impl_->output->stop();
}

void PlayerEngine::setCallbacks(Callbacks callbacks) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callbacks = std::move(callbacks);
}

void PlayerEngine::setNextTrack(const std::string& path) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->nextPath == path) return;
  impl_->nextPath = path;
  impl_->nextDecoder.reset();
}

bool PlayerEngine::load(const std::string& path) {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->state.store(PlaybackState::Loading);
    if (impl_->output) impl_->output->setPaused(true);
    impl_->ring.clear();
    impl_->equalizer.reset();
    impl_->analyzer.reset();  // sem isso o espectro congela no último bloco tocado
    impl_->playedFrames.store(0);
    impl_->eof.store(false);
    impl_->prebuffered = false;
    impl_->track = TrackInfo{};
    {
      std::lock_guard<std::mutex> io(impl_->decoderMutex);
      impl_->decoder.reset();
    }
    impl_->nextDecoder.reset();
    impl_->nextPath.clear();
    impl_->switchAfterFrames.store(0);
    impl_->switchPending.store(false);
  }
  impl_->cv.notify_all();

  auto decoder = makeFFmpegDecoder();
  std::string error;
  if (!decoder->open(path, &error)) {
    impl_->fail("falha ao abrir: " + error);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->track = decoder->info();
    impl_->durationMs.store(decoder->durationMs());
    {
      std::lock_guard<std::mutex> io(impl_->decoderMutex);
      impl_->decoder = std::move(decoder);
    }
    impl_->error.clear();
    impl_->state.store(PlaybackState::Paused);
  }
  impl_->cv.notify_all();
  return true;
}

void PlayerEngine::play() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->decoder == nullptr) return;
    if (impl_->state.load() == PlaybackState::Playing) return;

    // Depois de um fim de faixa, o decoder está em EOF: volta ao começo.
    if (impl_->eof.load()) {
      {
        std::lock_guard<std::mutex> io(impl_->decoderMutex);
        impl_->decoder->seek(0);
      }
      impl_->playedFrames.store(0);
      impl_->eof.store(false);
      impl_->prebuffered = false;
    }
    impl_->error.clear();
    impl_->state.store(PlaybackState::Playing);
    // Retomada depois de uma pausa: o ring continua cheio (pause() só silencia a
    // saída), mas o stream do PipeWire ficou pausado — a thread de controle não
    // reabre sozinha porque `prebuffered` já é true.
    if (impl_->output && impl_->prebuffered) impl_->output->setPaused(false);
  }
  impl_->cv.notify_all();
}

void PlayerEngine::pause() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.load() != PlaybackState::Playing) return;
    impl_->state.store(PlaybackState::Paused);
    if (impl_->output) impl_->output->setPaused(true);
  }
  impl_->cv.notify_all();
}

void PlayerEngine::togglePlayPause() {
  if (impl_->state.load() == PlaybackState::Playing) {
    pause();
  } else {
    play();
  }
}

void PlayerEngine::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->state.store(PlaybackState::Idle);
    if (impl_->output) impl_->output->setPaused(true);
    impl_->ring.clear();
    impl_->equalizer.reset();
    impl_->analyzer.reset();  // sem isso o espectro congela no último bloco tocado
    impl_->playedFrames.store(0);
    impl_->eof.store(false);
    impl_->prebuffered = false;
    impl_->nextDecoder.reset();
    impl_->switchAfterFrames.store(0);
    impl_->switchPending.store(false);
    if (impl_->decoder) {
      std::lock_guard<std::mutex> io(impl_->decoderMutex);
      impl_->decoder->seek(0);
    }
  }
  impl_->cv.notify_all();
}

void PlayerEngine::seek(std::int64_t positionMs) {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->decoder == nullptr) return;
    const bool playing = impl_->state.load() == PlaybackState::Playing;
    if (impl_->output) impl_->output->setPaused(true);

    // A faixa já foi trocada no EOF mas o áudio dela ainda não começou: o seek do
    // usuário é sobre a faixa visível, então reabre ela antes de reposicionar.
    std::lock_guard<std::mutex> io(impl_->decoderMutex);
    if (impl_->switchAfterFrames.load() > 0) {
      impl_->switchAfterFrames.store(0);
      impl_->switchPending.store(false);
      const std::string visible = impl_->track.path;
      auto reverted = makeFFmpegDecoder();
      std::string revertError;
      if (reverted->open(visible, &revertError)) impl_->decoder = std::move(reverted);
    }
    impl_->ring.clear();
    impl_->eof.store(false);
    impl_->prebuffered = false;
    if (!impl_->decoder->seek(positionMs)) {
      impl_->error = "seek falhou";
      return;
    }
    impl_->playedFrames.store(framesForMs(positionMs));
    if (playing) {
      // A thread de controle reabre o áudio quando o prebuffer estiver completo.
      impl_->state.store(PlaybackState::Playing);
    }
  }
  impl_->cv.notify_all();
}

PlaybackState PlayerEngine::state() const { return impl_->state.load(); }

std::int64_t PlayerEngine::positionMs() const {
  return msForFrames(impl_->playedFrames.load(std::memory_order_relaxed));
}

std::int64_t PlayerEngine::durationMs() const { return impl_->durationMs.load(); }

TrackInfo PlayerEngine::track() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->track;
}

std::string PlayerEngine::lastError() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->error;
}

void PlayerEngine::setEqualizerEnabled(bool enabled) { impl_->equalizer.setEnabled(enabled); }

bool PlayerEngine::equalizerEnabled() const { return impl_->equalizer.enabled(); }

void PlayerEngine::setEqualizerBands(const std::array<double, Equalizer::kBands>& gainsDb) {
  impl_->equalizer.setGains(gainsDb);
}

std::array<double, Equalizer::kBands> PlayerEngine::equalizerBands() const {
  return impl_->equalizer.gains();
}

void PlayerEngine::setEqualizerPreamp(double preampDb) { impl_->equalizer.setPreamp(preampDb); }

double PlayerEngine::equalizerPreamp() const { return impl_->equalizer.preamp(); }

AudioAnalyzer& PlayerEngine::analyzer() { return impl_->analyzer; }

std::int64_t PlayerEngine::analyzerLatencyFrames() const {
  // O que está no ring ainda não saiu na caixa: é exatamente o atraso entre o último
  // bloco decodificado (que o analisador viu) e o que o ouvido está ouvindo agora.
  return static_cast<std::int64_t>(impl_->ring.availableFrames());
}

void PlayerEngine::setReplayGainFactor(double factor) {
  // O ganho também passa pelo limitador do EQ; aqui só limitamos o absurdo.
  const float value = static_cast<float>(std::clamp(factor, 0.0, 16.0));
  impl_->replayGain.store(value, std::memory_order_relaxed);
}

double PlayerEngine::replayGainFactor() const {
  return static_cast<double>(impl_->replayGain.load(std::memory_order_relaxed));
}

void PlayerEngine::setVolume(float volume) {
  impl_->gainTarget.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

float PlayerEngine::volume() const { return impl_->gainTarget.load(std::memory_order_relaxed); }

}  // namespace wamp
