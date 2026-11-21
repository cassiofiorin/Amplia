#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace wamp::ui {

/// Extensões que o FFmpeg abre de forma confiável. A varredura de pasta usa esta lista
/// para não encher a playlist com capas, `.txt` e afins.
inline const QStringList& audioExtensions() {
  static const QStringList kExtensions = {
      QStringLiteral("mp3"),  QStringLiteral("mp2"), QStringLiteral("flac"), QStringLiteral("ogg"),
      QStringLiteral("oga"),  QStringLiteral("opus"), QStringLiteral("wav"),  QStringLiteral("m4a"),
      QStringLiteral("m4b"),  QStringLiteral("aac"),  QStringLiteral("wma"),  QStringLiteral("ape"),
      QStringLiteral("wv"),   QStringLiteral("mpc"),  QStringLiteral("tta"),  QStringLiteral("aiff"),
      QStringLiteral("aif"),  QStringLiteral("dsf"),  QStringLiteral("dff"),  QStringLiteral("ac3"),
      QStringLiteral("spx"),  QStringLiteral("caf")};
  return kExtensions;
}

inline const QStringList& playlistExtensions() {
  static const QStringList kExtensions = {QStringLiteral("m3u"), QStringLiteral("m3u8"),
                                          QStringLiteral("pls"), QStringLiteral("b4s"),
                                          QStringLiteral("wpl")};
  return kExtensions;
}

/// Não é para tocar — é só para a mensagem de recusa dizer o que é.
inline const QStringList& videoExtensions() {
  static const QStringList kExtensions = {
      QStringLiteral("mp4"),  QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("webm"),
      QStringLiteral("mov"),  QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
      QStringLiteral("m4v"),  QStringLiteral("flv"), QStringLiteral("ts"),  QStringLiteral("ogv"),
      QStringLiteral("3gp")};
  return kExtensions;
}

inline QString suffixOf(const QString& path) {
  return QFileInfo(path).suffix().toLower();
}

inline bool isAudioFile(const QString& path) { return audioExtensions().contains(suffixOf(path)); }

inline bool isPlaylistFile(const QString& path) {
  return playlistExtensions().contains(suffixOf(path));
}

inline bool isVideoFile(const QString& path) { return videoExtensions().contains(suffixOf(path)); }

}  // namespace wamp::ui
