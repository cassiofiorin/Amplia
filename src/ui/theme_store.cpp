#include <QCoreApplication>

#include "theme_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace wamp::ui {
namespace {

}  // namespace

QString themesDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/presets/themes");
}

QString themeCsvHeader() {
  return QStringLiteral("id,nome,fundo,destaque,secundaria,botoes,contraste");
}

QString themeCsvSample() {
  // A linha de exemplo começa com '#': o importador ignora, e ela serve de molde para
  // quem for montar um arquivo à mão (ou pedir para outra ferramenta montar).
  return themeCsvHeader() +
         QStringLiteral("\n#exemplo,Meu Tema,#1B1E24,#4DFF7A,#C8F5CC,#262B33,110\n");
}

QString themeSlug(const QString& name) {
  QString base = name.normalized(QString::NormalizationForm_KD).toLower();
  QString out;
  for (const QChar ch : base) {
    if (ch.isMark()) continue;
    if (ch.isLetterOrNumber()) {
      out.append(ch);
    } else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-'))) {
      out.append(QLatin1Char('-'));
    }
  }
  while (out.endsWith(QLatin1Char('-'))) out.chop(1);
  return out.isEmpty() ? QStringLiteral("tema") : out;
}

QString themeToCsvRow(const Theme& theme) {
  // Só as quatro bases e o contraste: o resto é derivado por makeCustom na leitura.
  const QColor bg = theme.customBackground.isValid() ? theme.customBackground : theme.windowBg;
  const QColor accent =
      theme.customAccent.isValid() ? theme.customAccent : theme.displayLit;
  const QColor secondary =
      theme.customSecondary.isValid() ? theme.customSecondary : theme.windowText;
  const QColor buttons =
      theme.customButtons.isValid() ? theme.customButtons : theme.controlBg;
  return QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
      .arg(theme.id, theme.name, bg.name(QColor::HexRgb), accent.name(QColor::HexRgb),
           secondary.name(QColor::HexRgb), buttons.name(QColor::HexRgb))
      .arg(theme.customContrast);
}

bool themeFromCsvRow(const QString& line, Theme* out, QString* error) {
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) return false;

  const QStringList fields = trimmed.split(QLatin1Char(','));
  if (fields.size() < 7) {
    if (error != nullptr) *error = QCoreApplication::translate("Theme", "linha com menos de sete campos");
    return false;
  }
  const QString id = themeSlug(fields.at(0));
  const QString name = fields.at(1).trimmed();
  const QColor background(fields.at(2).trimmed());
  const QColor accent(fields.at(3).trimmed());
  const QColor secondary(fields.at(4).trimmed());
  const QColor buttons(fields.at(5).trimmed());
  bool ok = false;
  const int contrast = fields.at(6).trimmed().toInt(&ok);
  if (name.isEmpty() || !background.isValid() || !accent.isValid() || !secondary.isValid() ||
      !buttons.isValid() || !ok || contrast < 80 || contrast > 140) {
    if (error != nullptr) *error = QCoreApplication::translate("Theme", "cor ou contraste inválido");
    return false;
  }

  Theme theme = makeCustom(background, accent, secondary, buttons, contrast);
  theme.id = id;
  theme.name = name;
  if (out != nullptr) *out = theme;
  return true;
}

QVector<Theme> loadSavedThemes() {
  QVector<Theme> themes;
  QDir dir(themesDir());
  if (!dir.exists()) return themes;
  const QStringList files = dir.entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    QFile handle(dir.filePath(file));
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    QTextStream in(&handle);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
      Theme theme;
      if (themeFromCsvRow(in.readLine(), &theme, nullptr)) {
        // O arquivo manda no id: assim renomear o arquivo renomeia o tema.
        theme.id = QFileInfo(file).completeBaseName();
        themes.append(theme);
      }
      break;  // um tema por arquivo
    }
  }
  return themes;
}

bool saveThemeFile(const Theme& theme, QString* error) {
  QDir().mkpath(themesDir());
  QFileInfo info(themesDir(), theme.id + QStringLiteral(".csv"));
  QSaveFile file(info.filePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) *error = file.errorString();
    return false;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  out << themeCsvHeader() << '\n' << themeToCsvRow(theme) << '\n';
  return file.commit();
}

bool removeThemeFile(const QString& id, QString* error) {
  QFile file(QDir(themesDir()).filePath(id + QStringLiteral(".csv")));
  if (file.remove()) return true;
  if (error != nullptr && file.exists()) *error = file.errorString();
  return !file.exists();
}

int importThemesCsv(const QString& filePath, QStringList* warnings, QString* error) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error != nullptr) *error = file.errorString();
    return -1;
  }
  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);

  QStringList lines;
  while (!in.atEnd()) lines.append(in.readLine());

  int rooms = kMaxSavedThemes - loadSavedThemes().size();
  if (rooms <= 0) {
    if (error != nullptr) *error = QCoreApplication::translate("Theme", "limite de %1 temas atingido").arg(kMaxSavedThemes);
    return -1;
  }

  int imported = 0;
  int lineNumber = 0;
  for (const QString& line : lines) {
    ++lineNumber;
    if (lineNumber == 1 && line.startsWith(QStringLiteral("id,"))) continue;  // cabeçalho
    if (imported >= rooms) {
      if (warnings != nullptr) {
        *warnings << QCoreApplication::translate("Theme", "linha %1: limite de %2 temas").arg(lineNumber).arg(kMaxSavedThemes);
      }
      break;
    }
    QString reason;
    Theme theme;
    if (!themeFromCsvRow(line, &theme, &reason)) {
      if (warnings != nullptr && !line.trimmed().isEmpty() && !line.startsWith(QLatin1Char('#'))) {
        *warnings << QCoreApplication::translate("Theme", "linha %1: %2").arg(lineNumber).arg(reason);
      }
      continue;
    }
    if (saveThemeFile(theme, nullptr)) ++imported;
  }
  return imported;
}

bool exportThemesCsv(const QString& filePath, QString* error) {
  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) *error = file.errorString();
    return false;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  out << themeCsvHeader() << '\n';
  for (const Theme& theme : loadSavedThemes()) out << themeToCsvRow(theme) << '\n';
  return file.commit();
}

}  // namespace wamp::ui
