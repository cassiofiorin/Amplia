// Lote do autotag: busca por texto no MusicBrainz para muitas faixas de uma vez.
//
// A regra do lote é a mesma do diálogo de uma faixa, só que em sequência: o que casa com
// confiança (score e margem) e só preenche campo vazio é gravado sozinho, o resto vai para
// revisão e o que não achou fica registrado como resposta. Cada rodada pega no máximo 250
// faixas; o excedente fica numa fila no banco e volta na próxima.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <vector>

#include "tag_journal.h"
#include "wamp/autotag.h"

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
  bool running_ = false;
  wamp::ui::JournalBatch journal_;

  QLabel* summary_ = nullptr;
  QLabel* queueLabel_ = nullptr;
  QLabel* status_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QListWidget* rows_ = nullptr;
  QPushButton* startButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QPushButton* undoButton_ = nullptr;
  QPushButton* closeButton_ = nullptr;
};

}  // namespace wamp::ui
