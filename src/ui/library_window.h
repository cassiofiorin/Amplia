#pragma once

#include <vector>

#include <QWidget>

#include "wamp/types.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace wamp::ui {

/// Janela da Biblioteca (F3): lista o que já está indexado no banco e permite apontar
/// pastas-raiz. Não lê arquivos sozinha — quem enfileira as tags é a MainWindow, dona da
/// fila de leitura e do contador de metadados; aqui só se mostra o estado dessa fila.
class LibraryWindow final : public QWidget {
  Q_OBJECT

 public:
  explicit LibraryWindow(QWidget* parent = nullptr);

  /// Recoloca as faixas na lista, preservando a seleção pelo caminho quando possível.
  void setTracks(const std::vector<wamp::TrackInfo>& tracks);

  /// Texto do andamento da leitura de tags; vazio esconde o rótulo.
  void setStatus(const QString& text);

  /// Atualiza uma linha já visível (as tags chegam depois, uma a uma).
  void updateTrack(const wamp::TrackInfo& info);

 signals:
  void addFolderRequested();
  void refreshRequested();
  void playRequested(const QString& path);

 private:
  QListWidget* list_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* emptyLabel_ = nullptr;
  QPushButton* addButton_ = nullptr;
};

}  // namespace wamp::ui
