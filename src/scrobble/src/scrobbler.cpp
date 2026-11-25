#include "wamp/scrobbler.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace wamp {

bool qualifiesForScrobble(std::int64_t durationMs, std::int64_t playedMs) {
  if (durationMs <= 0 || playedMs <= 0) return false;
  if (durationMs < 30'000) return false;  // faixas curtas nunca entram
  const std::int64_t half = durationMs / 2;
  const std::int64_t fourMinutes = 4 * 60 * 1000;
  const std::int64_t needed = std::min(half, fourMinutes);
  return playedMs >= needed;
}

namespace {

constexpr char kLastfmUrl[] = "https://ws.audioscrobbler.com/2.0/";
constexpr char kListenbrainzUrl[] = "https://api.listenbrainz.org/1/submit-listens";
constexpr char kListenbrainzValidate[] = "https://api.listenbrainz.org/1/validate-token";
constexpr char kLastfmAuthUrl[] = "https://www.last.fm/api/auth/";

QString md5Hex(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

/// api_sig = md5(concatenação de chave+valor em ordem alfabética de chave + secret).
/// `format` e `callback` ficam fora da assinatura (regra da API do Last.fm).
QString lastfmSignature(const QMap<QString, QString>& params, const QString& secret) {
  QString joined;
  for (auto it = params.cbegin(); it != params.cend(); ++it) {
    if (it.key() == QLatin1String("format") || it.key() == QLatin1String("callback")) continue;
    joined += it.key();
    joined += it.value();
  }
  joined += secret;
  return md5Hex(joined.toUtf8());
}

QString jsonString(const QByteArray& data, const QString& key) {
  const QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.isObject()) return doc.object().value(key).toString();
  return {};
}

QString lastfmError(const QByteArray& data) {
  const QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) return QString::fromUtf8(data.left(200));
  const QJsonObject object = doc.object();
  const QString message = object.value(QStringLiteral("message")).toString();
  return message.isEmpty() ? QString::fromUtf8(data.left(200)) : message;
}

/// Espera a resposta com teto de tempo. Autenticação roda no thread da UI, e uma
/// conexão pendurada não pode congelar a janela: aborta e devolve false.
bool waitReply(QNetworkReply* reply, int timeoutMs = 15000) {
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QTimer guard;
  guard.setSingleShot(true);
  QObject::connect(&guard, &QTimer::timeout, &loop, [reply, &loop] {
    reply->abort();
    loop.quit();
  });
  guard.start(timeoutMs);
  loop.exec();
  return reply->isFinished() && reply->error() != QNetworkReply::OperationCanceledError;
}

}  // namespace

struct Scrobbler::Impl {
  struct Pending {
    QString service;  // "lastfm" | "listenbrainz"
    ScrobbleEvent event;
    int attempts = 0;
    qint64 nextAttemptAt = 0;  // ms desde 1970
  };

  ScrobbleConfig config;
  QNetworkAccessManager* nam = nullptr;
  QVector<Pending> queue;
  bool inFlight = false;
  QTimer* retryTimer = nullptr;
  QString lastNowPlaying;  // evita repetir "tocando agora" da mesma faixa

  QString queueFile() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/scrobble-queue.json");
  }

  void loadQueue() {
    QFile file(queueFile());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return;
    for (const QJsonValue& value : doc.array()) {
      const QJsonObject object = value.toObject();
      Pending pending;
      pending.service = object.value(QStringLiteral("service")).toString();
      pending.attempts = object.value(QStringLiteral("attempts")).toInt();
      pending.nextAttemptAt = static_cast<qint64>(object.value(QStringLiteral("next")).toDouble());
      const QJsonObject event = object.value(QStringLiteral("event")).toObject();
      pending.event.artist = event.value(QStringLiteral("artist")).toString().toStdString();
      pending.event.title = event.value(QStringLiteral("title")).toString().toStdString();
      pending.event.album = event.value(QStringLiteral("album")).toString().toStdString();
      pending.event.albumArtist = event.value(QStringLiteral("albumArtist")).toString().toStdString();
      pending.event.path = event.value(QStringLiteral("path")).toString().toStdString();
      pending.event.durationMs = static_cast<std::int64_t>(event.value(QStringLiteral("durationMs")).toDouble());
      pending.event.startedAt = static_cast<std::int64_t>(event.value(QStringLiteral("startedAt")).toDouble());
      pending.event.playedMs = static_cast<std::int64_t>(event.value(QStringLiteral("playedMs")).toDouble());
      if (!pending.service.isEmpty()) queue.push_back(pending);
    }
  }

  void saveQueue() {
    QJsonArray array;
    for (const Pending& pending : queue) {
      QJsonObject event;
      event.insert(QStringLiteral("artist"), QString::fromStdString(pending.event.artist));
      event.insert(QStringLiteral("title"), QString::fromStdString(pending.event.title));
      event.insert(QStringLiteral("album"), QString::fromStdString(pending.event.album));
      event.insert(QStringLiteral("albumArtist"), QString::fromStdString(pending.event.albumArtist));
      event.insert(QStringLiteral("path"), QString::fromStdString(pending.event.path));
      event.insert(QStringLiteral("durationMs"), static_cast<double>(pending.event.durationMs));
      event.insert(QStringLiteral("startedAt"), static_cast<double>(pending.event.startedAt));
      event.insert(QStringLiteral("playedMs"), static_cast<double>(pending.event.playedMs));

      QJsonObject object;
      object.insert(QStringLiteral("service"), pending.service);
      object.insert(QStringLiteral("attempts"), pending.attempts);
      object.insert(QStringLiteral("next"), static_cast<double>(pending.nextAttemptAt));
      object.insert(QStringLiteral("event"), event);
      array.append(object);
    }
    QFile file(queueFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
  }
};

Scrobbler::Scrobbler(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
  impl_->nam = new QNetworkAccessManager(this);
  impl_->retryTimer = new QTimer(this);
  impl_->retryTimer->setSingleShot(true);
  connect(impl_->retryTimer, &QTimer::timeout, this, [this] { flushPending(); });
  impl_->loadQueue();
}

Scrobbler::~Scrobbler() = default;

void Scrobbler::setConfig(const ScrobbleConfig& config) {
  impl_->config = config;
  if (!config.anyEnabled()) {
    impl_->queue.clear();
    impl_->saveQueue();
    return;
  }
  flushPending();
}

ScrobbleConfig Scrobbler::config() const { return impl_->config; }

int Scrobbler::pendingCount() const { return impl_->queue.size(); }

void Scrobbler::nowPlaying(const ScrobbleEvent& event) {
  const QString key = QString::fromStdString(event.path) + QLatin1Char('|') +
                      QString::fromStdString(event.title);
  if (key == impl_->lastNowPlaying) return;
  impl_->lastNowPlaying = key;

  if (event.artist.empty() || event.title.empty()) return;  // sem tags, nada a anunciar

  if (impl_->config.lastfmEnabled && !impl_->config.lastfmSessionKey.empty()) {
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("track.updateNowPlaying"));
    params.insert(QStringLiteral("api_key"), QString::fromStdString(impl_->config.lastfmApiKey));
    params.insert(QStringLiteral("sk"), QString::fromStdString(impl_->config.lastfmSessionKey));
    params.insert(QStringLiteral("artist"), QString::fromStdString(event.artist));
    params.insert(QStringLiteral("track"), QString::fromStdString(event.title));
    if (!event.album.empty()) params.insert(QStringLiteral("album"), QString::fromStdString(event.album));
    if (event.durationMs > 0) {
      params.insert(QStringLiteral("duration"), QString::number(event.durationMs / 1000));
    }
    params.insert(QStringLiteral("format"), QStringLiteral("json"));
    params.insert(QStringLiteral("api_sig"),
                  lastfmSignature(params, QString::fromStdString(impl_->config.lastfmSharedSecret)));

    QUrlQuery query;
    for (auto it = params.cbegin(); it != params.cend(); ++it) query.addQueryItem(it.key(), it.value());
    QNetworkRequest request(QUrl(QString::fromLatin1(kLastfmUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = impl_->nam->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
  }

  if (impl_->config.listenbrainzEnabled && !impl_->config.listenbrainzToken.empty()) {
    QJsonObject metadata;
    metadata.insert(QStringLiteral("artist_name"), QString::fromStdString(event.artist));
    metadata.insert(QStringLiteral("track_name"), QString::fromStdString(event.title));
    if (!event.album.empty()) metadata.insert(QStringLiteral("release_name"), QString::fromStdString(event.album));
    QJsonObject info;
    info.insert(QStringLiteral("submission_client"), QStringLiteral("Amplia"));
    info.insert(QStringLiteral("media_player"), QStringLiteral("Amplia"));
    metadata.insert(QStringLiteral("additional_info"), info);

    QJsonObject listen;
    listen.insert(QStringLiteral("track_metadata"), metadata);
    QJsonArray payload;
    payload.append(listen);

    QJsonObject body;
    body.insert(QStringLiteral("listen_type"), QStringLiteral("playing_now"));
    body.insert(QStringLiteral("payload"), payload);

    QNetworkRequest request(QUrl(QString::fromLatin1(kListenbrainzUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         "Token " + QByteArray::fromStdString(impl_->config.listenbrainzToken));
    QNetworkReply* reply =
        impl_->nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
  }
}

void Scrobbler::scrobble(const ScrobbleEvent& event) {
  if (!impl_->config.anyEnabled()) return;
  if (!qualifiesForScrobble(event.durationMs, event.playedMs)) return;
  if (event.artist.empty() || event.title.empty()) return;

  const QString path = QString::fromStdString(event.path);
  const QString title = QString::fromStdString(event.title);

  const bool hasLastfm = impl_->config.lastfmEnabled && !impl_->config.lastfmSessionKey.empty();
  const bool hasListenbrainz =
      impl_->config.listenbrainzEnabled && !impl_->config.listenbrainzToken.empty();

  // Repetir a mesma faixa no mesmo minuto não duplica o registro.
  for (const Impl::Pending& pending : impl_->queue) {
    if (pending.event.path == event.path && pending.event.title == event.title &&
        std::llabs(pending.event.startedAt - event.startedAt) < 60) {
      return;
    }
  }

  if (hasLastfm) impl_->queue.push_back({QStringLiteral("lastfm"), event, 0, 0});
  if (hasListenbrainz) impl_->queue.push_back({QStringLiteral("listenbrainz"), event, 0, 0});
  (void)path;
  (void)title;
  impl_->saveQueue();
  flushPending();
}

void Scrobbler::flushPending() {
  if (impl_->queue.isEmpty()) return;
  if (impl_->inFlight) return;

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  int index = -1;
  for (int i = 0; i < impl_->queue.size(); ++i) {
    if (impl_->queue.at(i).nextAttemptAt <= now) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    // Tudo em espera: tenta de novo daqui a meio minuto.
    impl_->retryTimer->start(30'000);
    return;
  }

  const Impl::Pending pending = impl_->queue.at(index);
  const ScrobbleEvent& event = pending.event;
  impl_->inFlight = true;

  QNetworkRequest request;
  QByteArray body;
  if (pending.service == QStringLiteral("lastfm")) {
    if (impl_->config.lastfmSessionKey.empty() || impl_->config.lastfmApiKey.empty()) {
      impl_->queue.removeAt(index);
      impl_->saveQueue();
      impl_->inFlight = false;
      flushPending();
      return;
    }
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("track.scrobble"));
    params.insert(QStringLiteral("api_key"), QString::fromStdString(impl_->config.lastfmApiKey));
    params.insert(QStringLiteral("sk"), QString::fromStdString(impl_->config.lastfmSessionKey));
    params.insert(QStringLiteral("artist"), QString::fromStdString(event.artist));
    params.insert(QStringLiteral("track"), QString::fromStdString(event.title));
    if (!event.album.empty()) params.insert(QStringLiteral("album"), QString::fromStdString(event.album));
    if (event.durationMs > 0) {
      params.insert(QStringLiteral("duration"), QString::number(event.durationMs / 1000));
    }
    params.insert(QStringLiteral("timestamp"), QString::number(event.startedAt));
    params.insert(QStringLiteral("format"), QStringLiteral("json"));
    params.insert(QStringLiteral("api_sig"),
                  lastfmSignature(params, QString::fromStdString(impl_->config.lastfmSharedSecret)));
    QUrlQuery query;
    for (auto it = params.cbegin(); it != params.cend(); ++it) query.addQueryItem(it.key(), it.value());
    body = query.toString(QUrl::FullyEncoded).toUtf8();
    request.setUrl(QUrl(QString::fromLatin1(kLastfmUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
  } else {
    if (impl_->config.listenbrainzToken.empty()) {
      impl_->queue.removeAt(index);
      impl_->saveQueue();
      impl_->inFlight = false;
      flushPending();
      return;
    }
    QJsonObject metadata;
    metadata.insert(QStringLiteral("artist_name"), QString::fromStdString(event.artist));
    metadata.insert(QStringLiteral("track_name"), QString::fromStdString(event.title));
    if (!event.album.empty()) metadata.insert(QStringLiteral("release_name"), QString::fromStdString(event.album));
    if (event.durationMs > 0) {
      metadata.insert(QStringLiteral("duration_ms"), static_cast<double>(event.durationMs));
    }
    QJsonObject info;
    info.insert(QStringLiteral("submission_client"), QStringLiteral("Amplia"));
    info.insert(QStringLiteral("media_player"), QStringLiteral("Amplia"));
    metadata.insert(QStringLiteral("additional_info"), info);

    QJsonObject listen;
    listen.insert(QStringLiteral("listened_at"), static_cast<double>(event.startedAt));
    listen.insert(QStringLiteral("track_metadata"), metadata);
    QJsonArray payload;
    payload.append(listen);

    QJsonObject object;
    object.insert(QStringLiteral("listen_type"), QStringLiteral("single"));
    object.insert(QStringLiteral("payload"), payload);

    body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    request.setUrl(QUrl(QString::fromLatin1(kListenbrainzUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         "Token " + QByteArray::fromStdString(impl_->config.listenbrainzToken));
  }

  QNetworkReply* reply = impl_->nam->post(request, body);
  connect(reply, &QNetworkReply::finished, this, [this, reply, index, pending] {
    impl_->inFlight = false;
    const QByteArray data = reply->readAll();
    const bool httpOk = reply->error() == QNetworkReply::NoError;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    bool ok = httpOk && status >= 200 && status < 300;
    QString detail;
    if (ok && pending.service == QStringLiteral("lastfm")) {
      const QJsonDocument doc = QJsonDocument::fromJson(data);
      const QJsonObject object = doc.object();
      if (object.contains(QStringLiteral("error"))) {
        ok = false;
        detail = object.value(QStringLiteral("message")).toString();
      }
    }
    if (!ok) detail = detail.isEmpty() ? lastfmError(data) : detail;

    if (index < impl_->queue.size()) {
      if (ok) {
        impl_->queue.removeAt(index);
        impl_->saveQueue();
        emit statusMessage(QStringLiteral("%1: scrobble enviado (fila: %2)")
                               .arg(pending.service == QStringLiteral("lastfm") ? QStringLiteral("Last.fm")
                                                                               : QStringLiteral("ListenBrainz"))
                               .arg(impl_->queue.size()));
      } else {
        Impl::Pending& item = impl_->queue[index];
        ++item.attempts;
        item.nextAttemptAt = QDateTime::currentMSecsSinceEpoch() + std::min(600, 30 * item.attempts) * 1000;
        impl_->saveQueue();
        emit statusMessage(QStringLiteral("%1: falhou (%2) — fica na fila")
                               .arg(pending.service == QStringLiteral("lastfm") ? QStringLiteral("Last.fm")
                                                                               : QStringLiteral("ListenBrainz"),
                                    detail));
      }
    }
    flushPending();
  });
}

bool Scrobbler::requestLastfmToken(QString* token, QString* authUrl, QString* error) {
  if (impl_->config.lastfmApiKey.empty()) {
    if (error) *error = tr("Falta a API key do Last.fm.");
    return false;
  }
  QMap<QString, QString> params;
  params.insert(QStringLiteral("method"), QStringLiteral("auth.getToken"));
  params.insert(QStringLiteral("api_key"), QString::fromStdString(impl_->config.lastfmApiKey));
  params.insert(QStringLiteral("format"), QStringLiteral("json"));
  params.insert(QStringLiteral("api_sig"),
                lastfmSignature(params, QString::fromStdString(impl_->config.lastfmSharedSecret)));

  QUrlQuery query;
  for (auto it = params.cbegin(); it != params.cend(); ++it) query.addQueryItem(it.key(), it.value());
  QNetworkRequest request(QUrl(QString::fromLatin1(kLastfmUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
  QNetworkReply* reply = impl_->nam->post(request, query.toString(QUrl::FullyEncoded).toUtf8());

  const bool answered = waitReply(reply);
  const QByteArray data = reply->readAll();
  reply->deleteLater();
  if (!answered && error != nullptr) *error = tr("O Last.fm não respondeu a tempo.");
  const QString value = jsonString(data, QStringLiteral("token"));
  if (value.isEmpty()) {
    if (error) *error = lastfmError(data);
    return false;
  }
  if (token) *token = value;
  if (authUrl) {
    *authUrl = QString::fromLatin1(kLastfmAuthUrl) +
               QStringLiteral("?api_key=%1&token=%2")
                   .arg(QString::fromStdString(impl_->config.lastfmApiKey), value);
  }
  return true;
}

bool Scrobbler::exchangeLastfmToken(const QString& token, std::string* sessionKey, QString* error) {
  QMap<QString, QString> params;
  params.insert(QStringLiteral("method"), QStringLiteral("auth.getSession"));
  params.insert(QStringLiteral("api_key"), QString::fromStdString(impl_->config.lastfmApiKey));
  params.insert(QStringLiteral("token"), token);
  params.insert(QStringLiteral("format"), QStringLiteral("json"));
  params.insert(QStringLiteral("api_sig"),
                lastfmSignature(params, QString::fromStdString(impl_->config.lastfmSharedSecret)));

  QUrlQuery query;
  for (auto it = params.cbegin(); it != params.cend(); ++it) query.addQueryItem(it.key(), it.value());
  QNetworkRequest request(QUrl(QString::fromLatin1(kLastfmUrl)));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
  QNetworkReply* reply = impl_->nam->post(request, query.toString(QUrl::FullyEncoded).toUtf8());

  const bool answered = waitReply(reply);
  const QByteArray data = reply->readAll();
  reply->deleteLater();
  if (!answered && error != nullptr) *error = tr("O Last.fm não respondeu a tempo.");

  const QJsonDocument doc = QJsonDocument::fromJson(data);
  const QString key = doc.object().value(QStringLiteral("session")).toObject()
                          .value(QStringLiteral("key")).toString();
  if (key.isEmpty()) {
    if (error) *error = lastfmError(data);
    return false;
  }
  if (sessionKey) *sessionKey = key.toStdString();
  impl_->config.lastfmSessionKey = key.toStdString();
  return true;
}

bool Scrobbler::validateListenbrainzToken(const QString& token, QString* error) {
  if (token.isEmpty()) {
    if (error) *error = tr("Falta o token do ListenBrainz.");
    return false;
  }
  QNetworkRequest request(QUrl(QString::fromLatin1(kListenbrainzValidate)));
  request.setRawHeader("Authorization", "Token " + token.toUtf8());
  QNetworkReply* reply = impl_->nam->get(request);

  const bool answered = waitReply(reply);
  const QByteArray data = reply->readAll();
  reply->deleteLater();
  if (!answered) {
    if (error != nullptr) *error = tr("O ListenBrainz não respondeu a tempo.");
    return false;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.object().value(QStringLiteral("valid")).toBool()) return true;
  if (error) {
    const QString message = doc.object().value(QStringLiteral("message")).toString();
    *error = message.isEmpty() ? QString::fromUtf8(data.left(200)) : message;
  }
  return false;
}

}  // namespace wamp
