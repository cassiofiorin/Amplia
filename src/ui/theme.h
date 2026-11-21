// Copyright (c) 2026 Cassio Fiorin. Projeto privado (ver LICENSE na raiz do WAmp).
//
// Esquema de cores do Amplia. Todos os widgets leem daqui em vez de espalhar literais:
// trocar de esquema é trocar o objeto ativo e repintar.

#ifndef AMPLIA_UI_THEME_H
#define AMPLIA_UI_THEME_H

#include <QColor>
#include <QString>
#include <QVector>

namespace wamp::ui {

/// Conjunto de cores de uma paleta. Separado em três grupos: moldura (janela e controles),
/// lista (playlist) e display (o painel de sete segmentos).
struct Theme {
  QString id;
  QString name;

  // Moldura
  QColor windowBg;
  QColor windowText;
  QColor border;
  QColor controlBg;
  QColor controlHover;
  QColor controlPressed;
  QColor controlCheckedBg;
  QColor controlCheckedBorder;
  QColor accent;       ///< seleção, foco, aviso
  QColor textMuted;
  QColor groove;
  QColor handle;

  // Lista
  QColor listBg;
  QColor listText;
  QColor listSelectedBg;
  QColor listSelectedText;
  QColor listNumber;   ///< números de faixa e de fila

  // Display
  QColor displayTop;
  QColor displayBottom;
  QColor displayLit;
  QColor displayDim;
  QColor displayOff;
  QColor displayLabel;
  QColor displayValue;
  QColor displayValueDim;
  QColor displayGrid;
  QColor displayBar;
  QColor displayBarPeak;

  /// Bases originais de um esquema personalizado (vazio nos presets). Ficam aqui para o
  /// CSV poder ser escrito de volta sem tentar "desderivar" as 33 cores.
  QColor customBackground;
  QColor customAccent;
  QColor customSecondary;
  QColor customButtons;
  int customContrast = 100;
};

/// Todas as paletas embutidas, na ordem em que aparecem no menu.
const QVector<Theme>& allThemes();

/// Paleta embutida; devolve a primeira se o id não existir (nunca um objeto vazio).
const Theme& themeById(const QString& id);

/// Paleta ativa agora (a que os widgets usam para se pintar).
QString localizedThemeName(const QString& id);

const Theme& activeTheme();

/// Troca a paleta ativa. Quem pinta à mão precisa se repintar; o stylesheet vem junto.
void setActiveTheme(const QString& id);

/// Instala uma paleta montada em tempo de execução (o "Personalizado").
void setActiveTheme(const Theme& theme);

/// Monta a paleta "Personalizado" a partir de quatro cores e de um contraste (80..140).
/// Todas as outras cores são derivadas: superfícies e bordas do fundo, texto e valores da
/// secundária, aceso e barras do destaque, controles da cor de botões.
Theme makeCustom(const QColor& background, const QColor& accent, const QColor& secondary,
                 const QColor& buttons, int contrastPercent);

/// Mistura duas cores (t = 0 devolve a; t = 1 devolve b).
QColor mixColor(const QColor& a, const QColor& b, double t);

/// Preto ou branco, o que der mais contraste sobre a cor informada.
QColor readableTextOn(const QColor& background);

/// Folha de estilo gerada a partir da paleta.
QString styleSheet(const Theme& theme);

}  // namespace wamp::ui

#endif  // AMPLIA_UI_THEME_H
