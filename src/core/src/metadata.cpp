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

/// Primeiro valor não vazio entre várias grafias de chave de metadado.
std::string firstOf(AVDictionary* fmt, AVDictionary* st, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const std::string value = firstTag(fmt, st, key);
    if (!value.empty()) return value;
  }
  return std::string();
}

/// "8A"/"8B" (Camelot), "Am"/"F#m"/"G" (nota) → classe de altura + modo. Devolve -1 se não
/// reconhecer; a rádio usa a tag quando existe e a estimativa quando não.
int parseKeyTag(const std::string& text, bool* major) {
  static const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static const char* kFlat[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  std::string value = text;
  for (char& c : value) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  if (value.size() >= 2 && (value.back() == 'A' || value.back() == 'B')) {
    const bool isMajor = value.back() == 'B';
    const int camelot = std::atoi(value.c_str());
    if (camelot >= 1 && camelot <= 12) {
      // Camelot: 8B = Dó maior, 8A = Lá menor; a roda anda de quinta em quinta.
      static const int kCamelotMajor[12] = {7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5, 0};
      static const int kCamelotMinor[12] = {4, 11, 6, 1, 8, 3, 10, 5, 0, 7, 2, 9};
      *major = isMajor;
      return isMajor ? kCamelotMajor[camelot - 1] : kCamelotMinor[camelot - 1];
    }
  }
  const bool minor = value.size() >= 2 && value.back() == 'M' && value[value.size() - 2] != 'A';
  std::string note = value;
  if (minor && note.size() >= 2) note.pop_back();
  if (note == "DO") note = "C";
  if (note.find("MAJ") != std::string::npos) note = note.substr(0, note.find("MAJ"));
  for (int i = 0; i < 12; ++i) {
    if (note == kNotes[i] || note == kFlat[i]) {
      *major = !minor;
      return i;
    }
  }
  return -1;
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

  // BPM e tom das tags: entram como valor do arquivo (a estimativa é outra coluna). As
  // grafias variam por container — ID3v2 usa TBPM/TKEY, MP4 usa tmpo/key, Vorbis usa BPM/KEY.
  const std::string bpmText = firstOf(fmt, st, {"TBPM", "tbpm", "BPM", "bpm", "tmpo", "TEMPO"});
  if (!bpmText.empty()) {
    try {
      info->bpmTag = static_cast<float>(std::stof(bpmText));
    } catch (...) {
      info->bpmTag = 0.0f;
    }
    if (info->bpmTag < 20.0f || info->bpmTag > 400.0f) info->bpmTag = 0.0f;
  }
  const std::string keyText = firstOf(fmt, st, {"TKEY", "tkey", "KEY", "key", "INITIALKEY"});
  if (!keyText.empty()) {
    bool major = true;
    const int pc = parseKeyTag(keyText, &major);
    if (pc >= 0) {
      info->keyPcTag = pc;
      info->keyMajorTag = major ? 1 : 0;
    }
  }

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
