#pragma once

#include <QDialog>

namespace wamp::ui {

/// "Sobre o Amplia…": versão, data do build, link da release e os créditos de tradução.
class AboutDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace wamp::ui
