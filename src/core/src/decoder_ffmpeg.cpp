#include "wamp/decoder.h"

#include "track_metadata.h"

#include "wamp/file_info.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace wamp {
namespace {

std::string errString(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof(buf));
  return std::string(buf);
}

void fail(std::string* out, const char* what, int code) {
  if (out != nullptr) *out = std::string(what) + ": " + errString(code);
}


class FFmpegDecoder final : public IDecoder {
 public:
  ~FFmpegDecoder() override { close(); }

  bool open(const std::string& path, std::string* error) override {
    close();

    int ret = avformat_open_input(&format_, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
      fail(error, "avformat_open_input", ret);
      return false;
    }

    ret = avformat_find_stream_info(format_, nullptr);
    if (ret < 0) {
      fail(error, "avformat_find_stream_info", ret);
      return false;
    }

    const AVCodec* codec = nullptr;
    streamIndex_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (streamIndex_ < 0 || codec == nullptr) {
      if (error != nullptr) *error = "nenhuma faixa de áudio encontrada";
      return false;
    }

    AVStream* stream = format_->streams[streamIndex_];
    codec_ = avcodec_alloc_context3(codec);
    if (codec_ == nullptr) {
      if (error != nullptr) *error = "avcodec_alloc_context3 falhou";
      return false;
    }

    ret = avcodec_parameters_to_context(codec_, stream->codecpar);
    if (ret < 0) {
      fail(error, "avcodec_parameters_to_context", ret);
      return false;
    }
    codec_->pkt_timebase = stream->time_base;

    ret = avcodec_open2(codec_, codec, nullptr);
    if (ret < 0) {
      fail(error, "avcodec_open2", ret);
      return false;
    }

    if (codec_->ch_layout.nb_channels <= 0) {
      av_channel_layout_default(&codec_->ch_layout, kChannels);
    }
    if (!createResampler()) {
      if (error != nullptr) *error = "swresample: nao foi possivel inicializar";
      return false;
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (packet_ == nullptr || frame_ == nullptr) {
      if (error != nullptr) *error = "ffmpeg: falha alocando frame/packet";
      return false;
    }

    fillInfo(path, stream);
    return true;
  }

  void close() override {
    swr_free(&swr_);
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&codec_);
    if (format_ != nullptr) avformat_close_input(&format_);
    pending_.clear();
    pendingOffset_ = 0;
    streamIndex_ = -1;
    durationMs_ = 0;
    eof_ = false;
    sentFlush_ = false;
    info_ = TrackInfo{};
  }

  int read(float* dst, int frames) override {
    if (dst == nullptr || frames <= 0) return 0;
    int written = 0;
    while (written < frames) {
      if (pendingOffset_ >= pending_.size()) {
        pending_.clear();
        pendingOffset_ = 0;
        if (!decodeMore()) break;
        if (pending_.empty()) break;
      }
      const std::size_t available = (pending_.size() - pendingOffset_) / kChannels;
      const std::size_t want = std::min<std::size_t>(
          static_cast<std::size_t>(frames - written), available);
      std::memcpy(dst + static_cast<std::size_t>(written) * kChannels,
                  pending_.data() + pendingOffset_, want * kChannels * sizeof(float));
      pendingOffset_ += want * kChannels;
      written += static_cast<int>(want);
    }
    return written;
  }

  bool seek(std::int64_t positionMs) override {
    if (format_ == nullptr || streamIndex_ < 0) return false;
    AVStream* stream = format_->streams[streamIndex_];
    const AVRational ms = {1, 1000};
    const std::int64_t target = av_rescale_q(positionMs, ms, stream->time_base);
    const int ret = av_seek_frame(format_, streamIndex_, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return false;
    if (codec_ != nullptr) avcodec_flush_buffers(codec_);
    resetResampler();
    pending_.clear();
    pendingOffset_ = 0;
    eof_ = false;
    sentFlush_ = false;
    return true;
  }

  std::int64_t durationMs() const override { return durationMs_; }
  TrackInfo info() const override { return info_; }

 private:
  bool createResampler() {
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kChannels);
    const int ret = swr_alloc_set_opts2(&swr_, &outLayout, AV_SAMPLE_FMT_FLT, kSampleRate,
                                        &codec_->ch_layout, codec_->sample_fmt,
                                        codec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outLayout);
    if (ret < 0 || swr_ == nullptr) return false;
    return swr_init(swr_) >= 0;
  }

  void resetResampler() {
    swr_free(&swr_);
    if (codec_ != nullptr) createResampler();
  }

  void fillInfo(const std::string& path, AVStream* stream) {
    // Mesma extração usada pela fila de metadados da playlist: o que a lista mostra é
    // exatamente o que o player carregou.
    detail::fillTrackInfo(format_, stream, codec_, &info_);
    info_.path = path;
  info_.modifiedMs = wamp::fileModifiedMs(path);
    durationMs_ = info_.durationMs;
  }

  /// Puxa do demuxer até obter um frame decodificado (ou EOF).
  bool decodeMore() {
    if (codec_ == nullptr) return false;
    while (true) {
      const int ret = avcodec_receive_frame(codec_, frame_);
      if (ret == 0) {
        convertFrame(frame_);
        av_frame_unref(frame_);
        return !pending_.empty();
      }
      if (ret == AVERROR_EOF) {
        eof_ = true;
        return false;
      }
      if (ret != AVERROR(EAGAIN)) return false;

      // Precisa de mais pacotes.
      while (true) {
        const int rret = av_read_frame(format_, packet_);
        if (rret == AVERROR_EOF) {
          if (!sentFlush_) {
            sentFlush_ = true;
            avcodec_send_packet(codec_, nullptr);  // drenar o decoder
          }
          break;
        }
        if (rret < 0) {
          eof_ = true;
          return false;
        }
        if (packet_->stream_index != streamIndex_) {
          av_packet_unref(packet_);
          continue;
        }
        // TODO(F1): honrar AV_PKT_DATA_SKIP_SAMPLES para gapless exato.
        const int sret = avcodec_send_packet(codec_, packet_);
        av_packet_unref(packet_);
        if (sret < 0 && sret != AVERROR(EAGAIN)) return false;
        break;
      }
    }
  }

  void convertFrame(AVFrame* frame) {
    if (swr_ == nullptr) return;
    const int outMax = swr_get_out_samples(swr_, frame->nb_samples);
    if (outMax <= 0) return;

    const std::size_t base = pending_.size();
    pending_.resize(base + static_cast<std::size_t>(outMax) * kChannels);

    uint8_t* out = reinterpret_cast<uint8_t*>(pending_.data() + base);
    const uint8_t** in = const_cast<const uint8_t**>(frame->extended_data);
    const int got = swr_convert(swr_, &out, outMax, in, frame->nb_samples);
    if (got <= 0) {
      pending_.resize(base);
      return;
    }
    pending_.resize(base + static_cast<std::size_t>(got) * kChannels);
  }

  AVFormatContext* format_ = nullptr;
  AVCodecContext* codec_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  SwrContext* swr_ = nullptr;

  int streamIndex_ = -1;
  std::int64_t durationMs_ = 0;
  bool eof_ = false;
  bool sentFlush_ = false;

  /// Amostras já convertidas para o formato interno, aguardando entrega.
  std::vector<float> pending_;
  std::size_t pendingOffset_ = 0;

  TrackInfo info_;
};

}  // namespace

std::unique_ptr<IDecoder> makeFFmpegDecoder() { return std::make_unique<FFmpegDecoder>(); }

}  // namespace wamp
