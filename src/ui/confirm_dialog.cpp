// Copyright (c) 2026 Cassio Fiorin. GPL-3.0-or-later; ver COPYING.
#include "confirm_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace wamp::ui {
namespace {

/// Uma camada: título, texto e dois botões. `defaultButton` vazio = sem padrão.
bool ask(QWidget* parent, const QString& title, const QString& text, const QString& acceptLabel,
         const QSize& size, bool acceptIsDefault, bool delayAccept, bool destructive) {
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setModal(true);
  dialog.resize(size);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(18, 16, 18, 14);
  layout->setSpacing(14);

  auto* label = new QLabel(text, &dialog);
  label->setWordWrap(true);
  label->setFocusPolicy(Qt::StrongFocus);  // o foco começa no texto, nunca no botão
  layout->addWidget(label, 1);
  label->setFocus();

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* accept = buttons->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
  auto* cancel = buttons->addButton(QObject::tr("Cancelar"), QDialogButtonBox::RejectRole);
  accept->setAutoDefault(acceptIsDefault);
  accept->setDefault(acceptIsDefault);
  cancel->setAutoDefault(acceptIsDefault);
  cancel->setDefault(acceptIsDefault);
  if (destructive) accept->setObjectName(QStringLiteral("destructive"));
  layout->addWidget(buttons);

  if (delayAccept) {
    accept->setEnabled(false);
    QTimer::singleShot(700, accept, [accept] { accept->setEnabled(true); });
  }

  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
  return dialog.exec() == QDialog::Accepted;
}

}  // namespace

bool confirmDestructive(QWidget* parent, const QString& title, const QString& question,
                        const QString& permanence, const QString& actionLabel) {
  const QSize base(420, 170);
  if (!ask(parent, title, question, QObject::tr("Continuar"), base, true, false, false)) return false;
  if (!ask(parent, title, permanence, QObject::tr("Sim, é permanente"), base * 1.5, false, false,
           true)) {
    return false;
  }
  return ask(parent, title, QObject::tr("Podemos prosseguir?"), actionLabel, base * 1.25, false,
             true, true);
}

}  // namespace wamp::ui
