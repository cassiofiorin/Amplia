#include "wamp/audio_output.h"

#include "wamp/types.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

namespace wamp {
namespace {

/// Saída PipeWire nativa (D-003).
///
/// O stream é criado com SPA_AUDIO_FORMAT_F32 no formato interno do pipeline, então
/// não há conversão no servidor de áudio. O callback roda com PW_STREAM_FLAG_RT_PROCESS:
/// nada aqui aloca, trava ou faz log.
class PipeWireOutput final : public IAudioOutput {
 public:
  PipeWireOutput() = default;
  ~PipeWireOutput() override { stop(); }

  bool start(PullFn pull, std::string* error) override {
    if (loop_ != nullptr) return true;
    pull_ = std::move(pull);

    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("wamp-audio", nullptr);
    if (loop_ == nullptr) {
      if (error != nullptr) *error = "pipewire: pw_thread_loop_new falhou";
      return false;
    }

    pw_thread_loop_lock(loop_);
    pw_thread_loop_start(loop_);

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_), "amplia",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, "Amplia",
                          PW_KEY_NODE_NAME, "amplia", nullptr),
        &events(), this);

    if (stream_ == nullptr) {
      pw_thread_loop_unlock(loop_);
      pw_thread_loop_stop(loop_);
      pw_thread_loop_destroy(loop_);
      loop_ = nullptr;
      if (error != nullptr) *error = "pipewire: pw_stream_new_simple falhou";
      return false;
    }

    std::uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    struct spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kSampleRate;
    info.channels = kChannels;

    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    const int ret = pw_stream_connect(
        stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                     PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(loop_);

    if (ret < 0) {
      if (error != nullptr) *error = "pipewire: pw_stream_connect falhou (servidor ativo?)";
      stop();
      return false;
    }
    return true;
  }

  void stop() override {
    if (loop_ == nullptr) return;
    pw_thread_loop_lock(loop_);
    if (stream_ != nullptr) {
      pw_stream_disconnect(stream_);
      pw_stream_destroy(stream_);
      stream_ = nullptr;
    }
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
    pw_deinit();
  }

  void setPaused(bool paused) override { paused_.store(paused, std::memory_order_relaxed); }

  int sampleRate() const override { return kSampleRate; }
  int channels() const override { return kChannels; }

 private:
  static const struct pw_stream_events& events() {
    static const struct pw_stream_events value = [] {
      struct pw_stream_events e = {};
      e.version = PW_VERSION_STREAM_EVENTS;
      e.process = &PipeWireOutput::onProcess;
      return e;
    }();
    return value;
  }

  static void onProcess(void* data) { static_cast<PipeWireOutput*>(data)->process(); }

  void process() {
    struct pw_buffer* buffer = nullptr;
    while ((buffer = pw_stream_dequeue_buffer(stream_)) != nullptr) {
      struct spa_buffer* spa = buffer->buffer;
      auto* dst = static_cast<float*>(spa->datas[0].data);

      if (dst == nullptr) {
        pw_stream_queue_buffer(stream_, buffer);
        continue;
      }

      const std::uint32_t stride = sizeof(float) * kChannels;
      std::uint32_t frames = spa->datas[0].maxsize / stride;
      if (buffer->requested != 0) frames = std::min<std::uint32_t>(frames, buffer->requested);

      if (paused_.load(std::memory_order_relaxed)) {
        std::memset(dst, 0, frames * stride);
      } else {
        // O pull preenche até `frames`; o que faltar já vem zerado por ele.
        pull_(dst, static_cast<int>(frames));
      }

      spa->datas[0].chunk->offset = 0;
      spa->datas[0].chunk->stride = stride;
      spa->datas[0].chunk->size = frames * stride;

      pw_stream_queue_buffer(stream_, buffer);
    }
  }

  pw_thread_loop* loop_ = nullptr;
  pw_stream* stream_ = nullptr;
  PullFn pull_;
  std::atomic<bool> paused_{true};
};

}  // namespace

std::unique_ptr<IAudioOutput> makePipeWireOutput() { return std::make_unique<PipeWireOutput>(); }

}  // namespace wamp
