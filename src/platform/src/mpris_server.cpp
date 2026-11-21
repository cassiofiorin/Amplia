// Controle de mídia do sistema (MPRIS2) sobre sd-bus — ver mpris_server.h.
#include "wamp/mpris_server.h"

#include <QSocketNotifier>
#include <QString>

#include <systemd/sd-bus.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace wamp {
namespace {

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootIface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";

const char* statusString(PlaybackState state) {
  switch (state) {
    case PlaybackState::Playing:
      return "Playing";
    case PlaybackState::Paused:
    case PlaybackState::Loading:
      return "Paused";
    case PlaybackState::Idle:
    case PlaybackState::Error:
      break;
  }
  return "Stopped";
}

// Uma chamada D-Bus espera resposta: devolver sem responder derruba a conexão.
std::chrono::steady_clock::time_point lastPlayPause{};

namespace {
// Diagnóstico do caminho AVRCP → MPRIS: com AMPLIA_MPRIS_LOG definido, cada método
// chamado deixa uma linha em stderr. Serve para saber o que o mpris-proxy pediu.
}  // namespace

int replyOk(sd_bus_message* message) {
  const int r = sd_bus_reply_method_return(message, nullptr);
  return r < 0 ? r : 1;
}

std::string fileUrl(const std::string& path) {
  if (path.rfind("file://", 0) == 0) return path;
  return "file://" + path;
}

}  // namespace

struct MprisServer::Impl {
  MprisHooks hooks;
  sd_bus* bus = nullptr;
  QSocketNotifier* readNotifier = nullptr;
  QSocketNotifier* writeNotifier = nullptr;
  sd_bus_slot* rootSlot = nullptr;
  sd_bus_slot* playerSlot = nullptr;
  QString serviceName;
  std::uint64_t trackSerial = 1;
  QString lastStatus;
  QString lastMetadataKey;
  bool hasLastCanNext = false;
  bool lastCanNext = false;
  bool hasLastCanPrevious = false;
  bool lastCanPrevious = false;
  bool hasLastShuffle = false;
  bool lastShuffle = false;
  bool hasLastRepeat = false;
  bool lastRepeat = false;
  bool hasLastVolume = false;
  double lastVolume = 0.0;

  void updateNotifiers();
  void process();
  void emitPropertiesChanged(const char* interface, const char* property);
  int appendMetadata(sd_bus_message* message);
};

// --- Propriedades e métodos de org.mpris.MediaPlayer2 ----------------------

int rootGetCanQuit(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 1);
}
int rootGetCanRaise(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 1);
}
int rootGetHasTrackList(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 0);
}
int rootGetIdentity(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", "Amplia");
}
int rootGetDesktopEntry(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", "io.github.cassiofiorin.Amplia");
}
int rootGetSupportedUriSchemes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  int r = sd_bus_message_open_container(reply, 'a', "s");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "s", "file");
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}
int rootGetSupportedMimeTypes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  static const char* const kTypes[] = {"audio/mpeg", "audio/flac", "audio/ogg",
                                       "audio/x-wav", "audio/mp4", "audio/x-m4a"};
  int r = sd_bus_message_open_container(reply, 'a', "s");
  if (r < 0) return r;
  for (const char* type : kTypes) {
    r = sd_bus_message_append(reply, "s", type);
    if (r < 0) return r;
  }
  return sd_bus_message_close_container(reply);
}
int rootRaise(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.raise) impl->hooks.raise();
  return replyOk(message);
}
int rootQuit(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.quit) impl->hooks.quit();
  return replyOk(message);
}

// --- Propriedades e métodos de org.mpris.MediaPlayer2.Player ---------------

int playerGetPlaybackStatus(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const PlaybackState state = impl->hooks.state ? impl->hooks.state() : PlaybackState::Idle;
  return sd_bus_message_append(reply, "s", statusString(state));
}
int playerGetLoopStatus(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  int mode = 0;
  if (impl->hooks.repeatMode) {
    mode = impl->hooks.repeatMode();
  } else if (impl->hooks.repeat && impl->hooks.repeat()) {
    mode = 1;
  }
  const char* value = mode == 2 ? "Track" : mode == 1 ? "Playlist" : "None";
  return sd_bus_message_append(reply, "s", value);
}
int playerSetLoopStatus(sd_bus*, const char*, const char*, const char*, sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const char* value = nullptr;
  if (sd_bus_message_read(message, "s", &value) < 0 || value == nullptr) return replyOk(message);
  const int mode = std::strcmp(value, "Track") == 0 ? 2
                  : std::strcmp(value, "Playlist") == 0 ? 1
                                                        : 0;
  if (impl->hooks.setRepeatMode != nullptr) {
    impl->hooks.setRepeatMode(mode);
  } else if (impl->hooks.setRepeat != nullptr) {
    if (std::strcmp(value, "Playlist") == 0) impl->hooks.setRepeat(true);
    else if (std::strcmp(value, "None") == 0) impl->hooks.setRepeat(false);
  }
  return replyOk(message);
}
int playerGetRate(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "d", 1.0);
}
int playerSetRate(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*) { return 0; }
int playerGetShuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const bool on = impl->hooks.shuffle ? impl->hooks.shuffle() : false;
  return sd_bus_message_append(reply, "b", static_cast<int>(on));
}
int playerSetShuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  int value = 0;
  int r = sd_bus_message_read(message, "b", &value);
  if (r < 0) return r;
  if (impl->hooks.setShuffle) impl->hooks.setShuffle(value != 0);
  return 0;
}
int playerGetMetadata(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  return impl->appendMetadata(reply);
}
int playerGetPosition(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const std::int64_t ms = impl->hooks.positionMs ? impl->hooks.positionMs() : 0;
  return sd_bus_message_append(reply, "x", ms * 1000);
}
int playerGetVolume(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const double volume = impl->hooks.volume ? impl->hooks.volume() : 1.0;
  return sd_bus_message_append(reply, "d", volume);
}
int playerSetVolume(sd_bus*, const char*, const char*, const char*, sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  double volume = 0.0;
  int r = sd_bus_message_read(message, "d", &volume);
  if (r < 0) return r;
  if (volume < 0.0) volume = 0.0;
  if (volume > 1.0) volume = 1.0;
  if (impl->hooks.setVolume) impl->hooks.setVolume(volume);
  return 0;
}
int playerGetCanNext(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const bool can = impl->hooks.hasNext && impl->hooks.hasNext();
  return sd_bus_message_append(reply, "b", static_cast<int>(can));
}
int playerGetCanPrevious(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const bool can = impl->hooks.hasPrevious && impl->hooks.hasPrevious();
  return sd_bus_message_append(reply, "b", static_cast<int>(can));
}
int playerGetTrue(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 1);
}

int playerPlay(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.play) impl->hooks.play();
  return replyOk(message);
}
int playerPause(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.pause) impl->hooks.pause();
  return replyOk(message);
}
int playerPlayPause(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  // Alguns fones mandam o toque do botão duas vezes (o BlueZ entrega dois PlayPause
  // seguidos); alternar duas vezes devolve o player ao mesmo estado e o botão parece
  // não funcionar. Ignora a repetição dentro da janela de debounce.
  const auto now = std::chrono::steady_clock::now();
  if (now - lastPlayPause < std::chrono::milliseconds(400)) {
    return replyOk(message);
  }
  lastPlayPause = now;
  if (impl->hooks.playPause) impl->hooks.playPause();
  return replyOk(message);
}
int playerStop(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.stop) impl->hooks.stop();
  return replyOk(message);
}
int playerNext(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.next) impl->hooks.next();
  return replyOk(message);
}
int playerPrevious(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  if (impl->hooks.previous) impl->hooks.previous();
  return replyOk(message);
}
int playerSeek(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  std::int64_t offsetUs = 0;
  int r = sd_bus_message_read(message, "x", &offsetUs);
  if (r < 0) return r;
  if (impl->hooks.seekTo && impl->hooks.positionMs) {
    const std::int64_t target = impl->hooks.positionMs() + offsetUs / 1000;
    impl->hooks.seekTo(target < 0 ? 0 : target);
  }
  return replyOk(message);
}
int playerSetPosition(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const char* trackId = nullptr;
  std::int64_t positionUs = 0;
  int r = sd_bus_message_read(message, "ox", &trackId, &positionUs);
  if (r < 0) return r;
  (void)trackId;
  if (impl->hooks.seekTo) impl->hooks.seekTo(positionUs / 1000);
  return replyOk(message);
}
int playerOpenUri(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<MprisServer::Impl*>(userdata);
  const char* uri = nullptr;
  int r = sd_bus_message_read(message, "s", &uri);
  if (r < 0) return r;
  std::string path = uri != nullptr ? uri : "";
  if (path.rfind("file://", 0) == 0) path = path.substr(7);
  if (!path.empty() && impl->hooks.openPath) impl->hooks.openPath(path);
  return replyOk(message);
}

const sd_bus_vtable kRootVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("CanQuit", "b", rootGetCanQuit, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", rootGetCanRaise, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", rootGetHasTrackList, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", rootGetIdentity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("DesktopEntry", "s", rootGetDesktopEntry, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", rootGetSupportedUriSchemes, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", rootGetSupportedMimeTypes, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Raise", "", "", rootRaise, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", rootQuit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

const sd_bus_vtable kPlayerVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", playerGetPlaybackStatus, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("LoopStatus", "s", playerGetLoopStatus, playerSetLoopStatus, 0,
                             SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Rate", "d", playerGetRate, playerSetRate, 0,
                             SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Shuffle", "b", playerGetShuffle, playerSetShuffle, 0,
                             SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", playerGetMetadata, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", playerGetPosition, 0, 0),
    SD_BUS_WRITABLE_PROPERTY("Volume", "d", playerGetVolume, playerSetVolume, 0,
                             SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("MinimumRate", "d", playerGetRate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", playerGetRate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", playerGetCanNext, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanGoPrevious", "b", playerGetCanPrevious, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPlay", "b", playerGetTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPause", "b", playerGetTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSeek", "b", playerGetTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanControl", "b", playerGetTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Next", "", "", playerNext, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", playerPrevious, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", playerPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", "", "", playerPlay, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", "", "", playerPlayPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", playerStop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", playerSeek, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", playerSetPosition, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", "", playerOpenUri, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Seeked", "x", 0),
    SD_BUS_VTABLE_END};

int MprisServer::Impl::appendMetadata(sd_bus_message* message) {
  const TrackInfo info = hooks.track ? hooks.track() : TrackInfo{};
  const std::string trackPath = "/org/mpris/MediaPlayer2/Track/" + std::to_string(trackSerial);

  // Cada campo é uma entrada "{sv}"; os fechamentos ficam junto de quem abre,
  // porque esquecer um deles corrompe a mensagem.
  const auto addString = [message](const char* key, const std::string& value) -> int {
    int r = sd_bus_message_open_container(message, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "s", key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(message, 'v', "s");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "s", value.c_str());
    if (r < 0) return r;
    r = sd_bus_message_close_container(message);
    if (r < 0) return r;
    return sd_bus_message_close_container(message);
  };
  const auto addObjectPath = [message](const char* key, const std::string& value) -> int {
    int r = sd_bus_message_open_container(message, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "s", key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(message, 'v', "o");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "o", value.c_str());
    if (r < 0) return r;
    r = sd_bus_message_close_container(message);
    if (r < 0) return r;
    return sd_bus_message_close_container(message);
  };
  const auto addInt64 = [message](const char* key, std::int64_t value) -> int {
    int r = sd_bus_message_open_container(message, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "s", key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(message, 'v', "x");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "x", value);
    if (r < 0) return r;
    r = sd_bus_message_close_container(message);
    if (r < 0) return r;
    return sd_bus_message_close_container(message);
  };
  const auto addArtists = [message](const char* key, const std::string& artist) -> int {
    int r = sd_bus_message_open_container(message, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append(message, "s", key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(message, 'v', "as");
    if (r < 0) return r;
    r = sd_bus_message_open_container(message, 'a', "s");
    if (r < 0) return r;
    if (!artist.empty()) {
      r = sd_bus_message_append(message, "s", artist.c_str());
      if (r < 0) return r;
    }
    r = sd_bus_message_close_container(message);
    if (r < 0) return r;
    r = sd_bus_message_close_container(message);
    if (r < 0) return r;
    return sd_bus_message_close_container(message);
  };

  int r = sd_bus_message_open_container(message, 'a', "{sv}");
  if (r < 0) return r;
  if ((r = addObjectPath("mpris:trackid", trackPath)) < 0) return r;
  if (info.durationMs > 0 && (r = addInt64("mpris:length", info.durationMs * 1000)) < 0) return r;
  if (!info.title.empty() && (r = addString("xesam:title", info.title)) < 0) return r;
  if (!info.artist.empty() && (r = addArtists("xesam:artist", info.artist)) < 0) return r;
  if (!info.album.empty() && (r = addString("xesam:album", info.album)) < 0) return r;
  if (!info.path.empty() && (r = addString("xesam:url", fileUrl(info.path))) < 0) return r;
  // Capa: um arquivo local que o applet do KDE carrega por file://.
  const std::string art = hooks.artPath ? hooks.artPath() : std::string();
  if (!art.empty() && (r = addString("mpris:artUrl", fileUrl(art))) < 0) return r;
  return sd_bus_message_close_container(message);
}

void MprisServer::Impl::updateNotifiers() {
  if (bus == nullptr) return;
  // A leitura fica sempre ligada: desligá-la quando o sd-bus não tem evento
  // pendente deixaria o processo surdo para a próxima mensagem.
  if (readNotifier != nullptr) readNotifier->setEnabled(true);
  const int events = sd_bus_get_events(bus);
  if (writeNotifier != nullptr) writeNotifier->setEnabled(events >= 0 && (events & 0x004) != 0);
}

void MprisServer::Impl::process() {
  if (bus == nullptr) return;
  int r = 0;
  while ((r = sd_bus_process(bus, nullptr)) > 0) {
  }
  if (r < 0) std::fprintf(stderr, "amplia: sd-bus: %s\n", std::strerror(-r));
  updateNotifiers();
}

void MprisServer::Impl::emitPropertiesChanged(const char* interface, const char* property) {
  if (bus == nullptr) return;
  const int r = sd_bus_emit_properties_changed(bus, kObjectPath, interface, property, nullptr);
  if (r < 0) std::fprintf(stderr, "amplia: sd-bus emitir %s: %s\n", property, std::strerror(-r));
}

MprisServer::MprisServer(MprisHooks hooks) : impl_(std::make_unique<Impl>()) {
  impl_->hooks = std::move(hooks);
}

MprisServer::~MprisServer() { stop(); }

const MprisHooks& MprisServer::hooks() const { return impl_->hooks; }

QString MprisServer::trackId() const {
  return QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(impl_->trackSerial);
}

void MprisServer::bumpTrackId() { ++impl_->trackSerial; }

bool MprisServer::start(const QString& serviceName) {
  if (impl_->bus != nullptr) return true;
  impl_->serviceName = serviceName;

  int r = sd_bus_open_user(&impl_->bus);
  if (r < 0) {
    std::fprintf(stderr, "amplia: sd-bus abrir sessão: %s\n", std::strerror(-r));
    impl_->bus = nullptr;
    return false;
  }
  sd_bus_set_description(impl_->bus, "Amplia player");

  r = sd_bus_add_object_vtable(impl_->bus, &impl_->rootSlot, kObjectPath, kRootIface, kRootVtable,
                               impl_.get());
  if (r < 0) {
    std::fprintf(stderr, "amplia: sd-bus vtable raiz: %s\n", std::strerror(-r));
    stop();
    return false;
  }
  r = sd_bus_add_object_vtable(impl_->bus, &impl_->playerSlot, kObjectPath, kPlayerIface,
                               kPlayerVtable, impl_.get());
  if (r < 0) {
    std::fprintf(stderr, "amplia: sd-bus vtable player: %s\n", std::strerror(-r));
    stop();
    return false;
  }

  const QByteArray name = serviceName.toUtf8();
  r = sd_bus_request_name(impl_->bus, name.constData(), 0);
  if (r < 0) {
    std::fprintf(stderr, "amplia: sd-bus pedir nome %s: %s\n", name.constData(), std::strerror(-r));
    stop();
    return false;
  }

  const int fd = sd_bus_get_fd(impl_->bus);
  if (fd >= 0) {
    impl_->readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read);
    QObject::connect(impl_->readNotifier, &QSocketNotifier::activated, [this] {
      impl_->process();
    });
    impl_->writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write);
    QObject::connect(impl_->writeNotifier, &QSocketNotifier::activated, [this] {
      impl_->process();
    });
    impl_->updateNotifiers();
  }
  impl_->process();  // escoa a requisição do nome antes de o loop começar
  return true;
}

void MprisServer::stop() {
  if (impl_ == nullptr) return;
  delete impl_->readNotifier;
  impl_->readNotifier = nullptr;
  delete impl_->writeNotifier;
  impl_->writeNotifier = nullptr;
  if (impl_->playerSlot != nullptr) sd_bus_slot_unref(impl_->playerSlot);
  impl_->playerSlot = nullptr;
  if (impl_->rootSlot != nullptr) sd_bus_slot_unref(impl_->rootSlot);
  impl_->rootSlot = nullptr;
  if (impl_->bus != nullptr) {
    sd_bus_flush_close_unref(impl_->bus);
    impl_->bus = nullptr;
  }
  impl_->lastStatus.clear();
  impl_->lastMetadataKey.clear();
}

void MprisServer::refreshStatus() {
  if (impl_->bus == nullptr) return;
  const MprisHooks& hooks = impl_->hooks;

  const PlaybackState state = hooks.state ? hooks.state() : PlaybackState::Idle;
  const QString status = QString::fromLatin1(statusString(state));
  if (status != impl_->lastStatus) {
    impl_->lastStatus = status;
    impl_->emitPropertiesChanged(kPlayerIface, "PlaybackStatus");
  }

  // As capacidades também mudam (fim da playlist, fila, sorteio). Sem avisar,
  // o applet do desktop continua achando que "avançar" está indisponível e
  // nem manda os cliques.
  const bool canNext = hooks.hasNext && hooks.hasNext();
  if (!impl_->hasLastCanNext || canNext != impl_->lastCanNext) {
    impl_->hasLastCanNext = true;
    impl_->lastCanNext = canNext;
    impl_->emitPropertiesChanged(kPlayerIface, "CanGoNext");
  }

  const bool canPrevious = hooks.hasPrevious && hooks.hasPrevious();
  if (!impl_->hasLastCanPrevious || canPrevious != impl_->lastCanPrevious) {
    impl_->hasLastCanPrevious = true;
    impl_->lastCanPrevious = canPrevious;
    impl_->emitPropertiesChanged(kPlayerIface, "CanGoPrevious");
  }

  const bool shuffle = hooks.shuffle && hooks.shuffle();
  if (!impl_->hasLastShuffle || shuffle != impl_->lastShuffle) {
    impl_->hasLastShuffle = true;
    impl_->lastShuffle = shuffle;
    impl_->emitPropertiesChanged(kPlayerIface, "Shuffle");
  }

  const bool repeat = hooks.repeat && hooks.repeat();
  if (!impl_->hasLastRepeat || repeat != impl_->lastRepeat) {
    impl_->hasLastRepeat = true;
    impl_->lastRepeat = repeat;
    impl_->emitPropertiesChanged(kPlayerIface, "LoopStatus");
  }

  const double volume = hooks.volume ? hooks.volume() : 1.0;
  if (!impl_->hasLastVolume || std::abs(volume - impl_->lastVolume) > 0.002) {
    impl_->hasLastVolume = true;
    impl_->lastVolume = volume;
    impl_->emitPropertiesChanged(kPlayerIface, "Volume");
  }
}

void MprisServer::refreshMetadata() {
  if (impl_->bus == nullptr) return;
  const TrackInfo info = impl_->hooks.track ? impl_->hooks.track() : TrackInfo{};
  const QString key = QStringLiteral("%1|%2|%3")
                          .arg(QString::fromStdString(info.path))
                          .arg(QString::fromStdString(info.title))
                          .arg(info.durationMs);
  if (key == impl_->lastMetadataKey) return;
  impl_->lastMetadataKey = key;
  bumpTrackId();
  impl_->emitPropertiesChanged(kPlayerIface, "Metadata");
}

}  // namespace wamp
