#include "equalizer_dialog.h"

#include "wamp/eq_preset.h"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QMessageBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace wamp::ui {
namespace {

struct Preset {
  const char* name;
  std::array<double, 10> bands;
};

// Curvas de uso comum, arredondadas para o inteiro mais próximo em dB.
constexpr Preset kPresets[] = {
    {"Classical", {0, 0, 0, 0, 0, 0, -7, -7, -7, -9}},
    {"Club", {0, 0, 8, 5, 5, 5, 3, 0, 0, 0}},
    {"Dance", {9, 7, 2, 0, 0, -5, -7, -7, 0, 0}},
    {"Full Bass", {6, 6, 6, 5, 2, -4, -8, -10, -11, -11}},
    {"Full Bass & Treble", {7, 5, 0, -7, -8, 5, 10, 10, 10, 10}},
    {"Full Treble", {-9, -9, -9, -4, 2, 11, 11, 11, 11, 11}},
    {"Laptop", {4, 1, 0, -2, -1, 1, 5, 9, 11, 12}},
    {"Live", {-3, 0, 2, 3, 3, 3, 2, 1, 1, 1}},
    {"Party", {7, 7, 7, 4, 0, 0, -3, -4, -5, -6}},
    {"Pop", {-1, 2, 5, 6, 6, 0, -2, -2, -1, -1}},
    {"Reggae", {0, 0, 0, -3, 0, 5, 5, 0, 0, 0}},
    {"Rock", {8, 5, -2, -3, -1, 3, 6, 7, 8, 8}},
    {"Ska", {-2, -4, -4, 0, 0, 3, 6, 6, 4, 2}},
    {"Soft", {2, 1, 0, 0, 0, -1, -2, -3, -4, -5}},
    {"Soft Rock", {2, 2, 1, 0, -1, -2, -2, -1, 1, 3}},
    {"Techno", {5, 4, 1, 0, -3, -4, 0, 4, 5, 5}},
    {"Vocal", {-2, -3, -1, 0, 3, 5, 5, 3, 1, 0}},
};

const char* kBandLabels[10] = {"70", "180", "320", "600", "1K",
                              "3K", "6K",  "12K", "14K", "16K"};

QString formatDb(double value) {
  if (std::abs(value) < 0.05) return QStringLiteral("0");
  return (value > 0 ? QStringLiteral("+") : QString()) +
         QString::number(static_cast<int>(std::lround(value)));
}

}  // namespace

EqualizerDialog::EqualizerDialog(bool enabled, const std::array<double, 10>& bands, double preamp,
                                 QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Equalizador"));
  setAttribute(Qt::WA_DeleteOnClose, false);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(8);

  auto* header = new QHBoxLayout();
  header->setSpacing(8);
  enabledBox_ = new QCheckBox(tr("Habilitar"), this);
  enabledBox_->setChecked(enabled);
  header->addWidget(enabledBox_);

  presetBox_ = new QComboBox(this);
  presetBox_->addItem(tr("Predefinições…"));
  for (const Preset& preset : kPresets) presetBox_->addItem(QString::fromUtf8(preset.name));
  header->addWidget(presetBox_, 1);

  auto* flat = new QPushButton(tr("Zerar"), this);
  header->addWidget(flat);
  layout->addLayout(header);

  // Importar/exportar em linha própria: `.eqf` vem do Winamp, CSV é o nosso
  // formato de compartilhamento (abre em planilha, sem código para ler).
  auto* files = new QHBoxLayout();
  files->setSpacing(8);
  auto* importButton = new QPushButton(tr("Importar .eqf/CSV…"), this);
  auto* exportButton = new QPushButton(tr("Exportar CSV…"), this);
  auto* saveButton = new QPushButton(tr("Salvar curva…"), this);
  deleteButton_ = new QPushButton(tr("Excluir"), this);
  importButton->setToolTip(tr("Abre um arquivo .eqf ou um CSV do Amplia"));
  exportButton->setToolTip(tr("Grava a curva atual em CSV para compartilhar"));
  saveButton->setToolTip(tr("Guarda a curva na sua lista de predefinições"));
  deleteButton_->setToolTip(tr("Apaga a curva salva selecionada"));
  files->addWidget(importButton);
  files->addWidget(exportButton);
  files->addWidget(saveButton);
  files->addWidget(deleteButton_);
  files->addStretch(1);
  layout->addLayout(files);

  // A curva mostra exatamente o que a cascata faz — desenhada com os coeficientes
  // do motor, não com uma interpolação dos sliders.
  curve_ = new EqCurveView(this);
  layout->addWidget(curve_);
  connect(curve_, &EqCurveView::bandMoved, this, [this](int band, double db) {
    if (band < 0 || band >= 10) return;
    bandSliders_[static_cast<std::size_t>(band)]->setValue(static_cast<int>(std::lround(db)));
  });

  auto* scale = new QHBoxLayout();
  scale->setSpacing(6);

  // Pré-amplificação à esquerda, como no Winamp: ela vale para todas as bandas.
  auto* preampColumn = new QVBoxLayout();
  preampColumn->setSpacing(4);
  preampValue_ = new QLabel(formatDb(preamp), this);
  preampValue_->setAlignment(Qt::AlignHCenter);
  preampValue_->setFixedWidth(26);
  preampColumn->addWidget(preampValue_);
  preampSlider_ = new QSlider(Qt::Vertical, this);
  preampSlider_->setRange(-12, 12);
  preampSlider_->setValue(static_cast<int>(std::lround(preamp)));
  preampSlider_->setTickPosition(QSlider::TicksBothSides);
  preampSlider_->setTickInterval(6);
  preampSlider_->setMinimumHeight(140);
  preampSlider_->setToolTip(tr("Pré-amplificação"));
  preampColumn->addWidget(preampSlider_, 1);
  auto* preampLabel = new QLabel(tr("Pré"), this);
  preampLabel->setAlignment(Qt::AlignHCenter);
  preampLabel->setFixedWidth(26);
  preampColumn->addWidget(preampLabel);
  scale->addLayout(preampColumn);

  for (int band = 0; band < 10; ++band) {
    auto* column = new QVBoxLayout();
    column->setSpacing(4);
    bandValues_[static_cast<std::size_t>(band)] = new QLabel(
        formatDb(bands[static_cast<std::size_t>(band)]), this);
    bandValues_[static_cast<std::size_t>(band)]->setAlignment(Qt::AlignHCenter);
    bandValues_[static_cast<std::size_t>(band)]->setFixedWidth(26);
    column->addWidget(bandValues_[static_cast<std::size_t>(band)]);

    auto* slider = new QSlider(Qt::Vertical, this);
    slider->setRange(-12, 12);
    slider->setValue(static_cast<int>(std::lround(bands[static_cast<std::size_t>(band)])));
    slider->setTickPosition(QSlider::TicksBothSides);
    slider->setTickInterval(6);
    slider->setMinimumHeight(140);
    slider->setFixedWidth(26);
    bandSliders_[static_cast<std::size_t>(band)] = slider;
    column->addWidget(slider, 1);

    auto* label = new QLabel(QString::fromLatin1(kBandLabels[band]), this);
    label->setAlignment(Qt::AlignHCenter);
    label->setFixedWidth(26);
    column->addWidget(label);
    scale->addLayout(column);
  }
  layout->addLayout(scale);

  auto* hint = new QLabel(
      tr("O limitador de pico segura a saída em −0,6 dB, então a pré-amplificação "
         "comprime em vez de estourar."),
      this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  auto* close = new QPushButton(tr("Fechar"), this);
  footer->addWidget(close);
  layout->addLayout(footer);

  connect(close, &QPushButton::clicked, this, &QDialog::close);
  connect(enabledBox_, &QCheckBox::toggled, this, [this] { emitChanged(); });
  connect(preampSlider_, &QSlider::valueChanged, this, [this] { refreshCurve(); emitChanged(); });
  for (QSlider* slider : bandSliders_) {
    connect(slider, &QSlider::valueChanged, this, [this] { refreshCurve(); emitChanged(); });
  }
  connect(flat, &QPushButton::clicked, this, [this] {
    // Zerar apaga a curva inteira: confirma antes, como o "Limpar playlist" faz.
    const auto answer = QMessageBox::question(
        this, tr("Zerar o equalizador"),
        tr("Voltar todas as bandas e a pré-amplificação para 0 dB?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    resetToFlat();
    emitChanged();
  });
  connect(presetBox_, &QComboBox::activated, this, [this](int index) { applyCombo(index); });
  connect(presetBox_, &QComboBox::currentIndexChanged, this, [this] { updateDeleteButton(); });
  connect(saveButton, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString suggestion =
        presetName_ == QStringLiteral("Personalizado") ? tr("Minha curva") : presetName_;
    const QString typed =
        QInputDialog::getText(this, tr("Salvar curva"),
                              tr("Nome da curva:"), QLineEdit::Normal, suggestion, &ok)
            .trimmed();
    if (!ok || typed.isEmpty()) return;

    const std::string slug = wamp::presetFileName(typed.toStdString());
    if (slug.empty()) {
      QMessageBox::warning(this, tr("Salvar curva"),
                           tr("O nome precisa ter pelo menos uma letra ou número."));
      return;
    }

    const QString dirPath = userPresetDir();
    if (!QDir().mkpath(dirPath)) {
      QMessageBox::warning(this, tr("Salvar curva"),
                           tr("Não foi possível criar %1").arg(dirPath));
      return;
    }
    const QString file = dirPath + QLatin1Char('/') + QString::fromStdString(slug) +
                         QStringLiteral(".csv");
    wamp::EqPreset preset;
    preset.name = typed.toStdString();
    preset.preampDb = this->preamp();
    preset.bandsDb = this->bands();
    std::string error;
    if (!wamp::saveEqPresetCsv(file.toStdString(), preset, &error)) {
      QMessageBox::warning(this, tr("Salvar curva"),
                           tr("Não foi possível gravar: %1")
                               .arg(QString::fromStdString(error)));
      return;
    }
    presetName_ = typed;
    reloadPresets();
    const int index = presetBox_->findText(typed, Qt::MatchFixedString);
    if (index > 0) presetBox_->setCurrentIndex(index);
    emitChanged();
  });
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    const int index = presetBox_->currentIndex();
    const int first = 1 + static_cast<int>(std::size(kPresets));
    if (index < first) return;
    const QString file = userFiles_.value(index - first);
    if (file.isEmpty()) return;

    const auto answer = QMessageBox::question(
        this, tr("Excluir curva"),
        tr("Apagar a curva “%1”? O ajuste atual não muda, só sai da lista.")
            .arg(presetBox_->currentText()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    QFile::remove(file);
    reloadPresets();
    presetBox_->setCurrentIndex(0);
    presetName_ = QStringLiteral("Personalizado");
    emitChanged();
  });
  connect(importButton, &QPushButton::clicked, this, [this] {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Importar curva"), QString(),
        tr("Curvas (*.eqf *.csv);;Equalizador (.eqf);;CSV (*.csv)"));
    if (file.isEmpty()) return;

    wamp::EqPreset preset;
    std::string error;
    const bool fromCsv = file.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive);
    const bool ok = fromCsv ? wamp::loadEqPresetCsv(file.toStdString(), &preset, &error)
                            : wamp::loadEqPreset(file.toStdString(), &preset, &error);
    if (!ok) {
      QMessageBox::warning(this, tr("Importar curva"),
                           tr("Não foi possível ler a curva: %1")
                               .arg(QString::fromStdString(error)));
      return;
    }
    applySettings(preset.bandsDb, preset.preampDb, QString::fromStdString(preset.name));
  });
  connect(exportButton, &QPushButton::clicked, this, [this] {
    const QString suggested = presetName_ + QStringLiteral(".csv");
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Exportar curva"), suggested, tr("CSV (*.csv)"));
    if (file.isEmpty()) return;

    wamp::EqPreset preset;
    preset.name = presetName_.toStdString();
    // `preamp` e `bands` também são nomes de parâmetros do construtor: qualificar.
    preset.preampDb = this->preamp();
    preset.bandsDb = this->bands();
    std::string error;
    if (!wamp::saveEqPresetCsv(file.toStdString(), preset, &error)) {
      QMessageBox::warning(this, tr("Exportar curva"),
                           tr("Não foi possível gravar: %1")
                               .arg(QString::fromStdString(error)));
    }
  });

  reloadPresets();
  update();

  // Os sliders já nascem com os valores recebidos; a curva é desenhada aqui
  // (as conexões acima só cobrem mudanças posteriores).
  refreshCurve();
}

QString EqualizerDialog::userPresetDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/presets");
}

void EqualizerDialog::reloadPresets() {
  const QSignalBlocker blocker(presetBox_);
  const int first = 1 + static_cast<int>(std::size(kPresets));
  while (presetBox_->count() > first) presetBox_->removeItem(presetBox_->count() - 1);

  userFiles_.clear();
  for (const std::string& path : wamp::listUserPresets(userPresetDir().toStdString())) {
    wamp::EqPreset preset;
    std::string error;
    const bool ok = wamp::loadEqPresetCsv(path, &preset, &error);
    const QString label = ok && !preset.name.empty()
                              ? QString::fromStdString(preset.name)
                              : QFileInfo(QString::fromStdString(path)).completeBaseName();
    presetBox_->addItem(label);
    userFiles_.append(QString::fromStdString(path));
  }
  updateDeleteButton();
}

void EqualizerDialog::updateDeleteButton() {
  if (deleteButton_ != nullptr) deleteButton_->setEnabled(currentIsUserPreset());
}

bool EqualizerDialog::currentIsUserPreset() const {
  return presetBox_ != nullptr &&
         presetBox_->currentIndex() > static_cast<int>(std::size(kPresets));
}

int EqualizerDialog::builtinPresetCount() { return static_cast<int>(std::size(kPresets)); }

int EqualizerDialog::presetCount() const { return presetBox_ != nullptr ? presetBox_->count() : 0; }

QString EqualizerDialog::presetAt(int index) const {
  if (presetBox_ == nullptr || index < 0 || index >= presetBox_->count()) return QString();
  return presetBox_->itemText(index);
}

bool EqualizerDialog::enabled() const { return enabledBox_ != nullptr && enabledBox_->isChecked(); }

std::array<double, 10> EqualizerDialog::bands() const {
  std::array<double, 10> values{};
  for (int band = 0; band < 10; ++band) {
    values[static_cast<std::size_t>(band)] = static_cast<double>(
        bandSliders_[static_cast<std::size_t>(band)]->value());
  }
  return values;
}

double EqualizerDialog::preamp() const {
  return preampSlider_ != nullptr ? static_cast<double>(preampSlider_->value()) : 0.0;
}

QSlider* EqualizerDialog::bandSlider(int index) const {
  if (index < 0 || index >= 10) return nullptr;
  return bandSliders_[static_cast<std::size_t>(index)];
}

QSlider* EqualizerDialog::preampSlider() const { return preampSlider_; }

EqCurveView* EqualizerDialog::curveView() const { return curve_; }

QCheckBox* EqualizerDialog::enabledBox() const { return enabledBox_; }

void EqualizerDialog::applyCombo(int index) {
  const int first = 1 + static_cast<int>(std::size(kPresets));
  if (index <= 0) return;
  if (index < first) {
    applyPreset(index - 1);
  } else {
    applyUserPreset(index - first);
  }
}

void EqualizerDialog::applyPreset(int index) {
  if (index < 0 || index >= static_cast<int>(std::size(kPresets))) return;
  const Preset& preset = kPresets[index];
  presetName_ = QString::fromUtf8(preset.name);
  updating_ = true;
  for (int band = 0; band < 10; ++band) {
    bandSliders_[static_cast<std::size_t>(band)]->setValue(
        static_cast<int>(preset.bands[static_cast<std::size_t>(band)]));
  }
  updating_ = false;
  emitChanged();
}

void EqualizerDialog::applyUserPreset(int index) {
  const QString file = userFiles_.value(index);
  if (file.isEmpty()) return;
  wamp::EqPreset stored;
  std::string error;
  if (!wamp::loadEqPresetCsv(file.toStdString(), &stored, &error)) {
    QMessageBox::warning(this, tr("Curva"),
                         tr("Não foi possível ler a curva: %1")
                             .arg(QString::fromStdString(error)));
    return;
  }
  applySettings(stored.bandsDb, stored.preampDb, QString::fromStdString(stored.name));
}

void EqualizerDialog::applySettings(const std::array<double, 10>& bands, double preamp,
                                    const QString& name) {
  updating_ = true;
  for (int band = 0; band < 10; ++band) {
    bandSliders_[static_cast<std::size_t>(band)]->setValue(
        static_cast<int>(std::lround(bands[static_cast<std::size_t>(band)])));
  }
  preampSlider_->setValue(static_cast<int>(std::lround(preamp)));
  presetBox_->setCurrentIndex(0);
  updating_ = false;
  presetName_ = name.isEmpty() ? QStringLiteral("Personalizado") : name;
  emitChanged();
}

QString EqualizerDialog::presetName() const { return presetName_; }

void EqualizerDialog::setPresetName(const QString& name) { presetName_ = name; }

void EqualizerDialog::resetToFlat() {
  updating_ = true;
  for (QSlider* slider : bandSliders_) slider->setValue(0);
  preampSlider_->setValue(0);
  updating_ = false;
}

void EqualizerDialog::refreshCurve() {
  if (curve_ == nullptr || preampSlider_ == nullptr) return;
  std::array<double, 10> bands{};
  for (int band = 0; band < 10; ++band) {
    const auto index = static_cast<std::size_t>(band);
    if (bandSliders_[index] == nullptr) return;
    bands[index] = static_cast<double>(bandSliders_[index]->value());
  }
  curve_->setBands(bands);
}

void EqualizerDialog::emitChanged() {
  if (updating_) return;
  for (int band = 0; band < 10; ++band) {
    bandValues_[static_cast<std::size_t>(band)]->setText(
        formatDb(static_cast<double>(bandSliders_[static_cast<std::size_t>(band)]->value())));
  }
  if (preampValue_ != nullptr) {
    preampValue_->setText(formatDb(static_cast<double>(preampSlider_->value())));
  }
  emit changed();
}

}  // namespace wamp::ui
