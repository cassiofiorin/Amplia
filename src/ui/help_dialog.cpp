#include "help_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QHBoxLayout>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "translator.h"

namespace wamp::ui {
namespace {

/// Sufixo do idioma ativo. Só existem dois arquivos hoje; idioma sem ajuda cai no inglês,
/// que é a aposta razoável para quem não lê português.
QString helpSuffix() {
  const QString code = activeLanguageCode();
  if (code.isEmpty() || code.startsWith(QLatin1String("pt"))) return QStringLiteral("pt");
  return QStringLiteral("en");
}

QString readResource(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  return QString::fromUtf8(file.readAll());
}

}  // namespace

QString helpText() {
  const QString path = QStringLiteral(":/help/ajuda-%1.md").arg(helpSuffix());
  const QString text = readResource(path);
  if (!text.isEmpty()) return text;
  // Recurso ausente é erro de empacotamento, não de tradução: avisa em vez de mostrar vazio.
  return QStringLiteral("# Ajuda\n\nO texto da ajuda não foi encontrado no pacote (%1).").arg(path);
}

QString translationCredits() {
  const QString text = readResource(QStringLiteral(":/help/idiomas.txt"));
  // pt_BR vira "PT-BR", en vira "EN": o rótulo é o que o usuário reconhece, não o código.
  const auto label = [](const QString& code) {
    const QString upper = code.toUpper();
    return upper == QLatin1String("PT_BR") || upper == QLatin1String("PT") ? QStringLiteral("PT-BR")
                                                                          : upper;
  };
  QStringList parts;
  for (const QString& line : text.split('\n')) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
    const QStringList fields = trimmed.split('|');
    if (fields.size() < 2) continue;
    QString entry = label(fields[0].trimmed());
    const bool native = fields.size() > 2 && fields[2].trimmed().compare(QStringLiteral("nativo"),
                                                                        Qt::CaseInsensitive) == 0;
    if (native) {
      entry += QLatin1Char(' ') +
               QCoreApplication::translate("wamp::ui::HelpDialog", "(Nativo)");
    }
    const QString name = fields[1].trimmed();
    entry += QLatin1Char(' ') +
             (name == QStringLiteral("—")
                  ? QCoreApplication::translate("wamp::ui::HelpDialog", "aberta — mande a sua num PR")
                  : name);
    parts << entry;
  }
  return parts.join(QStringLiteral("; "));
}

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Ajuda do Amplia"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  resize(820, 660);

  view_ = new QTextBrowser(this);
  view_->setOpenExternalLinks(true);
  view_->setMarkdown(helpText());

  auto* footer = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
  // O texto padrão do "Fechar" vem do tema do desktop, que pode estar em outro idioma que
  // não o escolhido no app: fixar em tr() mantém a janela coerente.
  if (QPushButton* close = footer->button(QDialogButtonBox::Close)) close->setText(tr("Fechar"));
  // O texto é a razão da janela: os botões ficam discretos, encostados à direita.
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);
  layout->addWidget(view_, 1);
  layout->addWidget(footer);
}

}  // namespace wamp::ui
