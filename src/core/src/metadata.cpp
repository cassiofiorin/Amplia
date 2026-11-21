#include "wamp/metadata.h"

#include "track_metadata.h"

#include "wamp/file_info.h"

#include <cstdint>
#include <string>

namespace wamp::detail {
namespace {

std::string tag(AVDictionary* dict, const char* key) {
  if (dict == nullptr) return {};
  const AVDictionaryEntry* entry = av_dict_get(dict, key, nullptr, 0);
  if (entry == nullptr || entry->value == nullptr) return {};
  return std::string(entry->value);
}

std::string firstTag(AVDictionary* a, AVDictionary* b, const char* key) {
  std::string value = tag(a, key);
  return value.empty() ? tag(b, key) : value;
}

int toInt(const std::string& text) {
  if (text.empty()) return 0;
  try {
    return std::stoi(text);
  } catch (...) {
    return 0;
  }
}

/// Leva o texto de `date`/`year` a um ano de 4 dígitos ("2004-05-11" → 2004).
int toYear(const std::string& text) {
  if (text.size() < 4) return 0;
  try {
    return std::stoi(text.substr(0, 4));
  } catch (...) {
    return 0;
  }
}

}  // namespace

void fillTrackInfo(AVFormatContext* format, AVStream* stream, AVCodecContext* codec,
                   TrackInfo* info) {
  if (info == nullptr) return;

  AVDictionary* fmt = format != nullptr ? format->metadata : nullptr;
  AVDictionary* st = stream != nullptr ? stream->metadata : nullptr;

  *info = TrackInfo{};
  info->title = firstTag(fmt, st, "title");
  info->artist = firstTag(fmt, st, "artist");
  info->album = firstTag(fmt, st, "album");
  info->albumArtist = firstTag(fmt, st, "album_artist");
  info->genre = firstTag(fmt, st, "genre");
  info->year = toYear(firstTag(fmt, st, "date"));
  if (info->year == 0) info->year = toYear(firstTag(fmt, st, "year"));
  info->trackNo = toInt(firstTag(fmt, st, "track"));

  if (format != nullptr && format->pb != nullptr) {
    const std::int64_t size = avio_size(format->pb);
    if (size > 0) info->fileSize = size;
  }

  if (format != nullptr && format->duration != AV_NOPTS_VALUE && format->duration > 0) {
    info->durationMs = format->duration / (AV_TIME_BASE / 1000);
  } else if (stream != nullptr && stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
    info->durationMs = static_cast<std::int64_t>(stream->duration * av_q2d(stream->time_base) * 1000.0);
  } else {
    info->durationMs = 0;
  }

  if (codec != nullptr) {
    info->sampleRate = codec->sample_rate;
    info->channels = codec->ch_layout.nb_channels;
  } else if (stream != nullptr && stream->codecpar != nullptr) {
    info->sampleRate = stream->codecpar->sample_rate;
    info->channels = stream->codecpar->ch_layout.nb_channels;
  }

  std::int64_t bitrate = 0;
  if (stream != nullptr && stream->codecpar != nullptr) bitrate = stream->codecpar->bit_rate;
  if (bitrate <= 0 && codec != nullptr) bitrate = codec->bit_rate;
  if (bitrate <= 0 && format != nullptr) bitrate = format->bit_rate;
  info->bitrate = bitrate > 0 ? static_cast<int>(bitrate / 1000) : 0;
}

}  // namespace wamp::detail

namespace wamp {

bool probeTrack(const std::string& path, TrackInfo* info, std::string* error) {
  AVFormatContext* format = nullptr;
  int ret = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    if (error != nullptr) *error = "não foi possível abrir " + path;
    return false;
  }

  ret = avformat_find_stream_info(format, nullptr);
  if (ret < 0) {
    avformat_close_input(&format);
    if (error != nullptr) *error = "não foi possível ler " + path;
    return false;
  }

  const int index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (index < 0) {
    avformat_close_input(&format);
    if (error != nullptr) *error = "sem stream de áudio em " + path;
    return false;
  }

  if (info != nullptr) {
    detail::fillTrackInfo(format, format->streams[index], nullptr, info);
    info->path = path;
    info->modifiedMs = fileModifiedMs(path);
  }

  avformat_close_input(&format);
  return true;
}

bool probeAudioOnly(const std::string& path) {
  AVFormatContext* format = nullptr;
  if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) return false;
  // Sem `avformat_find_stream_info`: aqui só interessa o tipo de cada stream, e ler os
  // primeiros pacotes de um arquivo grande custaria caro para uma pergunta binária.
  bool hasAudio = false;
  bool hasVideo = false;
  for (unsigned i = 0; i < format->nb_streams; ++i) {
    const AVStream* stream = format->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      hasAudio = true;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
               (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
      // Capa embutida chega como vídeo: não é motivo para recusar a faixa.
      hasVideo = true;
    }
  }
  avformat_close_input(&format);
  return hasAudio && !hasVideo;
}

}  // namespace wamp
