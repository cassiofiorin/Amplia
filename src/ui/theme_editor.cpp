// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.

#include "theme_editor.h"

#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "theme.h"

namespace {

/// Botão que se desenha como amostra da cor: fundo na própria cor, texto legível por cima.
void styleSwatch(QPushButton* button, const QColor& color, const QString& label) {
  const QColor text = wamp::ui::readableTextOn(color);
  // Só o hex: o nome da cor já está no rótulo à esquerda da linha.
  Q_UNUSED(label);
  button->setText(color.name(QColor::HexRgb).toUpper());
  button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; "
                                       "border: 1px solid %3; border-radius: 4px; "
                                       "padding: 4px 8px; text-align: center; }")
                            .arg(color.name(QColor::HexRgb), text.name(QColor::HexRgb),
                                 wamp::ui::mixColor(color, text, 0.35).name(QColor::HexRgb)));
}

}  // namespace

ThemeDialog::ThemeDialog(const QColor& background, const QColor& accent, const QColor& secondary,
                         const QColor& buttons, int contrastPercent, QWidget* parent)
    : QDialog(parent),
      background_(background),
      accent_(accent),
      secondary_(secondary),
      buttons_(buttons) {
  setWindowTitle(tr("Aparência"));
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(10);

  auto* columns = new QHBoxLayout();
  columns->setSpacing(12);
  outer->addLayout(columns, 1);

  // --- Lista de esquemas ---------------------------------------------------
  auto* left = new QVBoxLayout();
  left->addWidget(new QLabel(tr("Esquemas"), this));
  themeList_ = new QListWidget(this);
  themeList_->setMinimumWidth(170);
  themeList_->setMaximumWidth(220);
  left->addWidget(themeList_, 1);
  columns->addLayout(left);

  // --- Editor --------------------------------------------------------------
  auto* right = new QVBoxLayout();
  right->setSpacing(8);
  auto* intro = new QLabel(
      tr("Quatro cores e um contraste. O resto da paleta é derivado delas."), this);
  intro->setWordWrap(true);
  right->addWidget(intro);

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(6);

  // Rótulos e dicas em literais para o lupdate enxergar: tr() com variável em tempo de
  // execução não entra no .ts.
  const struct {
    QString label;
    QPushButton** button;
    QColor* color;
    QString hint;
  } rows[] = {
      {tr("Fundo"), &backgroundButton_, &background_,
       tr("janela, painéis, bordas e trilhos")},
      {tr("Destaque"), &accentButton_, &accent_,
       tr("display aceso, espectro, seleção")},
      {tr("Secundária"), &secondaryButton_, &secondary_,
       tr("texto, valores e rótulos")},
      {tr("Botões"), &buttonsButton_, &buttons_,
       tr("fundo dos botões e do transporte")},
  };

  int row = 0;
  for (const auto& item : rows) {
    auto* label = new QLabel(item.label, this);
    label->setMinimumWidth(80);
    auto* button = new QPushButton(this);
    button->setMinimumHeight(30);
    button->setMinimumWidth(120);
    button->setCursor(Qt::PointingHandCursor);
    *item.button = button;
    const int which = row;
    connect(button, &QPushButton::clicked, this, [this, which] { pick(which); });

    auto* hint = new QLabel(item.hint, this);
    hint->setEnabled(false);

    grid->addWidget(label, row, 0);
    grid->addWidget(button, row, 1);
    grid->addWidget(hint, row, 2);
    ++row;
  }
  grid->setColumnStretch(1, 1);
  right->addLayout(grid);

  auto* contrastRow = new QHBoxLayout();
  contrastRow->addWidget(new QLabel(tr("Contraste"), this));
  contrastSlider_ = new QSlider(Qt::Horizontal, this);
  contrastSlider_->setRange(80, 140);
  contrastSlider_->setSingleStep(5);
  contrastSlider_->setPageStep(10);
  contrastSlider_->setValue(contrastPercent);
  contrastLabel_ = new QLabel(QStringLiteral("%1%").arg(contrastPercent), this);
  contrastLabel_->setMinimumWidth(42);
  contrastLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  contrastRow->addWidget(contrastSlider_, 1);
  contrastRow->addWidget(contrastLabel_);
  right->addLayout(contrastRow);

  auto* copyRow = new QHBoxLayout();
  copyRow->addWidget(new QLabel(tr("Partir de"), this));
  presetCombo_ = new QComboBox(this);
  presetCombo_->addItem(tr("— escolha um esquema —"));
  for (const wamp::ui::Theme& theme : wamp::ui::allThemes()) {
    presetCombo_->addItem(wamp::ui::localizedThemeName(theme.id));
  }
  copyRow->addWidget(presetCombo_, 1);
  right->addLayout(copyRow);

  // A janela larga revela mais: o painel distribui o espaço que sobra, e a capa só cabe
  // quando há vão entre os medidores e o espectro. Fica como dica porque é o tipo de
  // detalhe que ninguém descobre sozinho.
  auto* widthHint = new QLabel(
      tr("Alargar a janela revela mais: o vão entre os medidores e o espectro recebe a capa "
         "do álbum quando houver espaço para ela, e as bandas do espectro crescem junto."),
      this);
  widthHint->setWordWrap(true);
  widthHint->setEnabled(false);
  right->addWidget(widthHint);
  right->addStretch(1);
  columns->addLayout(right, 1);

  // --- Ações ---------------------------------------------------------------
  auto* actions = new QHBoxLayout();
  auto* saveAs = new QPushButton(tr("Salvar como…"), this);
  auto* import = new QPushButton(tr("Importar CSV…"), this);
  auto* exportButton = new QPushButton(tr("Exportar CSV…"), this);
  auto* sample = new QPushButton(tr("Modelo CSV…"), this);
  deleteButton_ = new QPushButton(tr("Excluir"), this);
  actions->addWidget(saveAs);
  actions->addWidget(import);
  actions->addWidget(exportButton);
  actions->addWidget(sample);
  actions->addWidget(deleteButton_);
  actions->addStretch(1);
  auto* revert = new QPushButton(QStringLiteral("Reverter"), this);
  auto* close = new QPushButton(tr("Fechar"), this);
  close->setDefault(true);
  actions->addWidget(revert);
  actions->addWidget(close);
  outer->addLayout(actions);

  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(revert, &QPushButton::clicked, this, &ThemeDialog::revertRequested);
  connect(saveAs, &QPushButton::clicked, this, &ThemeDialog::saveAsRequested);
  connect(import, &QPushButton::clicked, this, &ThemeDialog::importRequested);
  connect(exportButton, &QPushButton::clicked, this, &ThemeDialog::exportRequested);
  connect(sample, &QPushButton::clicked, this, &ThemeDialog::sampleRequested);
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    const QString id = selectedId();
    if (!id.isEmpty()) emit deleteRequested(id);
  });

  connect(contrastSlider_, &QSlider::valueChanged, this, [this](int value) {
    contrastLabel_->setText(QStringLiteral("%1%").arg(value));
    emit previewChanged();
  });
  connect(presetCombo_, &QComboBox::activated, this, [this](int index) {
    loadFromPreset(index - 1);
  });
  connect(themeList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
    if (item == nullptr) return;
    const QString id = item->data(Qt::UserRole).toString();
    deleteButton_->setEnabled(!item->data(Qt::UserRole + 1).toBool());
    emit themeChosen(id);
  });

  refreshButtons();
}

int ThemeDialog::contrastPercent() const { return contrastSlider_->value(); }

QString ThemeDialog::selectedId() const {
  const QListWidgetItem* item = themeList_->currentItem();
  return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

void ThemeDialog::setColors(const QColor& background, const QColor& accent,
                            const QColor& secondary, const QColor& buttons, int contrastPercent) {
  const QSignalBlocker blockSlider(contrastSlider_);
  background_ = background;
  accent_ = accent;
  secondary_ = secondary;
  buttons_ = buttons;
  contrastSlider_->setValue(contrastPercent);
  contrastLabel_->setText(QStringLiteral("%1%").arg(contrastPercent));
  refreshButtons();
}

void ThemeDialog::setThemes(const QVector<wamp::ui::Theme>& saved, const QString& currentId) {
  saved_ = saved;
  const QSignalBlocker block(themeList_);
  themeList_->clear();

  const auto addItem = [&](const QString& id, const QString& name, bool builtin) {
    auto* item = new QListWidgetItem(name, themeList_);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, builtin);
    if (id == currentId) themeList_->setCurrentItem(item);
  };

  auto* presetsHeader = new QListWidgetItem(tr("Embutidos"), themeList_);
  presetsHeader->setFlags(Qt::NoItemFlags);
  presetsHeader->setForeground(QColor(0x8a, 0x8a, 0x8a));
  for (const wamp::ui::Theme& theme : wamp::ui::allThemes()) {
    addItem(theme.id, wamp::ui::localizedThemeName(theme.id), true);
  }

  auto* savedHeader = new QListWidgetItem(tr("Meus temas"), themeList_);
  savedHeader->setFlags(Qt::NoItemFlags);
  savedHeader->setForeground(QColor(0x8a, 0x8a, 0x8a));
  if (saved_.isEmpty()) {
    auto* empty = new QListWidgetItem(tr("(nenhum ainda)"), themeList_);
    empty->setFlags(Qt::NoItemFlags);
  } else {
    for (const wamp::ui::Theme& theme : saved_) addItem(theme.id, theme.name, false);
  }

  addItem(QStringLiteral("personalizado"), wamp::ui::localizedThemeName(QStringLiteral("personalizado")), true);
  deleteButton_->setEnabled(false);
}

void ThemeDialog::pick(int which) {
  QColor current = which == 0 ? background_ : which == 1 ? accent_ : which == 2 ? secondary_ : buttons_;
  const QString title = which == 0 ? tr("Cor de fundo")
                        : which == 1 ? tr("Cor de destaque")
                        : which == 2 ? tr("Cor secundária")
                                     : tr("Cor dos botões");
  // Diálogo do Qt (não o nativo): roda/HSV, hex e conta-gotas iguais em qualquer desktop.
  const QColor chosen = QColorDialog::getColor(current, this, title, QColorDialog::DontUseNativeDialog);
  if (!chosen.isValid()) return;
  switch (which) {
    case 0: background_ = chosen; break;
    case 1: accent_ = chosen; break;
    case 2: secondary_ = chosen; break;
    default: buttons_ = chosen; break;
  }
  refreshButtons();
  emit previewChanged();
}

void ThemeDialog::refreshButtons() {
  styleSwatch(backgroundButton_, background_, tr("Fundo"));
  styleSwatch(accentButton_, accent_, tr("Destaque"));
  styleSwatch(secondaryButton_, secondary_, tr("Secundária"));
  styleSwatch(buttonsButton_, buttons_, tr("Botões"));
}

void ThemeDialog::loadFromPreset(int presetIndex) {
  if (presetIndex < 0 || presetIndex >= wamp::ui::allThemes().size()) return;
  const wamp::ui::Theme& preset = wamp::ui::allThemes()[presetIndex];
  setColors(preset.windowBg, preset.accent, preset.windowText, preset.controlBg,
            contrastSlider_->value());
  emit previewChanged();
}
