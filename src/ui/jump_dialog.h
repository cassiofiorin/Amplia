#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;

namespace wamp::ui {

/// Uma linha pesquisável do Jump to File: o caminho mais os campos que entram na busca.
struct JumpEntry {
  QString path;
  QString title;
  QString artist;
  QString album;
};

/// "Jump to File" no espírito do plugin do Winamp: uma caixa de busca, a lista filtrada
/// enquanto se digita, Enter toca e Ctrl+Enter enfileira.
class JumpDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit JumpDialog(QVector<JumpEntry> entries, QWidget* parent = nullptr);

  /// Entrada de busca programática (usada nos testes de fumaça).
  void setQuery(const QString& text);
  int resultCount() const;
  QString currentPath() const;

 signals:
  void playRequested(const QString& path);
  void queueRequested(const QString& path);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void applyFilter();
  void activateCurrent(bool queue);
  void updateCounter(int shown);

  QVector<JumpEntry> entries_;
  QLineEdit* search_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* counter_ = nullptr;
};

}  // namespace wamp::ui
