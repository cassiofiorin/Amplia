#pragma once

#include <vector>

#include <QStringList>
#include <QWidget>

#include "wamp/tag_cache.h"
#include "wamp/types.h"

class QLabel;
class QLineEdit;
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

  wamp::LibraryView view() const;
  QString filter() const;
  /// Seleciona uma visão pelo índice da árvore (0 = todas). Usado também pelos testes.
  void selectView(int row);

 signals:
  /// Mudou a visão ou o texto de busca: a MainWindow refaz a consulta.
  void queryChanged();
  void addFolderRequested();
  void refreshRequested();
  void playRequested(const QString& path);
  void playSelectionRequested(const QStringList& paths);
  void enqueueSelectionRequested(const QStringList& paths);
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

  QTreeWidget* tree_ = nullptr;
  QTableWidget* table_ = nullptr;
  QLineEdit* search_ = nullptr;
  QLabel* summary_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* playButton_ = nullptr;
  QPushButton* queueButton_ = nullptr;
  QAction* removeAction_ = nullptr;
};

}  // namespace wamp::ui
