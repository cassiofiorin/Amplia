// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.

#include <QCoreApplication>

#include "theme.h"

#include <QStringBuilder>

#include <algorithm>
#include <cmath>

namespace wamp::ui {
namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

Theme makeGraphite() {
  Theme t;
  t.id = QStringLiteral("grafite");
  t.name = QStringLiteral("Grafite");
  t.windowBg = QColor(0x23, 0x23, 0x23);
  t.windowText = QColor(0xd8, 0xd8, 0xd8);
  t.border = QColor(0x55, 0x55, 0x55);
  t.controlBg = QColor(0x3a, 0x3a, 0x3a);
  t.controlHover = QColor(0x47, 0x47, 0x47);
  t.controlPressed = QColor(0x2e, 0x2e, 0x2e);
  t.controlCheckedBg = QColor(0x5a, 0x4a, 0x20);
  t.controlCheckedBorder = QColor(0xa0, 0x80, 0x30);
  t.accent = QColor(0xe0, 0xb0, 0x50);
  t.textMuted = QColor(0x8a, 0x8a, 0x8a);
  t.groove = QColor(0x3a, 0x3a, 0x3a);
  t.handle = QColor(0xc8, 0xc8, 0xc8);
  t.listBg = QColor(0x1a, 0x1a, 0x1a);
  t.listText = QColor(0xd0, 0xd0, 0xd0);
  t.listSelectedBg = QColor(0x4a, 0x3d, 0x1a);
  t.listSelectedText = QColor(0xff, 0xff, 0xff);
  t.listNumber = QColor(0x6e, 0x6e, 0x6e);
  t.displayTop = QColor(0x06, 0x06, 0x06);
  t.displayBottom = QColor(0x00, 0x00, 0x00);
  t.displayLit = QColor(0xe8, 0xb0, 0x4a);
  t.displayDim = QColor(0x39, 0x39, 0x39);
  t.displayOff = QColor(0x13, 0x13, 0x13);
  t.displayLabel = QColor(0x8f, 0x8f, 0x8f);
  t.displayValue = QColor(0xe6, 0xec, 0xdf);
  t.displayValueDim = QColor(0x2b, 0x2b, 0x2b);
  t.displayGrid = QColor(0x1f, 0x1f, 0x1f);
  t.displayBar = QColor(0x9a, 0x74, 0x2c);
  t.displayBarPeak = QColor(0xe8, 0xc0, 0x6a);
  return t;
}

Theme makeLcd() {
  Theme t = makeGraphite();
  t.id = QStringLiteral("verde-lcd");
  t.name = QCoreApplication::translate("Theme", "Verde LCD");
  t.windowBg = QColor(0x0d, 0x12, 0x0d);
  t.windowText = QColor(0xc8, 0xf5, 0xcc);
  t.border = QColor(0x1e, 0x3a, 0x20);
  t.controlBg = QColor(0x16, 0x24, 0x1a);
  t.controlHover = QColor(0x1d, 0x33, 0x22);
  t.controlPressed = QColor(0x0f, 0x1a, 0x12);
  t.controlCheckedBg = QColor(0x1d, 0x3a, 0x22);
  t.controlCheckedBorder = QColor(0x3f, 0x7a, 0x45);
  t.accent = QColor(0x4d, 0xff, 0x7a);
  t.textMuted = QColor(0x6f, 0x8f, 0x74);
  t.groove = QColor(0x16, 0x24, 0x1a);
  t.handle = QColor(0x7f, 0xe0, 0x8a);
  t.listBg = QColor(0x00, 0x00, 0x00);
  t.listText = QColor(0x9f, 0xe0, 0xa8);
  t.listSelectedBg = QColor(0x15, 0x35, 0x1d);
  t.listSelectedText = QColor(0xe6, 0xff, 0xe9);
  t.listNumber = QColor(0x4f, 0x7a, 0x57);
  t.displayTop = QColor(0x05, 0x05, 0x05);
  t.displayBottom = QColor(0x00, 0x00, 0x00);
  t.displayLit = QColor(0x39, 0xff, 0x6a);
  t.displayDim = QColor(0x36, 0x36, 0x36);
  t.displayOff = QColor(0x12, 0x12, 0x12);
  t.displayLabel = QColor(0x86, 0x8a, 0x86);
  t.displayValue = QColor(0xb9, 0xf2, 0xc0);
  t.displayValueDim = QColor(0x26, 0x26, 0x26);
  t.displayGrid = QColor(0x1c, 0x1c, 0x1c);
  t.displayBar = QColor(0x2f, 0x9e, 0x4a);
  t.displayBarPeak = QColor(0x7d, 0xff, 0xa0);
  return t;
}

Theme makeAmber() {
  Theme t = makeGraphite();
  t.id = QStringLiteral("ambar");
  t.name = QCoreApplication::translate("Theme", "Âmbar");
  t.windowBg = QColor(0x12, 0x0d, 0x06);
  t.windowText = QColor(0xf2, 0xe4, 0xc8);
  t.border = QColor(0x4a, 0x38, 0x18);
  t.controlBg = QColor(0x2a, 0x1f, 0x10);
  t.controlHover = QColor(0x35, 0x27, 0x17);
  t.controlPressed = QColor(0x1d, 0x15, 0x0a);
  t.controlCheckedBg = QColor(0x4a, 0x34, 0x10);
  t.controlCheckedBorder = QColor(0x8a, 0x6a, 0x20);
  t.accent = QColor(0xff, 0xb3, 0x47);
  t.textMuted = QColor(0x8a, 0x7a, 0x5f);
  t.groove = QColor(0x2a, 0x1f, 0x10);
  t.handle = QColor(0xc8, 0xa8, 0x70);
  t.listBg = QColor(0x12, 0x0e, 0x07);
  t.listText = QColor(0xe0, 0xcf, 0xa8);
  t.listSelectedBg = QColor(0x3f, 0x2f, 0x10);
  t.listSelectedText = QColor(0xff, 0xf4, 0xdd);
  t.listNumber = QColor(0x6e, 0x5a, 0x34);
  t.displayTop = QColor(0x05, 0x05, 0x05);
  t.displayBottom = QColor(0x00, 0x00, 0x00);
  t.displayLit = QColor(0xff, 0xb3, 0x47);
  t.displayDim = QColor(0x38, 0x36, 0x33);
  t.displayOff = QColor(0x13, 0x12, 0x10);
  t.displayLabel = QColor(0x8f, 0x88, 0x7c);
  t.displayValue = QColor(0xf0, 0xe3, 0xc6);
  t.displayValueDim = QColor(0x28, 0x27, 0x24);
  t.displayGrid = QColor(0x1e, 0x1d, 0x1a);
  t.displayBar = QColor(0xa8, 0x78, 0x1f);
  t.displayBarPeak = QColor(0xff, 0xd0, 0x8a);
  return t;
}

Theme makeSteel() {
  Theme t = makeGraphite();
  t.id = QStringLiteral("azul-aco");
  t.name = QCoreApplication::translate("Theme", "Azul aço");
  t.windowBg = QColor(0x17, 0x1d, 0x24);
  t.windowText = QColor(0xda, 0xe6, 0xf2);
  t.border = QColor(0x2b, 0x39, 0x47);
  t.controlBg = QColor(0x22, 0x30, 0x3c);
  t.controlHover = QColor(0x2a, 0x3b, 0x49);
  t.controlPressed = QColor(0x18, 0x24, 0x2e);
  t.controlCheckedBg = QColor(0x22, 0x38, 0x4a);
  t.controlCheckedBorder = QColor(0x4a, 0x7f, 0xa8);
  t.accent = QColor(0x7f, 0xb0, 0xff);
  t.textMuted = QColor(0x7d, 0x8f, 0xa3);
  t.groove = QColor(0x22, 0x30, 0x3c);
  t.handle = QColor(0xb8, 0xc8, 0xd8);
  t.listBg = QColor(0x11, 0x18, 0x20);
  t.listText = QColor(0xc2, 0xd2, 0xe2);
  t.listSelectedBg = QColor(0x1e, 0x33, 0x46);
  t.listSelectedText = QColor(0xea, 0xf3, 0xff);
  t.listNumber = QColor(0x5f, 0x74, 0x88);
  t.displayTop = QColor(0x05, 0x06, 0x07);
  t.displayBottom = QColor(0x00, 0x00, 0x00);
  t.displayLit = QColor(0x7f, 0xb0, 0xff);
  t.displayDim = QColor(0x35, 0x38, 0x3b);
  t.displayOff = QColor(0x11, 0x13, 0x15);
  t.displayLabel = QColor(0x8a, 0x90, 0x96);
  t.displayValue = QColor(0xd6, 0xe6, 0xf6);
  t.displayValueDim = QColor(0x26, 0x29, 0x2c);
  t.displayGrid = QColor(0x1b, 0x1d, 0x1f);
  t.displayBar = QColor(0x3f, 0x6f, 0x9e);
  t.displayBarPeak = QColor(0xa8, 0xcc, 0xff);
  return t;
}

Theme makePaper() {
  Theme t = makeGraphite();
  t.id = QStringLiteral("papel");
  t.name = QStringLiteral("Papel");
  t.windowBg = QColor(0xef, 0xea, 0xe1);
  t.windowText = QColor(0x2a, 0x26, 0x20);
  t.border = QColor(0xcf, 0xc6, 0xb6);
  t.controlBg = QColor(0xfb, 0xf8, 0xf2);
  t.controlHover = QColor(0xf2, 0xec, 0xe1);
  t.controlPressed = QColor(0xe6, 0xde, 0xd0);
  t.controlCheckedBg = QColor(0xe8, 0xd9, 0xbd);
  t.controlCheckedBorder = QColor(0xb5, 0x8a, 0x4a);
  t.accent = QColor(0xb5, 0x65, 0x1d);
  t.textMuted = QColor(0x6b, 0x60, 0x52);
  t.groove = QColor(0xdd, 0xd3, 0xc4);
  t.handle = QColor(0x8a, 0x7a, 0x64);
  t.listBg = QColor(0xff, 0xfd, 0xf8);
  t.listText = QColor(0x3a, 0x34, 0x2c);
  t.listSelectedBg = QColor(0xf0, 0xe0, 0xc0);
  t.listSelectedText = QColor(0x2a, 0x20, 0x18);
  t.listNumber = QColor(0x8a, 0x7f, 0x70);
  t.displayTop = QColor(0xfd, 0xfa, 0xf2);
  t.displayBottom = QColor(0xef, 0xe9, 0xda);
  t.displayLit = QColor(0xb5, 0x65, 0x1d);
  t.displayDim = QColor(0xdd, 0xd3, 0xc2);
  t.displayOff = QColor(0xd8, 0xcf, 0xbe);
  t.displayLabel = QColor(0x8a, 0x7f, 0x6a);
  t.displayValue = QColor(0x3a, 0x34, 0x2c);
  t.displayValueDim = QColor(0xb8, 0xad, 0x98);
  t.displayGrid = QColor(0xcf, 0xc3, 0xae);
  t.displayBar = QColor(0xc8, 0x89, 0x2f);
  t.displayBarPeak = QColor(0xe0, 0xa0, 0x50);
  return t;
}

Theme makeClassic() {
  // Referência: a skin base do player original — painéis azul-acinzentados, display preto com
  // verde forte, playlist preta com texto verde e seleção azul. Só contraste, sem relevo.
  Theme t = makeGraphite();
  t.id = QStringLiteral("classico");
  t.name = QCoreApplication::translate("Theme", "Clássico");
  t.windowBg = QColor(0x2a, 0x2e, 0x44);
  t.windowText = QColor(0xd7, 0xdb, 0xea);
  t.border = QColor(0x4b, 0x51, 0x70);
  t.controlBg = QColor(0x3e, 0x44, 0x60);
  t.controlHover = QColor(0x4a, 0x51, 0x70);
  t.controlPressed = QColor(0x33, 0x38, 0x50);
  t.controlCheckedBg = QColor(0x1e, 0x4a, 0x2c);
  t.controlCheckedBorder = QColor(0x00, 0xc8, 0x34);
  t.accent = QColor(0x00, 0xe8, 0x3c);
  t.textMuted = QColor(0x8a, 0x90, 0xa8);
  t.groove = QColor(0x17, 0x1a, 0x28);
  t.handle = QColor(0xc6, 0xcb, 0xdc);
  t.listBg = QColor(0x00, 0x00, 0x00);
  t.listText = QColor(0x00, 0xd2, 0x3a);
  t.listSelectedBg = QColor(0x1b, 0x3f, 0xa8);
  t.listSelectedText = QColor(0xff, 0xff, 0xff);
  t.listNumber = QColor(0x6e, 0x74, 0x90);
  t.displayTop = QColor(0x00, 0x00, 0x00);
  t.displayBottom = QColor(0x00, 0x00, 0x00);
  t.displayLit = QColor(0x00, 0xff, 0x3c);
  t.displayDim = QColor(0x24, 0x26, 0x2e);
  t.displayOff = QColor(0x10, 0x12, 0x18);
  t.displayLabel = QColor(0x8a, 0x90, 0xa8);
  t.displayValue = QColor(0xd7, 0xdb, 0xea);
  t.displayValueDim = QColor(0x24, 0x26, 0x2e);
  t.displayGrid = QColor(0x14, 0x26, 0x1a);
  t.displayBar = QColor(0x0f, 0xa8, 0x3a);
  t.displayBarPeak = QColor(0x7c, 0xff, 0xa0);
  return t;
}

QVector<Theme> buildThemes() {
  QVector<Theme> list;
  list << makeGraphite() << makeClassic() << makeLcd() << makeAmber() << makeSteel()
       << makePaper();
  return list;
}

const QVector<Theme>& themesRef() {
  static const QVector<Theme> themes = buildThemes();
  return themes;
}

QString& activeIdRef() {
  static QString id = QStringLiteral("grafite");
  return id;
}

/// Paleta montada em tempo de execução ("Personalizado"). Fica separada das embutidas
/// porque muda a cada ajuste no editor de cores.
Theme& customRef() {
  static Theme theme = makeGraphite();
  return theme;
}

bool& customActiveRef() {
  static bool active = false;
  return active;
}

}  // namespace

const QVector<Theme>& allThemes() { return themesRef(); }

const Theme& themeById(const QString& id) {
  for (const Theme& t : themesRef()) {
    if (t.id == id) return t;
  }
  return themesRef().first();
}

QString localizedThemeName(const QString& id) {
  if (id == QLatin1String("grafite")) return QCoreApplication::translate("Theme", "Grafite");
  if (id == QLatin1String("classico")) return QCoreApplication::translate("Theme", "Clássico");
  if (id == QLatin1String("verde-lcd")) return QCoreApplication::translate("Theme", "Verde LCD");
  if (id == QLatin1String("ambar")) return QCoreApplication::translate("Theme", "Âmbar");
  if (id == QLatin1String("azul-aco")) return QCoreApplication::translate("Theme", "Azul aço");
  if (id == QLatin1String("papel")) return QCoreApplication::translate("Theme", "Papel");
  if (id == QLatin1String("personalizado")) {
    return QCoreApplication::translate("Theme", "Personalizado");
  }
  return id;
}

const Theme& activeTheme() {
  return customActiveRef() ? customRef() : themeById(activeIdRef());
}

void setActiveTheme(const Theme& theme) {
  customRef() = theme;
  customActiveRef() = true;
  activeIdRef() = theme.id;
}

void setActiveTheme(const QString& id) {
  const bool custom = (id == QLatin1String("personalizado"));
  customActiveRef() = custom;
  activeIdRef() = custom ? QStringLiteral("personalizado") : themeById(id).id;
}

QColor mixColor(const QColor& a, const QColor& b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  return QColor(static_cast<int>(std::lround(a.red() + (b.red() - a.red()) * t)),
                static_cast<int>(std::lround(a.green() + (b.green() - a.green()) * t)),
                static_cast<int>(std::lround(a.blue() + (b.blue() - a.blue()) * t)));
}

QColor readableTextOn(const QColor& background) {
  const double luma = 0.2126 * background.redF() + 0.7152 * background.greenF() +
                      0.0722 * background.blueF();
  return luma > 0.55 ? QColor(0x14, 0x14, 0x14) : QColor(0xff, 0xff, 0xff);
}

Theme makeCustom(const QColor& background, const QColor& accent, const QColor& secondary,
                 const QColor& buttons, int contrastPercent) {
  Theme t = makeGraphite();
  t.id = QStringLiteral("personalizado");
  t.name = QStringLiteral("Personalizado");

  const double k = std::clamp(contrastPercent / 100.0, 0.8, 1.4);
  const double luma =
      0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
  const bool darkBg = luma < 0.5;
  const QColor black(0, 0, 0);
  const QColor white(0xff, 0xff, 0xff);

  // O contraste precisa mexer no que se vê de imediato: o texto e os realces. Antes ele só
  // ajustava bordas e trilhos (0,22 → 0,31 de mistura), o que era imperceptível.
  const QColor textTone = darkBg ? white : black;
  const QColor text = k >= 1.0 ? mixColor(secondary, textTone, (k - 1.0) * 1.6)
                               : mixColor(secondary, background, (1.0 - k) * 1.5);

  t.windowBg = background;
  t.windowText = text;
  t.border = mixColor(background, text, 0.30 * k);
  t.controlBg = buttons;
  t.controlHover = darkBg ? buttons.lighter(118) : buttons.darker(112);
  t.controlPressed = darkBg ? buttons.darker(115) : buttons.lighter(112);
  t.controlCheckedBg = mixColor(buttons, accent, 0.45);
  t.controlCheckedBorder = accent;
  t.accent = accent;
  t.textMuted = mixColor(text, background, std::clamp(0.45 / k, 0.2, 0.75));
  t.groove = mixColor(background, text, 0.25 * k);
  t.handle = mixColor(text, background, 0.25);

  t.listBg = darkBg ? mixColor(background, black, 0.60) : mixColor(background, white, 0.45);
  t.listText = text;
  t.listSelectedBg = mixColor(background, accent, 0.30);
  t.listSelectedText = readableTextOn(t.listSelectedBg);
  t.listNumber = mixColor(text, background, std::clamp(0.5 / k, 0.2, 0.8));

  t.displayTop = mixColor(black, background, 0.10);
  t.displayBottom = black;
  t.displayLit = accent;
  t.displayDim = mixColor(black, text, 0.30 * k);
  t.displayOff = mixColor(black, secondary, 0.12);
  t.displayLabel = mixColor(text, background, std::clamp(0.40 / k, 0.2, 0.7));
  t.displayValue = mixColor(text, accent, 0.20);
  t.displayValueDim = mixColor(black, text, 0.22 * k);
  t.displayGrid = mixColor(black, secondary, 0.14);
  t.displayBar = mixColor(accent, black, 0.55);
  t.displayBarPeak = mixColor(accent, white, 0.35);
  t.customBackground = background;
  t.customAccent = accent;
  t.customSecondary = secondary;
  t.customButtons = buttons;
  t.customContrast = contrastPercent;
  return t;
}

QString styleSheet(const Theme& t) {
  QString s;
  s += "QMainWindow, QDialog, QWidget { background: " % hex(t.windowBg) % "; color: " %
       hex(t.windowText) % "; }\n";
  s += "QLabel#title { color: " % hex(t.windowText) % "; font-size: 13px; font-weight: 600; }\n";
  s += "QLabel#readonlyWarning { color: " % hex(t.accent) % "; }\n";
  s += "QLabel#time { color: " % hex(t.textMuted) % "; font-family: monospace; font-size: 11px; }\n";
  s += "QPushButton { background: " % hex(t.controlBg) % "; border: 1px solid " % hex(t.border) %
       "; border-radius: 3px; padding: 5px 10px; color: " % hex(t.windowText) % "; min-width: 26px; }\n";
  s += "QPushButton:hover { background: " % hex(t.controlHover) % "; }\n";
  s += "QPushButton:pressed { background: " % hex(t.controlPressed) % "; }\n";
  s += "QPushButton:checked { background: " % hex(t.controlCheckedBg) % "; border-color: " %
       hex(t.controlCheckedBorder) % "; }\n";
  s += "QPushButton:disabled { color: " % hex(t.textMuted) % "; border-color: " % hex(t.controlBg) % "; }\n";
  // Caixa desligada tem de parecer desligada: o indicador vem do CheckStyle, então o que dá
  // para marcar aqui é o texto (era o que fazia uma opção indisponível parecer ativa).
  s += "QCheckBox:disabled { color: " % hex(t.textMuted) % "; }\n";
  s += "QLineEdit, QComboBox, QSpinBox { background: " % hex(t.controlBg) % "; color: " %
       hex(t.windowText) % "; border: 1px solid " % hex(t.border) % "; border-radius: 3px; padding: 3px 6px; }\n";
  s += "QComboBox QAbstractItemView { background: " % hex(t.controlBg) % "; color: " %
       hex(t.windowText) % "; selection-background-color: " % hex(t.controlCheckedBg) % "; }\n";
  s += "QSlider::groove:horizontal { height: 4px; background: " % hex(t.groove) %
       "; border-radius: 2px; }\n";
  s += "QSlider::sub-page:horizontal { background: " % hex(t.accent) % "; border-radius: 2px; }\n";
  s += "QSlider::handle:horizontal { background: " % hex(t.handle) %
       "; width: 10px; margin: -5px 0; border-radius: 5px; }\n";
  s += "QSlider::groove:vertical { width: 4px; background: " % hex(t.groove) %
       "; border-radius: 2px; }\n";
  // No vertical o `sub-page` é a parte **acima** do handle. O fader enche de baixo para
  // cima — senão a cor sobe quando o valor desce, ao contrário da curva do equalizador.
  s += "QSlider::add-page:vertical { background: " % hex(t.accent) % "; }\n";
  s += "QSlider::sub-page:vertical { background: " % hex(t.groove) % "; }\n";
  s += "QSlider::handle:vertical { background: " % hex(t.handle) %
       "; height: 10px; margin: 0 -5px; border-radius: 5px; }\n";
  // Sem regra para ::indicator: com uma regra aqui o QStyleSheetStyle assume o desenho e o
  // CheckStyle nunca recebe o pedido (a marca aparecia solta, sem caixa).
  s += "QGroupBox { border: 1px solid " % hex(t.border) %
       "; border-radius: 6px; margin-top: 10px; padding-top: 6px; }\n";
  s += "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
       "left: 10px; padding: 0 4px; color: " % hex(t.textMuted) % "; }\n";
  s += "QMenuBar { background: " % hex(t.listBg) % "; color: " % hex(t.windowText) % "; }\n";
  s += "QMenuBar::item:selected { background: " % hex(t.controlBg) % "; }\n";
  s += "QMenu { background: " % hex(t.controlBg) % "; color: " % hex(t.windowText) %
       "; border: 1px solid " % hex(t.border) % "; }\n";
  s += "QMenu::item:selected { background: " % hex(t.controlCheckedBg) % "; }\n";
  s += "QListWidget { background: " % hex(t.listBg) % "; border: 1px solid " % hex(t.border) %
       "; color: " % hex(t.listText) % "; font-family: monospace; }\n";
  s += "QListWidget::item:selected { background: " % hex(t.listSelectedBg) % "; color: " %
       hex(t.listSelectedText) % "; }\n";
  s += "QStatusBar { color: " % hex(t.textMuted) % "; font-size: 11px; }\n";
  s += "QTabWidget::pane { border: 1px solid " % hex(t.border) % "; }\n";
  s += "QTabBar::tab { background: " % hex(t.controlBg) % "; color: " % hex(t.windowText) %
       "; padding: 5px 10px; }\n";
  s += "QTabBar::tab:selected { background: " % hex(t.controlCheckedBg) % "; color: " %
       hex(t.accent) % "; }\n";
  // A barra tem botões: o trilho é o que sobra **entre** eles. Sem definir os sub-controles,
  // o estilo da plataforma desenha setas no tamanho nativo e elas invadem o trilho.
  const QString bar = "12px";
  s += "QScrollBar:vertical { background: " % hex(t.listBg) % "; width: " % bar %
       "; margin: 0; border: none; }\n";
  s += "QScrollBar::handle:vertical { background: " % hex(t.controlBg) %
       "; border-radius: 6px; min-height: 24px; margin: 2px; }\n";
  s += "QScrollBar::handle:vertical:hover { background: " % hex(t.controlHover) % "; }\n";
  s += "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical { height: " % bar %
       "; background: " % hex(t.listBg) % "; border: none; }\n";
  s += "QScrollBar::sub-page:vertical, QScrollBar::add-page:vertical { background: " %
       hex(t.listBg) % "; }\n";
  s += "QScrollBar:horizontal { background: " % hex(t.listBg) % "; height: " % bar %
       "; margin: 0; border: none; }\n";
  s += "QScrollBar::handle:horizontal { background: " % hex(t.controlBg) %
       "; border-radius: 6px; min-width: 24px; margin: 2px; }\n";
  s += "QScrollBar::handle:horizontal:hover { background: " % hex(t.controlHover) % "; }\n";
  s += "QScrollBar::sub-line:horizontal, QScrollBar::add-line:horizontal { width: " % bar %
       "; background: " % hex(t.listBg) % "; border: none; }\n";
  s += "QScrollBar::sub-page:horizontal, QScrollBar::add-page:horizontal { background: " %
       hex(t.listBg) % "; }\n";
  return s;
}

}  // namespace wamp::ui
