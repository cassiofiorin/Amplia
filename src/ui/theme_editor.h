// Copyright (c) 2026 Cassio Fiorin. Projeto privado (ver LICENSE na raiz do Amplia).
//
// Janela "Aparência": lista de esquemas à esquerda, editor de quatro cores à direita.
// Tudo aplica na hora — a janela atrás repinta enquanto você escolhe ou arrasta um slider —
// e "Reverter" devolve o esquema que estava ativo quando a janela abriu.

#ifndef AMPLIA_UI_THEME_EDITOR_H
#define AMPLIA_UI_THEME_EDITOR_H

#include <QColor>
#include <QDialog>
#include <QVector>

#include "theme.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;

class ThemeDialog final : public QDialog {
  Q_OBJECT

 public:
  ThemeDialog(const QColor& background, const QColor& accent, const QColor& secondary,
              const QColor& buttons, int contrastPercent, QWidget* parent = nullptr);

  QColor background() const { return background_; }
  QColor accent() const { return accent_; }
  QColor secondary() const { return secondary_; }
  QColor buttons() const { return buttons_; }
  int contrastPercent() const;

  /// Recarrega a lista (presets, temas salvos e "Personalizado") e marca o ativo.
  void setThemes(const QVector<wamp::ui::Theme>& saved, const QString& currentId);
  /// Carrega as quatro cores e o contraste sem emitir prévia (usado ao trocar de esquema).
  void setColors(const QColor& background, const QColor& accent, const QColor& secondary,
                 const QColor& buttons, int contrastPercent);
  QString selectedId() const;

 signals:
  /// Emitido a cada mudança de cor ou de contraste (prévia ao vivo).
  void previewChanged();
  /// Escolha de um esquema da lista (id de preset, de tema salvo ou "personalizado").
  void themeChosen(const QString& id);
  void saveAsRequested();
  void importRequested();
  void exportRequested();
  void sampleRequested();
  void deleteRequested(const QString& id);
  void revertRequested();

 private:
  void pick(int which);
  void refreshButtons();
  void loadFromPreset(int presetIndex);

  QColor background_;
  QColor accent_;
  QColor secondary_;
  QColor buttons_;
  QPushButton* backgroundButton_ = nullptr;
  QPushButton* accentButton_ = nullptr;
  QPushButton* secondaryButton_ = nullptr;
  QPushButton* buttonsButton_ = nullptr;
  QSlider* contrastSlider_ = nullptr;
  QLabel* contrastLabel_ = nullptr;
  QComboBox* presetCombo_ = nullptr;
  QListWidget* themeList_ = nullptr;
  QPushButton* deleteButton_ = nullptr;
  QVector<wamp::ui::Theme> saved_;
};

#endif  // AMPLIA_UI_THEME_EDITOR_H
