#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wamp {

/// Campos de texto que o File Info edita. Os que o formato não conhece ficam vazios.
struct TagFields {
  std::string title;
  std::string artist;
  std::string album;
  std::string albumArtist;
  std::string genre;
  std::string comment;
  std::string composer;
  std::string publisher;
  int track = 0;
  int disc = 0;
  int year = 0;
  int bpm = 0;
};

/// Informação que não vem das tags, e sim do áudio.
struct TagFormat {
  std::string codec;
  int bitrate = 0;      ///< kbps
  int sampleRate = 0;   ///< Hz
  int channels = 0;
  std::int64_t durationMs = 0;
  std::string replayGainTrack;
  std::string replayGainAlbum;
  std::string replayGainTrackPeak;
  std::string replayGainAlbumPeak;
};

/// Uma imagem embutida no arquivo (capa).
struct TagPicture {
  std::string data;         ///< bytes da imagem, como estão no arquivo
  std::string mime;         ///< image/jpeg, image/png…
  std::string description;  ///< rótulo guardado junto (ex.: "cover")
  std::string type;         ///< cover (front), back, artist…
};

/// Leitura e escrita de tags via TagLib, com a mesma cara para os formatos que o app
/// aceita. Gravação segura = escreve numa cópia temporária, confere relendo e só então
/// troca o arquivo original.
class TagEditor final {
 public:
  static bool read(const std::string& path, TagFields* fields, TagFormat* format,
                   std::vector<TagPicture>* pictures, std::string* error);

  static bool canSave(const std::string& path, bool safeSave, std::string* reason);

  /// Capa na pasta da faixa (`cover.jpg`, `folder.jpg`, `front.png`…, sem caixa).
  /// Devolve o caminho ou "" — é o que salva discografia que veio do Windows.
  static std::string folderCover(const std::string& trackPath);

  static bool write(const std::string& path, const TagFields& fields, bool safeSave,
                    std::string* error);

  /// Troca a imagem de índice `index` (ou acrescenta, se `index` < 0).
  static bool replacePicture(const std::string& path, std::size_t index,
                             const TagPicture& picture, bool safeSave, std::string* error);

  static bool removePicture(const std::string& path, std::size_t index, bool safeSave,
                            std::string* error);
};

}  // namespace wamp
