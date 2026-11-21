#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>

#include "wamp/scrobble_types.h"

namespace wamp {

/// Envia "tocando agora" e scrobbles para Last.fm e ListenBrainz.
///
/// O envio é assíncrono (QNetworkAccessManager) para nunca travar a interface. O que
/// falha por rede ou limite de taxa fica numa fila em disco e é reenviado depois — um
/// scrobble perdido é pior do que um scrobble atrasado.
class Scrobbler final : public QObject {
  Q_OBJECT

 public:
  explicit Scrobbler(QObject* parent = nullptr);
  ~Scrobbler() override;

  void setConfig(const ScrobbleConfig& config);
  ScrobbleConfig config() const;

  /// Avisa os serviços habilitados que a faixa começou (Last.fm: updateNowPlaying,
  /// ListenBrainz: playing_now). Não entra na fila de scrobble.
  void nowPlaying(const ScrobbleEvent& event);

  /// Registra a reprodução se ela passa na regra de 30 s / 50% ou 4 min.
  void scrobble(const ScrobbleEvent& event);

  int pendingCount() const;
  void flushPending();

  /// Fluxo de autenticação do Last.fm (desktop): pede um token e devolve a URL que o
  /// usuário precisa abrir no navegador. Depois de autorizado, `exchangeLastfmToken`
  /// troca o token pela session key.
  bool requestLastfmToken(QString* token, QString* authUrl, QString* error);
  bool exchangeLastfmToken(const QString& token, std::string* sessionKey, QString* error);

  /// Verificação simples do token do ListenBrainz (GET /1/validate-token).
  bool validateListenbrainzToken(const QString& token, QString* error);

 signals:
  void statusMessage(const QString& text);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wamp
