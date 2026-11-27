#include "musicbrainz.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace wamp::ui {
namespace {

// A URL é montada com QUrlQuery para o Qt codificar espaços e aspas do jeito que o serviço
// espera. Montar a string na mão deixava espaço cru no caminho e o MusicBrainz devolvia 503.
QUrl luceneUrl(const QString& entity, const QStringList& terms, int limit) {
  QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/") + entity);
  QUrlQuery params;
  params.addQueryItem(QStringLiteral("query"), terms.join(QStringLiteral(" AND ")));
  params.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
  params.addQueryItem(QStringLiteral("limit"), QString::number(limit));
  url.setQuery(params);
  return url;
}


// O MusicBrainz pede 1 requisição por segundo por IP e um user agent que identifique a
// aplicação com uma URL de contato. 1,1 s dá folga para a latência.
constexpr int kRequestIntervalMs = 1100;

QString userAgent() {
  return QStringLiteral("Amplia/") + QCoreApplication::applicationVersion() +
         QStringLiteral(" ( https://github.com/cassiofiorin/Amplia )");
}

// Converte "2002-10-22" ou "2002" no ano. Qualquer outra coisa vira 0 (campo em branco).
int yearOf(const QString& date) {
  bool ok = false;
  const int year = date.left(4).toInt(&ok);
  return ok && year > 0 ? year : 0;
}

void fillCommon(TagCandidate& candidate, const QJsonObject& recording) {
  candidate.title = recording.value(QStringLiteral("title")).toString().toStdString();
  candidate.id = recording.value(QStringLiteral("id")).toString().toStdString();
  // `length` vem em milissegundos; ausente em algumas gravações.
  candidate.durationMs = recording.value(QStringLiteral("length")).toVariant().toLongLong();

  const QJsonArray credits = recording.value(QStringLiteral("artist-credit")).toArray();
  if (!credits.isEmpty()) {
    const QJsonObject credit = credits.first().toObject();
    candidate.artist = credit.value(QStringLiteral("name")).toString().toStdString();
    if (candidate.artist.empty()) {
      candidate.artist =
          credit.value(QStringLiteral("artist")).toObject().value(QStringLiteral("name")).toString().toStdString();
    }
  }
}

QJsonObject firstRelease(const QJsonObject& recording, QJsonArray* media) {
  const QJsonArray releases = recording.value(QStringLiteral("releases")).toArray();
  if (releases.isEmpty()) return {};
  const QJsonObject release = releases.first().toObject();
  const QJsonArray releaseMedia = release.value(QStringLiteral("media")).toArray();
  if (media != nullptr) *media = releaseMedia;
  return release;
}

}  // namespace

std::vector<TagCandidate> parseRecordingSearch(const QByteArray& json, QString* error) {
  std::vector<TagCandidate> out;
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error != nullptr) *error = parseError.errorString();
    return out;
  }
  if (!document.isObject()) {
    if (error != nullptr) *error = QStringLiteral("resposta inesperada");
    return out;
  }
  if (error != nullptr) error->clear();

  const QJsonArray recordings = document.object().value(QStringLiteral("recordings")).toArray();
  out.reserve(static_cast<std::size_t>(recordings.size()));
  for (const QJsonValue& value : recordings) {
    const QJsonObject recording = value.toObject();
    TagCandidate candidate;
    fillCommon(candidate, recording);

    QJsonArray media;
    const QJsonObject release = firstRelease(recording, &media);
    if (!release.isEmpty()) {
      candidate.album = release.value(QStringLiteral("title")).toString().toStdString();
      candidate.releaseId = release.value(QStringLiteral("id")).toString().toStdString();
      candidate.year = yearOf(release.value(QStringLiteral("date")).toString());
      const QJsonObject credit = release.value(QStringLiteral("artist-credit")).toArray().isEmpty()
                                     ? QJsonObject()
                                     : release.value(QStringLiteral("artist-credit")).toArray().first().toObject();
      candidate.albumArtist = credit.value(QStringLiteral("name")).toString().toStdString();
    }
    if (!media.isEmpty()) {
      const QJsonObject first = media.first().toObject();
      candidate.discNo = first.value(QStringLiteral("position")).toInt();
      const QJsonArray tracks = first.value(QStringLiteral("track")).toArray();
      if (!tracks.isEmpty()) {
        const QJsonObject track = tracks.first().toObject();
        candidate.trackNo = track.value(QStringLiteral("number")).toString().toInt();
      }
    }
    out.push_back(std::move(candidate));
  }
  return out;
}

std::vector<TagCandidate> parseReleaseSearch(const QByteArray& json, QString* error) {
  std::vector<TagCandidate> out;
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error != nullptr) *error = parseError.errorString();
    return out;
  }
  if (!document.isObject()) {
    if (error != nullptr) *error = QStringLiteral("resposta inesperada");
    return out;
  }
  if (error != nullptr) error->clear();

  const QJsonArray releases = document.object().value(QStringLiteral("releases")).toArray();
  out.reserve(static_cast<std::size_t>(releases.size()));
  for (const QJsonValue& value : releases) {
    const QJsonObject release = value.toObject();
    TagCandidate candidate;
    candidate.id = release.value(QStringLiteral("id")).toString().toStdString();
    candidate.releaseId = candidate.id;
    candidate.album = release.value(QStringLiteral("title")).toString().toStdString();
    candidate.year = yearOf(release.value(QStringLiteral("date")).toString());
    const QJsonArray credits = release.value(QStringLiteral("artist-credit")).toArray();
    if (!credits.isEmpty()) {
      const QJsonObject credit = credits.first().toObject();
      candidate.albumArtist = credit.value(QStringLiteral("name")).toString().toStdString();
      candidate.artist = candidate.albumArtist;
    }
    const QJsonArray media = release.value(QStringLiteral("media")).toArray();
    if (!media.isEmpty()) {
      const QJsonObject first = media.first().toObject();
      candidate.discNo = first.value(QStringLiteral("position")).toInt();
      const QJsonArray tracks = first.value(QStringLiteral("track")).toArray();
      if (!tracks.isEmpty()) {
        const QJsonObject track = tracks.first().toObject();
        candidate.trackNo = track.value(QStringLiteral("number")).toString().toInt();
        candidate.durationMs = track.value(QStringLiteral("length")).toVariant().toLongLong();
        if (candidate.title.empty()) {
          candidate.title = track.value(QStringLiteral("title")).toString().toStdString();
        }
      }
    }
    out.push_back(std::move(candidate));
  }
  return out;
}

std::vector<TagCandidate> parseReleaseTracks(const QByteArray& json, QString* error) {
  std::vector<TagCandidate> out;
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error != nullptr) *error = parseError.errorString();
    return out;
  }
  if (!document.isObject()) {
    if (error != nullptr) *error = QStringLiteral("resposta inesperada");
    return out;
  }
  if (error != nullptr) error->clear();

  const QJsonObject release = document.object();
  const std::string album = release.value(QStringLiteral("title")).toString().toStdString();
  const std::string releaseId = release.value(QStringLiteral("id")).toString().toStdString();
  const int year = yearOf(release.value(QStringLiteral("date")).toString());
  std::string albumArtist;
  const QJsonArray credits = release.value(QStringLiteral("artist-credit")).toArray();
  if (!credits.isEmpty()) {
    albumArtist = credits.first().toObject().value(QStringLiteral("name")).toString().toStdString();
  }

  const QJsonArray media = release.value(QStringLiteral("media")).toArray();
  for (const QJsonValue& mediumValue : media) {
    const QJsonObject medium = mediumValue.toObject();
    const int discNo = medium.value(QStringLiteral("position")).toInt();
    const QJsonArray tracks = medium.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue& trackValue : tracks) {
      const QJsonObject track = trackValue.toObject();
      TagCandidate candidate;
      candidate.releaseId = releaseId;
      candidate.album = album;
      candidate.albumArtist = albumArtist;
      candidate.artist = albumArtist;
      candidate.year = year;
      candidate.discNo = discNo;
      // `number` pode ser "3" ou "A1"; o número útil é o que vem no começo.
      const QString number = track.value(QStringLiteral("number")).toString();
      bool numberOk = false;
      const int trackNo = number.left(3).toInt(&numberOk);
      candidate.trackNo = numberOk ? trackNo : 0;
      candidate.title = track.value(QStringLiteral("title")).toString().toStdString();
      candidate.id = track.value(QStringLiteral("recording"))
                         .toObject()
                         .value(QStringLiteral("id"))
                         .toString()
                         .toStdString();
      // A duração da faixa do lançamento é a que vale para casar com o arquivo; quando falta,
      // a da gravação serve.
      const QJsonObject recording = track.value(QStringLiteral("recording")).toObject();
      candidate.durationMs = track.value(QStringLiteral("length")).toVariant().toLongLong();
      if (candidate.durationMs == 0) {
        candidate.durationMs = recording.value(QStringLiteral("length")).toVariant().toLongLong();
      }
      const QJsonArray trackCredits = recording.value(QStringLiteral("artist-credit")).toArray();
      if (!trackCredits.isEmpty()) {
        const QString name = trackCredits.first().toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) candidate.artist = name.toStdString();
      }
      if (!candidate.title.empty()) out.push_back(std::move(candidate));
    }
  }
  return out;
}

MusicBrainzClient::MusicBrainzClient(QObject* parent) : QObject(parent) {
  network_ = new QNetworkAccessManager(this);
  timer_.setInterval(kRequestIntervalMs);
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, &MusicBrainzClient::pump);
}

void MusicBrainzClient::searchRecordings(const TagQuery& query) {
  QStringList terms;
  const auto add = [&terms](const QString& field, const QString& value) {
    if (!value.trimmed().isEmpty()) terms << QStringLiteral("%1:\"%2\"").arg(field, value.trimmed());
  };
  add(QStringLiteral("artist"), QString::fromStdString(query.artist));
  add(QStringLiteral("recording"), QString::fromStdString(query.title));
  if (terms.isEmpty()) return;
  enqueue(luceneUrl(QStringLiteral("recording"), terms, 15));
}

void MusicBrainzClient::searchReleases(const QString& artist, const QString& album) {
  QStringList terms;
  if (!artist.trimmed().isEmpty()) terms << QStringLiteral("artist:\"%1\"").arg(artist.trimmed());
  if (!album.trimmed().isEmpty()) terms << QStringLiteral("release:\"%1\"").arg(album.trimmed());
  if (terms.isEmpty()) return;
  enqueue(luceneUrl(QStringLiteral("release"), terms, 15));
}

void MusicBrainzClient::lookupRelease(const QString& releaseId) {
  if (releaseId.trimmed().isEmpty()) return;
  QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/") + releaseId.trimmed());
  QUrlQuery params;
  params.addQueryItem(QStringLiteral("inc"), QStringLiteral("recordings+artist-credits"));
  params.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
  url.setQuery(params);
  enqueue(url);
}

void MusicBrainzClient::enqueue(const QUrl& url) {
  queue_.enqueue(url);
  if (!busy_ && !timer_.isActive()) pump();
}

void MusicBrainzClient::cancel() {
  queue_.clear();
  timer_.stop();
}

void MusicBrainzClient::pump() {
  if (queue_.isEmpty()) {
    busy_ = false;
    return;
  }
  busy_ = true;
  const QUrl url = queue_.dequeue();

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  request.setRawHeader("Accept", "application/json");
  QNetworkReply* reply = network_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
  // A próxima só sai depois do intervalo, mesmo que esta responda rápido.
  timer_.start();
}

void MusicBrainzClient::handleReply(QNetworkReply* reply) {
  reply->deleteLater();
  const QByteArray body = reply->readAll();

  if (reply->error() != QNetworkReply::NoError) {
    // 503/429 é o MusicBrainz pedindo calma, não erro de verdade: tenta de novo depois.
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if ((status == 503 || status == 429) && retries_ < kMaxRetries) {
      ++retries_;
      const QUrl url = reply->url();
      QTimer::singleShot(kRetryDelayMs, this, [this, url] {
        queue_.prepend(url);
        pump();
      });
      // Nada de `finished` aqui: para quem espera, a consulta ainda está em andamento.
      // Avisar "tentando de novo" como se fosse resposta fazia o lote marcar a faixa como
      // processada e seguir para a próxima.
      qInfo("amplia: MusicBrainz ocupado (HTTP %d); nova tentativa em %d ms", status,
            kRetryDelayMs);
      return;
    }
    retries_ = 0;
    const QString reason = (status == 503 || status == 429)
                               ? tr("O MusicBrainz está ocupado agora. Tente de novo em alguns segundos.")
                               : reply->errorString();
    emit finished({}, reason);
    return;
  }
  retries_ = 0;

  QString error;
  // O caminho da URL diz qual analisador usar.
  const QString path = reply->url().path();
  // Três rotas caem aqui: busca por gravação, busca por lançamento e abertura de um
  // lançamento pelo id (`/release/<id>`, com as faixas todas).
  std::vector<TagCandidate> candidates;
  if (path.contains(QStringLiteral("/release/"))) {
    candidates = parseReleaseTracks(body, &error);
  } else if (path.contains(QStringLiteral("/release"))) {
    candidates = parseReleaseSearch(body, &error);
  } else {
    candidates = parseRecordingSearch(body, &error);
  }
  emit finished(std::move(candidates), error);
  if (!queue_.isEmpty() && !timer_.isActive()) timer_.start();
}

}  // namespace wamp::ui
