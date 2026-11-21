#include "translator.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

namespace wamp::ui {
namespace {

/// Mesmo arquivo que a janela usa. Fica aqui porque o idioma precisa ser conhecido antes
/// de a janela existir — e não dá para pedir a INI emprestada a ela.
QString settingsFile() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                      QStringLiteral("/amplia");
  return dir + QStringLiteral("/amplia.ini");
}

/// Tradutor instalado. Precisa sobreviver ao main: se sair de escopo, o Qt volta ao
/// idioma-fonte.
QTranslator* installed = nullptr;

/// Código efetivo do idioma. "sistema" vira o locale do SO; português é o idioma-fonte,
/// então não carrega arquivo nenhum.
QString effectiveCode(const QString& requested) {
  QString id = requested.trimmed();
  if (id.isEmpty()) id = QStringLiteral("sistema");
  if (id == QLatin1String("sistema")) {
    const QLocale system = QLocale::system();
    if (system.language() == QLocale::Portuguese) return QString();
    return QLocale::languageToCode(system.language());
  }
  if (id.startsWith(QLatin1String("pt"))) return QString();
  return id;
}

}  // namespace

QString settingsFileForTests() { return settingsFile(); }

QString languageFromSettings() {
  QSettings settings(settingsFile(), QSettings::IniFormat);
  const QString value = settings.value(QStringLiteral("general/language")).toString();
  return value.isEmpty() ? QStringLiteral("sistema") : value;
}

void installTranslators(const QString& requested) {
  // Escape de depuração: permite testar um idioma sem mexer na INI.
  const QString requestedId =
      qEnvironmentVariableIsSet("AMPLIA_LANG") ? qEnvironmentVariable("AMPLIA_LANG") : requested;

  if (installed != nullptr) {
    QCoreApplication::removeTranslator(installed);
    delete installed;
    installed = nullptr;
  }

  const QString code = effectiveCode(requestedId);
  if (code.isEmpty()) return;  // idioma-fonte: as strings já estão em português

  auto* translator = new QTranslator;
  const QString embedded = QStringLiteral(":/i18n/amplia_%1.qm").arg(code);
  const QString dropIn = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/i18n/amplia_%1.qm").arg(code);
  // O arquivo avulso vem primeiro: é o caminho de quem traduziu sem recompilar.
  for (const QString& path : {dropIn, embedded}) {
    if (translator->load(path)) {
      QCoreApplication::installTranslator(translator);
      installed = translator;
      return;
    }
  }
  delete translator;
}

QStringList availableLanguages() {
  return {QStringLiteral("sistema"), QStringLiteral("pt_BR"), QStringLiteral("en")};
}

QString languageLabel(const QString& id) {
  if (id == QLatin1String("sistema")) return QCoreApplication::translate("Translator", "Sistema");
  if (id == QLatin1String("pt_BR")) return QStringLiteral("Português (Brasil)");
  if (id == QLatin1String("en")) return QStringLiteral("English");
  return id;
}

}  // namespace wamp::ui
