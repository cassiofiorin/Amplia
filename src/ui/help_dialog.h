#pragma once

#include <QDialog>

class QTextBrowser;

namespace wamp::ui {

/// Texto da ajuda no idioma ativo, em Markdown. Vem de `help/ajuda-<código>.md` embutido
/// nos recursos: quem traduz mexe num arquivo de texto, nunca no código.
QString helpText();

/// Quem traduziu o idioma ativo, lido de `help/tradutores.txt`. Vazio quando ainda não há
/// nome — o "Sobre" então convida a mandar o pull request.
QString translatorCredit();

/// Janela da ajuda: um `QTextBrowser` mostrando o Markdown do idioma ativo.
class HelpDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit HelpDialog(QWidget* parent = nullptr);

 private:
  QTextBrowser* view_ = nullptr;
};

}  // namespace wamp::ui
