#pragma once

#include <vector>

#include <QStringList>
#include <QWidget>

#include "wamp/radio.h"
#include "wamp/tag_cache.h"
#include "wamp/types.h"

class QLabel;
class QLineEdit;
class QSlider;
class QProgressBar;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QPushButton;

namespace wamp::ui {

/// Janela da Biblioteca: árvore de visões à esquerda (como o ML original), tabela de
/// faixas à direita e busca no topo. Não lê arquivos nem toca nada sozinha — pede à
/// MainWindow, que é dona do banco, da fila de metadados e do player.
class LibraryWindow final : public QWidget {
  Q_OBJECT

 public:
  explicit LibraryWindow(QWidget* parent = nullptr);

  /// Recoloca as faixas na tabela, preservando a seleção pelo caminho.
  void setRows(const std::vector<wamp::LibraryEntry>& rows);
  void setStatus(const QString& text);
  void setSummary(const QString& text);

  /// Progresso da análise local, para o painel de "Composições dinâmicas".
  void setAnalysisProgress(int analysed, int total, qint64 milliseconds);

  wamp::LibraryView view() const;
  QString filter() const;
  /// Seleciona uma visão pelo índice da árvore (0 = todas). Usado também pelos testes.
  void selectView(int row);

  /// Seleciona o nó "Composições dinâmicas" (painel de progresso da análise).
  void selectDynamics();
  /// Seleciona o nó "Rádio local": a tabela passa a mostrar a prévia da estação.
  void selectStation();

  /// Prévia da estação ligada? (nó "Rádio local" selecionado)
  bool stationActive() const { return stationActive_; }

  /// Presets de humor (fábrica + salvos) viram filhos do nó "Humor", na ordem recebida.
  void setHumorPresets(const std::vector<wamp::HumorPreset>& presets);
  /// Seleciona um preset pelo nome. Usado também pelos testes.
  void selectHumor(const QString& name);
  /// Nó de um preset de humor selecionado?
  bool humorActive() const { return humorActive_; }
  /// Rascunho dos sliders (o que a prévia mostra antes de tocar).
  wamp::HumorPoint humorDraft() const;
  double humorBlendDraft() const;
  /// Move os quatro sliders (a partir do env de teste); sem bloquear sinais, de propósito:
  /// mexer neles é modificação pendente.
  void setHumorDraft(double agitation, double tone, double stability, double blend);
  /// Aviso laranja de "modificações não aplicadas".
  void setHumorPending(bool pending);
  /// Nome e id do preset selecionado (id 0 = preset de fábrica).
  QString humorPresetName() const;
  int humorPresetId() const;

  /// Mistura da semente (0 = só a faixa, 100 = só o histórico), sem reemitir o sinal.
  void setSeedBlend(int blend);
  int seedBlend() const;

 signals:
  /// Mudou a visão ou o texto de busca: a MainWindow refaz a consulta.
  void queryChanged();
  void addFolderRequested();
  void refreshRequested();
  void playRequested(const QString& path);
  void playSelectionRequested(const QStringList& paths);
  void enqueueSelectionRequested(const QStringList& paths);
  /// "Tocar estação local": a rádio usa a faixa tocando (ou a selecionada) como semente.
  void stationRequested(const QString& seedPath);
  /// O slider da semente mudou: a MainWindow guarda e refaz a prévia.
  void seedBlendChanged(int blend);
  /// Sliders do humor mudaram (ou outro preset foi escolhido): a MainWindow refaz a prévia.
  /// `dirty` = veio de um slider, então há modificação não aplicada.
  void humorPointChanged(bool dirty);
  /// "Tocar" com um preset de humor aberto: monta a estação com o rascunho atual.
  void humorApplyRequested();
  /// "Enfileirar" com um preset aberto: mesma estação, sem tocar.
  void humorEnqueueRequested();
  void humorSaveRequested();
  void humorDeleteRequested(int presetId);
  /// Clique na coluna de nota: 0 limpa, 1–5 grava.
  void ratingRequested(const QString& path, int rating);
  /// Delete na tabela: a MainWindow confirma em três camadas e tira da biblioteca.
  void removeRequested(const QStringList& paths);
  /// "Limpar biblioteca…": confirmação em três camadas e apaga tudo.
  void clearRequested();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void updateButtons();
  QStringList selectedPaths() const;
  void updateHumorValues();
  void applyHumorDraftFromPreset();
  QTreeWidgetItem* humorSelectedItem() const;

  QTreeWidget* tree_ = nullptr;
  QTableWidget* table_ = nullptr;
  QTreeWidgetItem* dynamicsItem_ = nullptr;
  QTreeWidgetItem* stationItem_ = nullptr;
  QWidget* stationPanel_ = nullptr;
  QLabel* stationHint_ = nullptr;
  QSlider* blendSlider_ = nullptr;
  QWidget* humorPanel_ = nullptr;
  QLabel* humorTitle_ = nullptr;
  QLabel* humorPending_ = nullptr;
  QSlider* agitationSlider_ = nullptr;
  QSlider* toneSlider_ = nullptr;
  QSlider* stabilitySlider_ = nullptr;
  QSlider* humorBlendSlider_ = nullptr;
  QLabel* agitationValue_ = nullptr;
  QLabel* toneValue_ = nullptr;
  QLabel* stabilityValue_ = nullptr;
  QLabel* humorBlendValue_ = nullptr;
  QPushButton* humorSaveButton_ = nullptr;
  QPushButton* humorRemoveButton_ = nullptr;
  std::vector<wamp::HumorPreset> humorPresets_;
  std::vector<QTreeWidgetItem*> humorItems_;
  QWidget* searchBar_ = nullptr;
  QWidget* dynamicsPanel_ = nullptr;
  QLabel* dynamicsTitle_ = nullptr;
  QLabel* dynamicsCount_ = nullptr;
  QLabel* dynamicsHint_ = nullptr;
  QProgressBar* dynamicsProgress_ = nullptr;
  bool dynamicsActive_ = false;
  bool stationActive_ = false;
  bool humorActive_ = false;
  QLineEdit* search_ = nullptr;
  QLabel* summary_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* playButton_ = nullptr;
  QPushButton* queueButton_ = nullptr;
  QPushButton* stationButton_ = nullptr;
  QAction* removeAction_ = nullptr;
};

}  // namespace wamp::ui
