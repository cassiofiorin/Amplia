// Lote do autotag: busca por texto no MusicBrainz para muitas faixas de uma vez.
//
// A regra do lote é a mesma do diálogo de uma faixa, só que em sequência: o que casa com
// confiança (score e margem) e só preenche campo vazio é gravado sozinho, o resto vai para
// revisão e o que não achou fica registrado como resposta. Cada rodada pega no máximo 250
// faixas; o excedente fica numa fila no banco e volta na próxima.
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QSet>
#include <QStringList>
#include <vector>

#include "tag_journal.h"
#include "wamp/autotag.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace wamp {
class TagCache;
}

namespace wamp::ui {

class MusicBrainzClient;

class BatchTagDialog final : public QDialog {
  Q_OBJECT

 public:
  /// `paths` é a seleção; a fila guardada entra depois dela, respeitando o limite da rodada.
  BatchTagDialog(wamp::TagCache* cache, const QStringList& paths, QWidget* parent = nullptr);

  /// Quantas faixas entram nesta rodada (o resto vai para a fila).
  static constexpr int kBatchLimit = 250;

 public slots:
  /// Começa a rodada. Público para o gancho de teste poder rodar sem clique.
  void start();

 private:
  void buildUi(int total, int sliceSize, int restSize);

  /// Caminho do disco inteiro: uma consulta por pasta, em vez de uma por faixa.
  void startAlbumPass();
  void lookupNextAlbum();
  void tryNextRelease();
  void commitAlbumTracks();
  void finishAlbumPass();
  static QString folderOf(const QString& path);
  void stop();
  void processNext();
  void handleCandidates(std::vector<wamp::TagCandidate> candidates, const QString& error);
  void applyCandidate(const QString& path, const wamp::TagCandidate& candidate);
  void addRow(const QString& path, const QString& verdict);
  void undoLast();
  void finish();
  QString cacheDirectory() const;

  wamp::TagCache* cache_ = nullptr;
  MusicBrainzClient* client_ = nullptr;
  QStringList slice_;
  QStringList rest_;
  QString currentPath_;
  std::size_t index_ = 0;
  int ready_ = 0;
  int review_ = 0;
  int ignored_ = 0;
  int fixed_ = 0;
  int deferred_ = 0;
  QSet<QString> deferredPaths_;
  bool running_ = false;
  bool albumMode_ = false;
  bool albumPass_ = false;      ///< true enquanto estamos consultando discos, não faixas
  bool expectingTracks_ = false;  ///< true quando o pedido pendente é a abertura de um disco
  QStringList albumReleaseIds_;   ///< lançamentos candidatos da pasta atual, do melhor palpite
  int albumTrial_ = 0;            ///< quantos já foram abertos para comparar as faixas
  int albumBestScore_ = -1;       ///< quantas faixas da pasta casaram com o melhor até agora
  std::vector<wamp::TagCandidate> albumBestTracks_;
  QStringList folderTitles_;      ///< títulos da pasta, normalizados, para medir o casamento
  QStringList albumFolders_;
  int albumCursor_ = 0;
  QString currentFolder_;
  QString currentAlbumArtist_;
  QString currentAlbumName_;
  int currentAlbumYear_ = 0;
  QHash<QString, std::vector<wamp::TagCandidate>> folderTracks_;
  wamp::ui::JournalBatch journal_;

  QLabel* summary_ = nullptr;
  QLabel* status_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QListWidget* rows_ = nullptr;
  QCheckBox* albumBox_ = nullptr;
  QPushButton* startButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QPushButton* undoButton_ = nullptr;
  QPushButton* reviewButton_ = nullptr;
  QPushButton* closeButton_ = nullptr;
};

}  // namespace wamp::ui
