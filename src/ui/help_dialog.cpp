#include "help_dialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QHBoxLayout>
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

QString translatorCredit() {
  const QString text = readResource(QStringLiteral(":/help/tradutores.txt"));
  const QString suffix = helpSuffix();
  for (const QString& line : text.split('\n')) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
    const QStringList parts = trimmed.split('|');
    if (parts.size() < 2) continue;
    if (parts[0].trimmed() != suffix) continue;
    // "—" é o marcador de "ainda sem nome"; sai vazio para o Sobre convidar o PR.
    const QString name = parts[1].trimmed();
    return name == QStringLiteral("—") ? QString() : name;
  }
  return QString();
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
