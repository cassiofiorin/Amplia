// O candidato do MusicBrainz vira JSON no banco para a revisão não depender de nova consulta.
//
// Congelar a resposta é o que permite retomar a revisão dias depois e ver exatamente a mesma
// comparação — o catálogo muda, e um candidato diferente no meio do caminho confundiria quem já
// tinha decidido.
#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QString>

#include "wamp/autotag.h"

namespace wamp::ui {

inline QJsonObject jsonFromCandidate(const wamp::TagCandidate& candidate) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), QString::fromStdString(candidate.id));
  object.insert(QStringLiteral("artist"), QString::fromStdString(candidate.artist));
  object.insert(QStringLiteral("albumArtist"), QString::fromStdString(candidate.albumArtist));
  object.insert(QStringLiteral("album"), QString::fromStdString(candidate.album));
  object.insert(QStringLiteral("title"), QString::fromStdString(candidate.title));
  object.insert(QStringLiteral("year"), candidate.year);
  object.insert(QStringLiteral("trackNo"), candidate.trackNo);
  object.insert(QStringLiteral("discNo"), candidate.discNo);
  object.insert(QStringLiteral("durationMs"), static_cast<double>(candidate.durationMs));
  object.insert(QStringLiteral("releaseId"), QString::fromStdString(candidate.releaseId));
  return object;
}

inline wamp::TagCandidate candidateFromJson(const QJsonObject& object) {
  wamp::TagCandidate candidate;
  candidate.id = object.value(QStringLiteral("id")).toString().toStdString();
  candidate.artist = object.value(QStringLiteral("artist")).toString().toStdString();
  candidate.albumArtist = object.value(QStringLiteral("albumArtist")).toString().toStdString();
  candidate.album = object.value(QStringLiteral("album")).toString().toStdString();
  candidate.title = object.value(QStringLiteral("title")).toString().toStdString();
  candidate.year = object.value(QStringLiteral("year")).toInt();
  candidate.trackNo = object.value(QStringLiteral("trackNo")).toInt();
  candidate.discNo = object.value(QStringLiteral("discNo")).toInt();
  candidate.durationMs = static_cast<std::int64_t>(
      object.value(QStringLiteral("durationMs")).toDouble());
  candidate.releaseId = object.value(QStringLiteral("releaseId")).toString().toStdString();
  return candidate;
}

inline QString candidateToText(const wamp::TagCandidate& candidate) {
  return QString::fromUtf8(QJsonDocument(jsonFromCandidate(candidate)).toJson(QJsonDocument::Compact));
}

inline wamp::TagCandidate candidateFromText(const QString& text) {
  return candidateFromJson(QJsonDocument::fromJson(text.toUtf8()).object());
}

}  // namespace wamp::ui
