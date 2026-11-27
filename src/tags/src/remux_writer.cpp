// Escrita de tags por remuxagem com libavformat.
//
// O TagLib do runtime (2.1.1) não grava em MP4 (o `save()` volta verdadeiro sem escrever) e
// não limpa campo no ID3 quando o arquivo também tem ID3v1. Nos dois casos a saída é a mesma:
// copiar os fluxos para um arquivo novo com o conjunto de metadados que queremos, conferir
// relendo e só então trocar o original. Sem recodificar áudio: os pacotes são copiados.
#include "wamp/remux_writer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

#include <cctype>
#include <cstdio>
#include <map>
#include <string>

namespace wamp {
namespace {

const char* muxerFor(const std::string& path) {
  const std::string lower = [&path] {
    std::string out;
    for (char c : path) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
  }();
  auto ends = [&lower](const char* suffix) {
    const std::string s(suffix);
    return lower.size() >= s.size() && lower.compare(lower.size() - s.size(), s.size(), s) == 0;
  };
  if (ends(".m4a") || ends(".mp4") || ends(".m4b")) return "mp4";
  if (ends(".mp3")) return "mp3";
  if (ends(".flac")) return "flac";
  if (ends(".ogg") || ends(".oga")) return "ogg";
  if (ends(".opus")) return "opus";
  return nullptr;
}

void setMeta(AVDictionary** dict, const char* key, const std::string& value) {
  if (!value.empty()) av_dict_set(dict, key, value.c_str(), 0);
}

/// Número e disco viram "3" e "1/2" no mesmo campo que o resto do mundo lê.
void setNumber(AVDictionary** dict, const char* key, int value) {
  if (value > 0) av_dict_set(dict, key, std::to_string(value).c_str(), 0);
}

}  // namespace

bool remuxCanWrite(const std::string& path) { return muxerFor(path) != nullptr; }

bool remuxWrite(const std::string& path, const TagFields& fields, std::string* error) {
  auto fail = [error](const std::string& text) {
    if (error != nullptr) *error = text;
    return false;
  };

  const char* muxer = muxerFor(path);
  if (muxer == nullptr) return fail("formato sem muxer conhecido para reescrever as tags");

  AVFormatContext* input = nullptr;
  if (avformat_open_input(&input, path.c_str(), nullptr, nullptr) < 0) {
    return fail("não foi possível abrir o arquivo para reler as tags");
  }
  if (avformat_find_stream_info(input, nullptr) < 0) {
    avformat_close_input(&input);
    return fail("não foi possível ler os fluxos do arquivo");
  }

  const std::string temporary = path + ".wamp-remux";
  AVFormatContext* output = nullptr;
  if (avformat_alloc_output_context2(&output, nullptr, muxer, temporary.c_str()) < 0 ||
      output == nullptr) {
    avformat_close_input(&input);
    return fail("não foi possível preparar o arquivo novo");
  }

  std::map<int, int> streamMap;
  bool headerOk = true;
  for (unsigned i = 0; i < input->nb_streams; ++i) {
    AVStream* in = input->streams[i];
    if (in->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        in->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
      continue;
    }
    AVStream* out = avformat_new_stream(output, nullptr);
    if (out == nullptr || avcodec_parameters_copy(out->codecpar, in->codecpar) < 0) {
      headerOk = false;
      break;
    }
    out->codecpar->codec_tag = 0;
    out->time_base = in->time_base;
    if (in->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      // Capa embutida continua capa embutida.
      out->disposition = in->disposition;
    }
    streamMap[static_cast<int>(i)] = out->index;
  }
  if (!headerOk) {
    avformat_free_context(output);
    avformat_close_input(&input);
    return fail("não foi possível copiar os fluxos");
  }

  setMeta(&output->metadata, "title", fields.title);
  setMeta(&output->metadata, "artist", fields.artist);
  setMeta(&output->metadata, "album", fields.album);
  setMeta(&output->metadata, "album_artist", fields.albumArtist);
  setMeta(&output->metadata, "genre", fields.genre);
  setMeta(&output->metadata, "comment", fields.comment);
  setMeta(&output->metadata, "composer", fields.composer);
  setMeta(&output->metadata, "publisher", fields.publisher);
  if (fields.year > 0) av_dict_set(&output->metadata, "date", std::to_string(fields.year).c_str(), 0);
  setNumber(&output->metadata, "track", fields.track);
  setNumber(&output->metadata, "disc", fields.disc);
  if (fields.bpm > 0.0f) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", static_cast<double>(fields.bpm));
    av_dict_set(&output->metadata, "BPM", text, 0);
  }

  if (avio_open(&output->pb, temporary.c_str(), AVIO_FLAG_WRITE) < 0) {
    avformat_free_context(output);
    avformat_close_input(&input);
    return fail("não foi possível criar o arquivo temporário");
  }
  if (avformat_write_header(output, nullptr) < 0) {
    avio_closep(&output->pb);
    avformat_free_context(output);
    avformat_close_input(&input);
    std::remove(temporary.c_str());
    return fail("não foi possível escrever o cabeçalho do arquivo novo");
  }

  AVPacket* packet = av_packet_alloc();
  bool copied = true;
  while (packet != nullptr && av_read_frame(input, packet) >= 0) {
    const auto found = streamMap.find(packet->stream_index);
    if (found == streamMap.end()) {
      av_packet_unref(packet);
      continue;
    }
    AVStream* in = input->streams[packet->stream_index];
    AVStream* out = output->streams[found->second];
    av_packet_rescale_ts(packet, in->time_base, out->time_base);
    packet->stream_index = found->second;
    if (av_interleaved_write_frame(output, packet) < 0) {
      copied = false;
      av_packet_unref(packet);
      break;
    }
  }
  if (packet != nullptr) av_packet_free(&packet);

  const bool trailer = copied && av_write_trailer(output) >= 0;
  avio_closep(&output->pb);
  avformat_free_context(output);
  avformat_close_input(&input);

  if (!trailer) {
    std::remove(temporary.c_str());
    return fail("a cópia dos fluxos não terminou");
  }

  // Conferir relendo: se o arquivo novo não diz o que devia, o original fica como está.
  TagFields check;
  std::string readError;
  if (!TagEditor::read(temporary, &check, nullptr, nullptr, &readError)) {
    std::remove(temporary.c_str());
    return fail("o arquivo novo não abriu para conferência: " + readError);
  }
  const bool same = check.title == fields.title && check.artist == fields.artist &&
                    check.album == fields.album && check.track == fields.track &&
                    (check.year > 0) == (fields.year > 0);
  if (!same) {
    std::remove(temporary.c_str());
    return fail("a conferência depois de reescrever as tags não bateu");
  }

  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return fail("não foi possível trocar o arquivo original pelo novo");
  }
  return true;
}

}  // namespace wamp
