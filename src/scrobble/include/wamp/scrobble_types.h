#pragma once

#include <cstdint>
#include <string>

namespace wamp {

/// Configuração dos dois destinos de scrobble. A session key do Last.fm é obtida uma
/// vez (auth.getSession) e vale indefinidamente; o token do ListenBrainz é fixo.
struct ScrobbleConfig {
  bool lastfmEnabled = false;
  std::string lastfmApiKey;
  std::string lastfmSharedSecret;
  std::string lastfmSessionKey;

  bool listenbrainzEnabled = false;
  std::string listenbrainzToken;

  /// Anunciar "ouvindo agora" no início da faixa. Desligado por padrão: o Last.fm não
  /// tem chamada para encerrar o anúncio, e ele fica no perfil quando o player parou.
  bool announceNowPlaying = false;

  bool anyEnabled() const { return lastfmEnabled || listenbrainzEnabled; }
};

/// Uma reprodução digna de registro: o que tocou, quanto tempo tocou e quando começou.
struct ScrobbleEvent {
  std::string artist;
  std::string title;
  std::string album;
  std::string albumArtist;
  std::string path;
  std::int64_t durationMs = 0;
  std::int64_t startedAt = 0;  // segundos desde 1970 (UTC)
  std::int64_t playedMs = 0;   // tempo efetivamente reproduzido
};

/// Regra do Last.fm, a mesma que o ListenBrainz usa para aceitar "single":
/// a faixa precisa ter mais de 30 s e ter tocado 50% ou 4 minutos, o que vier antes.
bool qualifiesForScrobble(std::int64_t durationMs, std::int64_t playedMs);

}  // namespace wamp
