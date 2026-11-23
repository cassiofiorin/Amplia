#pragma once

#include <cstdint>
#include <string>

namespace wamp {

/// Estado do transporte. A máquina de estados está em docs/ARCHITECTURE.md §6.
enum class PlaybackState {
  Idle,     ///< nada carregado
  Loading,  ///< abrindo o arquivo
  Playing,
  Paused,
  Error,
};

inline const char* toString(PlaybackState state) {
  switch (state) {
    case PlaybackState::Idle: return "Idle";
    case PlaybackState::Loading: return "Loading";
    case PlaybackState::Playing: return "Playing";
    case PlaybackState::Paused: return "Paused";
    case PlaybackState::Error: return "Error";
  }
  return "?";
}

/// Metadados de uma faixa carregada. Preenchido pelo decoder na abertura.
struct TrackInfo {
  std::string path;
  std::string title;
  std::string artist;
  std::string album;
  std::string albumArtist;
  std::string genre;
  int trackNo = 0;
  int year = 0;
  std::int64_t durationMs = 0;
  std::int64_t fileSize = 0;
  std::int64_t modifiedMs = 0;  ///< data de modificação do arquivo (cache de tags)
  int sampleRate = 0;  ///< taxa original, apenas para exibição
  int channels = 0;    ///< canais originais, apenas para exibição
  int bitrate = 0;     ///< kbps
  float bpmTag = 0.0f;    ///< TBPM/bpm da tag (0 = ausente)
  int keyPcTag = -1;      ///< classe de altura da tag (0–11, -1 = ausente)
  int keyMajorTag = 0;    ///< 1 = maior, 0 = menor (só vale com keyPcTag >= 0)

  std::string displayTitle() const {
    if (title.empty()) return path;
    if (artist.empty()) return title;
    return artist + " - " + title;
  }
};

/// Formato interno do pipeline (D-004): nada abaixo da UI vê outro layout.
inline constexpr int kSampleRate = 48000;
inline constexpr int kChannels = 2;

}  // namespace wamp
