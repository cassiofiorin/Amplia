// Cliente do MusicBrainz para o autotag.
//
// Duas partes, de propósito: `parseRecordingSearch` é uma função pura sobre o JSON (testável
// sem rede, com uma resposta gravada) e `MusicBrainzClient` é a parte que fala HTTP, com a fila
// de uma requisição por segundo que o serviço pede.
//
// O serviço exige *user agent* identificando a aplicação; sem isso ele bloqueia. O limite é de
// 1 req/s por IP, então tudo passa por uma fila única — o diálogo pode pedir quantas buscas
// quiser que elas saem no ritmo certo.
#pragma once

#include "wamp/autotag.h"

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QUrl>
#include <QString>
#include <QTimer>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace wamp::ui {

/// Converte a resposta de `recording/?query=…&fmt=json` em candidatos.
/// Devolve lista vazia quando o JSON não tem `recordings` — "nada encontrado" é resposta válida.
std::vector<TagCandidate> parseRecordingSearch(const QByteArray& json, QString* error);

/// Converte a resposta de `release/?query=…&fmt=json` em candidatos (um por release, com a
/// primeira faixa como referência de duração e número).
std::vector<TagCandidate> parseReleaseSearch(const QByteArray& json, QString* error);

/// Converte a resposta de `release/<id>?inc=recordings` em candidatos: **uma faixa por
/// posição** do disco, com álbum, disco, número, título e duração. É o que permite preencher
/// uma pasta inteira com uma consulta só.
std::vector<TagCandidate> parseReleaseTracks(const QByteArray& json, QString* error);

class MusicBrainzClient final : public QObject {
  Q_OBJECT

 public:
  explicit MusicBrainzClient(QObject* parent = nullptr);

  /// Busca gravações por artista e título. A consulta é montada com os campos que existem.
  void searchRecordings(const TagQuery& query);

  /// Busca lançamentos por artista e álbum — o caminho do disco inteiro.
  void searchReleases(const QString& artist, const QString& album);

  /// Abre um lançamento pelo id e devolve todas as faixas dele (resposta em `finished`).
  void lookupRelease(const QString& releaseId);

  /// Descarta o que estiver na fila (o diálogo fechou, por exemplo).
  void cancel();

 signals:
  /// Terminou uma busca: candidatos ou mensagem de erro (nunca os dois).
  void finished(std::vector<wamp::TagCandidate> candidates, QString error);

 private:
  void enqueue(const QUrl& url);
  void pump();
  void handleReply(QNetworkReply* reply);

  QNetworkAccessManager* network_ = nullptr;
  QQueue<QUrl> queue_;
  QTimer timer_;
  bool busy_ = false;
  static constexpr int kMaxRetries = 2;
  static constexpr int kRetryDelayMs = 3000;
  int retries_ = 0;
};

}  // namespace wamp::ui
