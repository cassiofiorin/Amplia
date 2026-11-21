// Controle de mídia do sistema (MPRIS2) sobre sd-bus.
//
// A interface é publicada em /org/mpris/MediaPlayer2 com dois objetos D-Bus
// (org.mpris.MediaPlayer2 e org.mpris.MediaPlayer2.Player), como o protocolo
// exige. Tudo é feito com sd-bus (systemd) e não com QtDBus: o QtDBus não
// exportou as interfaces do jeito esperado neste runtime, e o sd-bus dá
// controle direto das propriedades e do bombeamento de mensagens.
#ifndef WAMP_MPRIS_SERVER_H
#define WAMP_MPRIS_SERVER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <QString>

#include "wamp/types.h"

namespace wamp {

// Tudo o que o servidor MPRIS precisa saber sobre o player. As funções são
// chamadas da thread do Qt, então quem preenche pode usar o estado da janela.
struct MprisHooks {
  std::function<PlaybackState()> state;
  std::function<std::int64_t()> positionMs;
  std::function<TrackInfo()> track;
  /// Caminho local da capa já extraída ("" = sem capa). Vira `mpris:artUrl`.
  std::function<std::string()> artPath;
  std::function<bool()> hasNext;
  std::function<bool()> hasPrevious;
  std::function<void()> play;
  std::function<void()> pause;
  std::function<void()> playPause;
  std::function<void()> stop;
  std::function<void()> next;
  std::function<void()> previous;
  std::function<void(std::int64_t)> seekTo;
  std::function<void(const std::string&)> openPath;
  std::function<bool()> shuffle;
  std::function<void(bool)> setShuffle;
  /// Repetir a lista: o KDE mostra isso como LoopStatus "Playlist".
  std::function<bool()> repeat;
  std::function<void(bool)> setRepeat;
  /// 0 = desligado, 1 = lista, 2 = faixa (LoopStatus None/Playlist/Track). Quando
  /// presente, tem prioridade sobre `repeat`/`setRepeat`.
  std::function<int()> repeatMode;
  std::function<void(int)> setRepeatMode;
  std::function<double()> volume;
  std::function<void(double)> setVolume;
  std::function<void()> raise;
  std::function<void()> quit;
};

class MprisServer final {
 public:
  explicit MprisServer(MprisHooks hooks);
  ~MprisServer();

  MprisServer(const MprisServer&) = delete;
  MprisServer& operator=(const MprisServer&) = delete;

  // Conecta ao barramento de sessão e publica o nome. Devolve false se o nome
  // já estiver em uso ou o barramento não estiver acessível.
  bool start(const QString& serviceName = QStringLiteral("org.mpris.MediaPlayer2.amplia"));
  void stop();

  // Chamado pela janela quando o estado muda; só emite sinal se mudou de fato.
  void refreshStatus();
  void refreshMetadata();

  const MprisHooks& hooks() const;
  QString trackId() const;
  void bumpTrackId();

  // Público porque as funções de vtable do sd-bus (arquivo .cpp) precisam do tipo.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace wamp

#endif  // WAMP_MPRIS_SERVER_H
