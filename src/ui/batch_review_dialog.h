// Revisão do lote do autotag: comparação lado a lado, 20 por página, estado salvo.
//
// Cada faixa vem em um cartão: à esquerda o que está no arquivo, à direita o que o MusicBrainz
// propôs. Só entra na gravação o campo marcado; exceção combinada com o autor, o campo **vazio**
// pode ser preenchido pelo consenso das edições (ver `consensusFill`). O que é gravado sai da
// revisão; o que não foi marcado continua pendente para a próxima atuação.
#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QVector>

class QCheckBox;
class QLabel;
class QScrollArea;
class QPushButton;
class QVBoxLayout;

namespace wamp {
class TagCache;
}

namespace wamp::ui {

class BatchReviewDialog final : public QDialog {
  Q_OBJECT

 public:
  static constexpr int kPageSize = 20;

  explicit BatchReviewDialog(wamp::TagCache* cache, QWidget* parent = nullptr);

  /// Lê o diário mais recente e devolve os campos de antes (o desfazer de todo o ramo).
  static int undoLastBatch(wamp::TagCache* cache, QWidget* parent, bool confirm = true);

 private:
  struct Card {
    QString path;
    QJsonObject candidate;
    QCheckBox* rowBox = nullptr;
    QVBoxLayout* fieldLayout = nullptr;
    QVector<QPair<QString, QCheckBox*>> fields;
    QLabel* verdict = nullptr;
  };

  void loadPage();
  void applyPage(bool advance);
  void setPage(int page);
  QString settingsKey() const;

  wamp::TagCache* cache_ = nullptr;
  int page_ = 0;
  int total_ = 0;
  QVector<Card> cards_;
  QLabel* warning_ = nullptr;
  QLabel* pageLabel_ = nullptr;
  QPushButton* prev_ = nullptr;
  QPushButton* next_ = nullptr;
  QPushButton* applyButton_ = nullptr;
  QPushButton* saveNext_ = nullptr;
  QPushButton* close_ = nullptr;
  QVBoxLayout* cardsLayout_ = nullptr;
  QScrollArea* scroll_ = nullptr;
};

}  // namespace wamp::ui
