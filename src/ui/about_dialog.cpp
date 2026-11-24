#include "about_dialog.h"

#include <QDate>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include "help_dialog.h"
#include "version.h"

namespace wamp::ui {
namespace {

/// Data de compilação, em AAAA-MM-DD. `__DATE__` vem como "Nov 24 2026"; datas em inglês
/// de três letras são estáveis o bastante para virar QDate com o locale fixo.
QString buildDate() {
  const QDate parsed = QLocale::c().toDate(QString::fromLatin1(__DATE__), QStringLiteral("MMM d yyyy"));
  return parsed.isValid() ? parsed.toString(Qt::ISODate) : QString::fromLatin1(__DATE__);
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Sobre o Amplia"));
  setModal(true);

  auto* title = new QLabel(tr("Amplia"), this);
  title->setObjectName(QStringLiteral("dynamicsTitle"));
  // O Amplia não é um Winamp para Linux: é um player novo que bebe na fonte do Winamp.
  auto* subtitle = new QLabel(tr("Um player novo, com a alma do Winamp."), this);
  subtitle->setObjectName(QStringLiteral("hint"));

  auto* details = new QFormLayout();
  const QString version = QStringLiteral(AMPLIA_RELEASE).isEmpty()
                              ? QString::fromLatin1(AMPLIA_VERSION)
                              : QStringLiteral(AMPLIA_VERSION "-" AMPLIA_RELEASE);
  details->addRow(tr("Versão:"), new QLabel(version, this));
  details->addRow(tr("Commit:"), new QLabel(QString::fromLatin1(AMPLIA_GIT), this));
  details->addRow(tr("Build:"), new QLabel(buildDate(), this));

  // Créditos: o nome vem do arquivo de tradutores, então quem manda um PR de tradução
  // aparece aqui sem tocar em código. Sem nome, a linha convida a mandar.
  auto* translators = new QLabel(tr("Tradução: %1").arg(translationCredits()), this);
  translators->setObjectName(QStringLiteral("hint"));
  translators->setWordWrap(true);

  auto* link = new QLabel(
      tr("Lançamentos: <a href=\"https://github.com/cassiofiorin/Amplia/releases\">"
         "github.com/cassiofiorin/Amplia/releases</a>"),
      this);
  link->setOpenExternalLinks(true);
  link->setTextFormat(Qt::RichText);

  auto* footer = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
  // O texto padrão do "Fechar" vem do tema do desktop, que pode estar em outro idioma que
  // não o escolhido no app: fixar em tr() mantém a janela coerente.
  if (QPushButton* close = footer->button(QDialogButtonBox::Close)) close->setText(tr("Fechar"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 16);
  layout->setSpacing(10);
  layout->addWidget(title);
  layout->addWidget(subtitle);
  layout->addSpacing(6);
  layout->addLayout(details);
  layout->addWidget(translators);
  layout->addWidget(link);
  layout->addSpacing(6);
  layout->addWidget(footer);
}

}  // namespace wamp::ui
