#pragma once

#include <array>

#include <QStringList>

#include <QDialog>

#include "eq_curve_view.h"
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace wamp::ui {

/// Janela do equalizador: dez bandas verticais, pré-amplificação e as curvas
/// predefinidas do Winamp. Modeless de propósito — dá para arrastar um slider
/// ouvindo o resultado.
class EqualizerDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit EqualizerDialog(bool enabled, const std::array<double, 10>& bands, double preamp,
                           QWidget* parent = nullptr);

  bool enabled() const;
  std::array<double, 10> bands() const;
  double preamp() const;

  /// Usado pelo `wamp-ui-smoke` para exercitar a janela sem mouse.
  QSlider* bandSlider(int index) const;
  QSlider* preampSlider() const;
  QCheckBox* enabledBox() const;
  /// A curva desenhada (para os testes conferirem o desenho).
  EqCurveView* curveView() const;
  void applyPreset(int index);       ///< curva embutida (índice em `kPresets`)
  void applyUserPreset(int index);   ///< curva salva pelo usuário (índice em `userFiles_`)
  /// Aplica uma curva importada (`wamp::EqPreset`) e marca o combo como personalizado.
  void applySettings(const std::array<double, 10>& bands, double preamp, const QString& name);
  /// Nome da curva atual (para exportar).
  QString presetName() const;
  void setPresetName(const QString& name);

  /// Curvas salvas pelo usuário (arquivos `.csv`), para o teste conferir a lista.
  int presetCount() const;
  QString presetAt(int index) const;
  static QString userPresetDir();
  static int builtinPresetCount();

 signals:
  void changed();

 private:
  void emitChanged();
  void refreshCurve();
  void resetToFlat();
  void applyCombo(int comboIndex);
  void reloadPresets();
  void updateDeleteButton();
  bool currentIsUserPreset() const;

  QCheckBox* enabledBox_ = nullptr;
  QComboBox* presetBox_ = nullptr;
  EqCurveView* curve_ = nullptr;
  QString presetName_ = QStringLiteral("Personalizado");
  QStringList userFiles_;
  class QPushButton* deleteButton_ = nullptr;
  QSlider* preampSlider_ = nullptr;
  QLabel* preampValue_ = nullptr;
  std::array<QSlider*, 10> bandSliders_{};
  std::array<QLabel*, 10> bandValues_{};
  bool updating_ = false;
};

}  // namespace wamp::ui
