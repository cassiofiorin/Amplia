#pragma once

#include <QDialog>

class QTextBrowser;

namespace wamp::ui {

/// Texto da ajuda no idioma ativo, em Markdown. Vem de `help/ajuda-<código>.md` embutido
/// nos recursos: quem traduz mexe num arquivo de texto, nunca no código.
QString helpText();

/// Créditos de tradução de todos os idiomas, em texto corrido ("PT-BR (Nativo) Fulano;
/// EN (Nativo) Beltrano"), lidos de `help/tradutores.txt`. Idioma sem nome mostra "aberta",
/// que é o convite ao pull request.
QString translationCredits();

/// Janela da ajuda: um `QTextBrowser` mostrando o Markdown do idioma ativo.
class HelpDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit HelpDialog(QWidget* parent = nullptr);

 private:
  QTextBrowser* view_ = nullptr;
};

}  // namespace wamp::ui
