// Diálogo "Buscar tags…" (Alt+3 → busca por texto no MusicBrainz).
//
// A janela não grava nada: ela devolve o candidato escolhido e quem grava é o Alt+3, com a
// mesma gravação segura e a mesma confirmação de antes. Só campos vazios são preenchidos —
// o que já está na tag é do dono da biblioteca.
#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "wamp/autotag.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace wamp::ui {

class MusicBrainzClient;

class TagSearchDialog final : public QDialog {
  Q_OBJECT

 public:
  /// `query` é o que a faixa sabe hoje (artista, título, álbum, duração).
  explicit TagSearchDialog(const TagQuery& query, QWidget* parent = nullptr);

  /// Candidato escolhido, ou vazio quando a pessoa cancelou.
  const TagCandidate& chosen() const { return chosen_; }
  bool hasChoice() const { return hasChoice_; }

  /// Número de campos que a escolha preencheria (para a mensagem no Alt+3).
  static int fillableFields(const TagQuery& query, const TagCandidate& candidate);

 private:
  void startSearch();
  void showCandidates(std::vector<TagCandidate> candidates, const QString& error);
  void updateSelection();
  void acceptChoice();

  MusicBrainzClient* client_ = nullptr;
  TagQuery query_;
  std::vector<ScoredCandidate> scored_;
  TagCandidate chosen_;
  bool hasChoice_ = false;

  QLineEdit* artistEdit_ = nullptr;
  QLineEdit* titleEdit_ = nullptr;
  QLineEdit* albumEdit_ = nullptr;
  QPushButton* searchButton_ = nullptr;
  QPushButton* useButton_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* reasonsLabel_ = nullptr;
  QListWidget* results_ = nullptr;
};

}  // namespace wamp::ui
