#include "wamp/tag_editor.h"

#include <algorithm>
#include <cctype>

#include <tfile.h>
#include <tstring.h>
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <audioproperties.h>
#include <mpegfile.h>
#include <id3v2tag.h>
#include <attachedpictureframe.h>
#include <textidentificationframe.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <mp4file.h>
#include <mp4tag.h>
#include <mp4coverart.h>
#include <vorbisfile.h>
#include <xiphcomment.h>

#include <system_error>
#include <filesystem>

#include <sys/statvfs.h>
#include <unistd.h>

namespace wamp {
namespace {

namespace fs = std::filesystem;

std::string firstOf(const TagLib::PropertyMap& map, const TagLib::String& key) {
  const auto it = map.find(key);
  if (it == map.end() || it->second.isEmpty()) return {};
  return it->second.front().to8Bit(true);
}

void setKey(TagLib::PropertyMap& map, const TagLib::String& key, const std::string& value) {
  if (value.empty()) {
    map.erase(key);
    return;
  }
  map.replace(key, TagLib::StringList(TagLib::String(value, TagLib::String::UTF8)));
}

int toInt(const std::string& text) {
  if (text.empty()) return 0;
  try {
    return std::stoi(text);
  } catch (const std::exception&) {
    return 0;
  }
}

std::string toString(int value) { return value > 0 ? std::to_string(value) : std::string(); }

/// Rótulo de tipo da imagem (TagLib usa o mesmo vocabulário do ID3v2 APIC).
std::string pictureTypeToText(TagLib::ID3v2::AttachedPictureFrame::Type type) {
  switch (type) {
    case TagLib::ID3v2::AttachedPictureFrame::FrontCover: return "cover";
    case TagLib::ID3v2::AttachedPictureFrame::BackCover: return "back";
    case TagLib::ID3v2::AttachedPictureFrame::Media: return "media";
    case TagLib::ID3v2::AttachedPictureFrame::Artist: return "artist";
    default: return "other";
  }
}

TagLib::ID3v2::AttachedPictureFrame::Type textToPictureType(const std::string& text) {
  if (text == "back") return TagLib::ID3v2::AttachedPictureFrame::BackCover;
  if (text == "media") return TagLib::ID3v2::AttachedPictureFrame::Media;
  if (text == "artist") return TagLib::ID3v2::AttachedPictureFrame::Artist;
  if (text == "cover") return TagLib::ID3v2::AttachedPictureFrame::FrontCover;
  return TagLib::ID3v2::AttachedPictureFrame::Other;
}

TagLib::FLAC::Picture::Type textToFlacType(const std::string& text) {
  if (text == "back") return TagLib::FLAC::Picture::BackCover;
  if (text == "media") return TagLib::FLAC::Picture::Media;
  if (text == "artist") return TagLib::FLAC::Picture::Artist;
  if (text == "cover") return TagLib::FLAC::Picture::FrontCover;
  return TagLib::FLAC::Picture::Other;
}

std::string flacTypeToText(TagLib::FLAC::Picture::Type type) {
  switch (type) {
    case TagLib::FLAC::Picture::FrontCover: return "cover";
    case TagLib::FLAC::Picture::BackCover: return "back";
    case TagLib::FLAC::Picture::Media: return "media";
    case TagLib::FLAC::Picture::Artist: return "artist";
    default: return "other";
  }
}

std::string mimeFromBytes(const std::string& data) {
  if (data.size() > 8 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return "image/png";
  return "image/jpeg";
}

/// Substitui o arquivo original pela cópia já gravada: rename no mesmo diretório é atômico.
bool commitCopy(const std::string& temporary, const std::string& original, std::string* error) {
  std::error_code ec;
  fs::rename(temporary, original, ec);
  if (ec) {
    if (error != nullptr) *error = "falha ao substituir o arquivo: " + ec.message();
    fs::remove(temporary, ec);
    return false;
  }
  return true;
}

/// Executa `work` sobre `target`. Com gravação segura, `target` é uma cópia temporária e
/// só é promovida depois de relida com sucesso.
template <typename Work>
bool guardedSave(const std::string& path, bool safeSave, std::string* error, Work work) {
  if (!safeSave) return work(path, error);

  const std::string temporary = path + ".wamp-tmp";
  std::error_code ec;
  fs::copy_file(path, temporary, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error != nullptr) *error = "não foi possível copiar para gravação segura: " + ec.message();
    return false;
  }
  if (!work(temporary, error)) {
    fs::remove(temporary, ec);
    return false;
  }
  return commitCopy(temporary, path, error);
}

}  // namespace

bool TagEditor::read(const std::string& path, TagFields* fields, TagFormat* format,
                     std::vector<TagPicture>* pictures, std::string* error) {
  TagLib::FileRef ref(path.c_str());
  if (ref.isNull() || ref.file() == nullptr || !ref.file()->isValid()) {
    if (error != nullptr) *error = "formato não reconhecido pelo TagLib";
    return false;
  }

  TagFields parsed;
  if (const TagLib::Tag* tag = ref.tag(); tag != nullptr) {
    parsed.title = tag->title().to8Bit(true);
    parsed.artist = tag->artist().to8Bit(true);
    parsed.album = tag->album().to8Bit(true);
    parsed.genre = tag->genre().to8Bit(true);
    parsed.comment = tag->comment().to8Bit(true);
    parsed.year = static_cast<int>(tag->year());
    parsed.track = static_cast<int>(tag->track());
  }

  const TagLib::PropertyMap map = ref.file()->properties();
  parsed.albumArtist = firstOf(map, "ALBUMARTIST");
  if (parsed.albumArtist.empty()) parsed.albumArtist = firstOf(map, "ALBUM ARTIST");
  parsed.composer = firstOf(map, "COMPOSER");
  parsed.publisher = firstOf(map, "PUBLISHER");
  if (parsed.publisher.empty()) parsed.publisher = firstOf(map, "LABEL");
  parsed.bpm = toInt(firstOf(map, "BPM"));
  const std::string disc = firstOf(map, "DISCNUMBER");
  parsed.disc = toInt(disc.substr(0, disc.find('/')));

  TagFormat info;
  info.replayGainTrack = firstOf(map, "REPLAYGAIN_TRACK_GAIN");
  info.replayGainAlbum = firstOf(map, "REPLAYGAIN_ALBUM_GAIN");
  info.replayGainTrackPeak = firstOf(map, "REPLAYGAIN_TRACK_PEAK");
  info.replayGainAlbumPeak = firstOf(map, "REPLAYGAIN_ALBUM_PEAK");
  // O TagLib mapeia GAIN, mas não os PEAK, no PropertyMap do ID3v2: para MP3 os
  // picos precisam sair dos quadros TXXX na mão (sem eles, o teto anticlipping
  // não existe). Em Vorbis/FLAC o PropertyMap já traz tudo.
  if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(ref.file()); mpeg != nullptr) {
    if (TagLib::ID3v2::Tag* id3 = mpeg->ID3v2Tag(false); id3 != nullptr) {
      for (TagLib::ID3v2::Frame* frame : id3->frameList("TXXX")) {
        auto* text = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(frame);
        if (text == nullptr) continue;
        const TagLib::StringList fields = text->fieldList();
        if (fields.size() < 2) continue;
        std::string description = fields[0].to8Bit(true);
        std::transform(description.begin(), description.end(), description.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string value = fields[1].to8Bit(true);
        if (description == "REPLAYGAIN_TRACK_PEAK" && info.replayGainTrackPeak.empty()) {
          info.replayGainTrackPeak = value;
        } else if (description == "REPLAYGAIN_ALBUM_PEAK" && info.replayGainAlbumPeak.empty()) {
          info.replayGainAlbumPeak = value;
        }
      }
    }
  }
  if (const TagLib::AudioProperties* audio = ref.file()->audioProperties(); audio != nullptr) {
    info.bitrate = audio->bitrate();
    info.sampleRate = audio->sampleRate();
    info.channels = audio->channels();
    info.durationMs = static_cast<std::int64_t>(audio->lengthInMilliseconds());
  }

  // Nome do codec: o TagLib não expõe um rótulo pronto, então deduzimos pelo tipo do arquivo.
  if (dynamic_cast<TagLib::MPEG::File*>(ref.file()) != nullptr) {
    info.codec = "MPEG audio";
  } else if (dynamic_cast<TagLib::FLAC::File*>(ref.file()) != nullptr) {
    info.codec = "FLAC";
  } else if (dynamic_cast<TagLib::MP4::File*>(ref.file()) != nullptr) {
    info.codec = firstOf(map, "CODEC").empty() ? "MPEG-4 audio" : firstOf(map, "CODEC");
  } else if (dynamic_cast<TagLib::Vorbis::File*>(ref.file()) != nullptr) {
    info.codec = "Vorbis";
  } else {
    info.codec = "áudio";
  }

  std::vector<TagPicture> caps;
  if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(ref.file()); mpeg != nullptr) {
    TagLib::ID3v2::Tag* id3 = mpeg->ID3v2Tag();
    if (id3 != nullptr) {
      for (auto* frame : id3->frameList("APIC")) {
        auto* apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (apic == nullptr) continue;
        TagPicture picture;
        picture.mime = apic->mimeType().to8Bit(true);
        picture.description = apic->description().to8Bit(true);
        picture.type = pictureTypeToText(apic->type());
        picture.data = std::string(apic->picture().data(), apic->picture().size());
        caps.push_back(std::move(picture));
      }
    }
  } else if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(ref.file()); flac != nullptr) {
    for (auto* picture : flac->pictureList()) {
      TagPicture cap;
      cap.mime = picture->mimeType().to8Bit(true);
      cap.description = picture->description().to8Bit(true);
      cap.type = flacTypeToText(picture->type());
      cap.data = std::string(picture->data().data(), picture->data().size());
      caps.push_back(std::move(cap));
    }
  } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(ref.file()); mp4 != nullptr) {
    if (mp4->tag() != nullptr) {
      const TagLib::MP4::CoverArtList cover = mp4->tag()->item("covr").toCoverArtList();
      for (const auto& art : cover) {
        TagPicture cap;
        cap.mime = art.format() == TagLib::MP4::CoverArt::PNG ? "image/png" : "image/jpeg";
        cap.type = "cover";
        cap.data = std::string(art.data().data(), art.data().size());
        caps.push_back(std::move(cap));
      }
    }
  }

  if (fields != nullptr) *fields = std::move(parsed);
  if (format != nullptr) *format = std::move(info);
  if (pictures != nullptr) *pictures = std::move(caps);
  return true;
}

std::string TagEditor::folderCover(const std::string& trackPath) {
  static const char* kStems[] = {"cover", "folder", "front", "album", "albumart", "artwork"};
  static const char* kExtensions[] = {".jpg", ".jpeg", ".png", ".webp"};
  std::error_code ec;
  const fs::path folder = fs::path(trackPath).parent_path();
  if (folder.empty() || !fs::is_directory(folder, ec)) return {};

  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (!fs::is_regular_file(entry.path(), ec)) continue;
    names.push_back(entry.path().filename().string());
  }
  if (names.empty() || ec) return {};

  const auto equalIgnoringCase = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  };

  for (const char* stem : kStems) {
    for (const char* extension : kExtensions) {
      const std::string wanted = std::string(stem) + extension;
      for (const std::string& name : names) {
        if (equalIgnoringCase(name, wanted)) {
          return (folder / name).string();
        }
      }
    }
  }
  return {};
}

bool TagEditor::canSave(const std::string& path, bool safeSave, std::string* reason) {
  const auto fail = [reason](const char* text) {
    if (reason != nullptr) *reason = text;
    return false;
  };

  std::error_code ec;
  const fs::path file(path);
  if (!fs::exists(file, ec)) return fail("o arquivo não está mais no disco");

  const fs::path folder = file.parent_path().empty() ? fs::path(".") : file.parent_path();

  // Mesmo com modo de permissão permissivo, um ponto de montagem somente leitura
  // (CIFS/NFS ro, por exemplo) recusa a gravação. O statvfs responde isso sem tentar
  // escrever nada no disco do usuário.
  struct statvfs info {};
  if (::statvfs(folder.c_str(), &info) == 0 && (info.f_flag & ST_RDONLY) != 0) {
    return fail("a pasta está em um sistema de arquivos somente leitura");
  }

  if (safeSave) {
    if (::access(folder.c_str(), W_OK) != 0) return fail("a pasta do arquivo não permite escrita");
  } else if (::access(file.c_str(), W_OK) != 0) {
    return fail("o arquivo não tem permissão de escrita");
  }
  return true;
}

bool TagEditor::write(const std::string& path, const TagFields& fields, bool safeSave,
                      std::string* error) {
  auto work = [&fields](const std::string& target, std::string* err) {
    TagLib::FileRef ref(target.c_str());
    if (ref.isNull() || ref.file() == nullptr || !ref.file()->isValid()) {
      if (err != nullptr) *err = "formato não reconhecido pelo TagLib";
      return false;
    }
    TagLib::Tag* tag = ref.tag();
    if (tag == nullptr) {
      if (err != nullptr) *err = "o arquivo não aceita tags";
      return false;
    }
    // Tudo vai pelo PropertyMap: no MP4 os setters de `Tag` não gravam (album e faixa
    // ficam no átomo `©alb`/`trkn`, e o TagLib não os reescreve por essa via). As chaves
    // canônicas valem para todos os formatos e são as mesmas que o resto do arquivo usa.
    TagLib::PropertyMap map = ref.file()->properties();
    setKey(map, "TITLE", fields.title);
    setKey(map, "ARTIST", fields.artist);
    setKey(map, "ALBUM", fields.album);
    setKey(map, "GENRE", fields.genre);
    setKey(map, "COMMENT", fields.comment);
    setKey(map, "DATE", fields.year > 0 ? std::to_string(fields.year) : std::string());
    setKey(map, "TRACKNUMBER", fields.track > 0 ? std::to_string(fields.track) : std::string());
    setKey(map, "ALBUMARTIST", fields.albumArtist);
    setKey(map, "COMPOSER", fields.composer);
    setKey(map, "PUBLISHER", fields.publisher);
    setKey(map, "BPM", toString(fields.bpm));
    setKey(map, "DISCNUMBER", toString(fields.disc));
    ref.file()->setProperties(map);
    // Os setters continuam: em ID3 quem apaga campo é o `Tag` (o mapa remove a chave, mas
    // alguns formatos só reescrevem pelo objeto de tag). No MP4 vale o caminho do mapa.
    tag->setTitle(TagLib::String(fields.title, TagLib::String::UTF8));
    tag->setArtist(TagLib::String(fields.artist, TagLib::String::UTF8));
    tag->setAlbum(TagLib::String(fields.album, TagLib::String::UTF8));
    tag->setGenre(TagLib::String(fields.genre, TagLib::String::UTF8));
    tag->setComment(TagLib::String(fields.comment, TagLib::String::UTF8));
    tag->setYear(static_cast<unsigned int>(fields.year > 0 ? fields.year : 0));
    tag->setTrack(static_cast<unsigned int>(fields.track > 0 ? fields.track : 0));

    if (!ref.file()->save()) {
      if (err != nullptr) *err = "o TagLib não conseguiu gravar as tags";
      return false;
    }

    // Confere relendo: gravar sem erro não garante que os campos voltaram.
    TagFields check;
    if (!TagEditor::read(target, &check, nullptr, nullptr, err)) return false;
    // A conferência cobre os campos que o autotag preenche: se um deles não voltou, a
    // gravação não vale e o motivo diz qual.
    std::string failed;
    if (check.title != fields.title) failed = "título";
    else if (check.artist != fields.artist) failed = "artista";
    else if (check.album != fields.album) failed = "álbum";
    else if (check.track != fields.track) failed = "faixa";
    else if ((check.year > 0) != (fields.year > 0)) failed = "ano";
    if (!failed.empty()) {
      if (err != nullptr) *err = "a conferência depois da gravação não bateu (" + failed + ")";
      return false;
    }
    return true;
  };
  return guardedSave(path, safeSave, error, work);
}

// A troca é por *tipo* (cover, back, artist…), não por posição: assim a imagem nova
// assume o lugar da antiga na lista, que é o que o diálogo mostra como "Trocar".
bool TagEditor::replacePicture(const std::string& path, std::size_t /*index*/,
                               const TagPicture& picture, bool safeSave, std::string* error) {
  if (picture.data.empty()) {
    if (error != nullptr) *error = "imagem vazia";
    return false;
  }
  const std::string mime = picture.mime.empty() ? mimeFromBytes(picture.data) : picture.mime;
  auto work = [&, mime](const std::string& target, std::string* err) {
    TagLib::FileRef ref(target.c_str());
    if (ref.isNull() || ref.file() == nullptr || !ref.file()->isValid()) {
      if (err != nullptr) *err = "formato não reconhecido para imagem";
      return false;
    }
    const TagLib::ByteVector bytes(picture.data.data(),
                                   static_cast<unsigned int>(picture.data.size()));
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(ref.file()); mpeg != nullptr) {
      TagLib::ID3v2::Tag* id3 = mpeg->ID3v2Tag(true);
      auto* apic = new TagLib::ID3v2::AttachedPictureFrame();
      apic->setMimeType(TagLib::String(mime, TagLib::String::Latin1));
      apic->setType(textToPictureType(picture.type));
      apic->setDescription(TagLib::String(picture.description, TagLib::String::UTF8));
      apic->setPicture(bytes);
      // Retira a imagem antiga do mesmo tipo antes de anexar a nova.
      TagLib::ID3v2::FrameList existing = id3->frameList("APIC");
      for (auto* frame : existing) {
        auto* old = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (old != nullptr && old->type() == apic->type()) id3->removeFrame(frame, false);
      }
      id3->addFrame(apic);
    } else if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(ref.file()); flac != nullptr) {
      TagLib::List<TagLib::FLAC::Picture*> pictures = flac->pictureList();
      const TagLib::FLAC::Picture::Type type = textToFlacType(picture.type);
      for (auto* old : pictures) {
        if (old->type() == type) flac->removePicture(old);
      }
      auto* art = new TagLib::FLAC::Picture();
      art->setMimeType(TagLib::String(mime, TagLib::String::Latin1));
      art->setType(type);
      art->setDescription(TagLib::String(picture.description, TagLib::String::UTF8));
      art->setData(bytes);
      flac->addPicture(art);
    } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(ref.file()); mp4 != nullptr) {
      TagLib::MP4::Tag* tag = mp4->tag();
      if (tag == nullptr) {
        if (err != nullptr) *err = "MP4 sem área de tags";
        return false;
      }
      TagLib::MP4::CoverArtList list;
      list.append(TagLib::MP4::CoverArt(
          mime == "image/png" ? TagLib::MP4::CoverArt::PNG : TagLib::MP4::CoverArt::JPEG, bytes));
      tag->setItem("covr", TagLib::MP4::Item(list));
    } else {
      if (err != nullptr) *err = "este formato não guarda imagem embutida aqui";
      return false;
    }
    if (!ref.file()->save()) {
      if (err != nullptr) *err = "o TagLib não conseguiu gravar a imagem";
      return false;
    }
    return true;
  };
  return guardedSave(path, safeSave, error, work);
}

bool TagEditor::removePicture(const std::string& path, std::size_t index, bool safeSave,
                              std::string* error) {
  std::vector<TagPicture> pictures;
  if (!TagEditor::read(path, nullptr, nullptr, &pictures, error)) return false;
  if (index >= pictures.size()) {
    if (error != nullptr) *error = "imagem não encontrada";
    return false;
  }
  const std::string type = pictures[index].type;

  auto work = [&type](const std::string& target, std::string* err) {
    TagLib::FileRef ref(target.c_str());
    if (ref.isNull() || ref.file() == nullptr) {
      if (err != nullptr) *err = "arquivo inválido";
      return false;
    }
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(ref.file()); mpeg != nullptr) {
      TagLib::ID3v2::Tag* id3 = mpeg->ID3v2Tag();
      if (id3 == nullptr) return true;
      const TagLib::ID3v2::FrameList frames = id3->frameList("APIC");
      for (auto* frame : frames) {
        auto* apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (apic != nullptr && pictureTypeToText(apic->type()) == type) {
          id3->removeFrame(frame, true);
        }
      }
    } else if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(ref.file()); flac != nullptr) {
      const TagLib::List<TagLib::FLAC::Picture*> pictures = flac->pictureList();
      for (auto* old : pictures) {
        if (flacTypeToText(old->type()) == type) flac->removePicture(old);
      }
    } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(ref.file()); mp4 != nullptr) {
      if (mp4->tag() != nullptr) mp4->tag()->removeItem("covr");
    } else {
      if (err != nullptr) *err = "este formato não tem imagem embutida aqui";
      return false;
    }
    if (!ref.file()->save()) {
      if (err != nullptr) *err = "o TagLib não conseguiu gravar";
      return false;
    }
    return true;
  };
  return guardedSave(path, safeSave, error, work);
}

}  // namespace wamp
