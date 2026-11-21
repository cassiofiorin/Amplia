#include "library_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QFontMetrics>

#include "playlist_delegate.h"
#include "theme.h"

namespace wamp::ui {

namespace {

QString rowText(const wamp::TrackInfo& info) {
  const QString title = info.title.empty() ? QString::fromStdString(info.path).section('/', -1)
                                           : QString::fromStdString(info.title);
  if (info.artist.empty()) return title;
  return QString::fromStdString(info.artist) + QStringLiteral(" - ") + title;
}

QString durationText(const wamp::TrackInfo& info) {
  if (info.durationMs <= 0) return QString();
  const qint64 seconds = info.durationMs / 1000;
  return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

LibraryWindow::LibraryWindow(QWidget* parent) : QWidget(parent, Qt::Tool) {
  setWindowTitle(tr("Biblioteca — Amplia"));
  resize(560, 480);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  emptyLabel_ = new QLabel(tr("Nenhuma pasta na biblioteca. Use “Adicionar pasta…” para indexar "
                              "a sua coleção."),
                           this);
  emptyLabel_->setObjectName(QStringLiteral("hint"));
  emptyLabel_->setWordWrap(true);
  emptyLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  layout->addWidget(emptyLabel_);

  list_ = new QListWidget(this);
  list_->setItemDelegate(new PlaylistDelegate(list_));
  list_->setUniformItemSizes(true);
  list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layout->addWidget(list_, 1);

  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName(QStringLiteral("hint"));
  statusLabel_->hide();
  layout->addWidget(statusLabel_);

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(6);
  addButton_ = new QPushButton(tr("Adicionar pasta…"), this);
  auto* refreshButton = new QPushButton(tr("Atualizar"), this);
  auto* closeButton = new QPushButton(tr("Fechar"), this);
  buttons->addWidget(addButton_);
  buttons->addWidget(refreshButton);
  buttons->addStretch(1);
  buttons->addWidget(closeButton);
  layout->addLayout(buttons);

  connect(addButton_, &QPushButton::clicked, this, &LibraryWindow::addFolderRequested);
  connect(refreshButton, &QPushButton::clicked, this, &LibraryWindow::refreshRequested);
  connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    if (item == nullptr) return;
    emit playRequested(item->data(Qt::UserRole).toString());
  });
  connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    if (item == nullptr) return;
    emit playRequested(item->data(Qt::UserRole).toString());
  });
}

void LibraryWindow::setTracks(const std::vector<wamp::TrackInfo>& tracks) {
  const QString selected = list_->currentItem() != nullptr
                               ? list_->currentItem()->data(Qt::UserRole).toString()
                               : QString();

  list_->clear();
  auto* delegate = qobject_cast<PlaylistDelegate*>(list_->itemDelegate());
  if (delegate != nullptr) delegate->setTrackCount(static_cast<int>(tracks.size()));

  for (const auto& info : tracks) {
    auto* item = new QListWidgetItem(rowText(info), list_);
    item->setData(Qt::UserRole, QString::fromStdString(info.path));
    item->setData(kDurationRole, durationText(info));
    item->setToolTip(QString::fromStdString(info.path));
  }

  emptyLabel_->setVisible(tracks.empty());
  list_->setVisible(!tracks.empty());

  if (!selected.isEmpty()) {
    const auto found = list_->findItems(QString(), Qt::MatchExactly);
    Q_UNUSED(found);
    for (int i = 0; i < list_->count(); ++i) {
      if (list_->item(i)->data(Qt::UserRole).toString() == selected) {
        list_->setCurrentRow(i);
        break;
      }
    }
  }
}

void LibraryWindow::updateTrack(const wamp::TrackInfo& info) {
  const QString path = QString::fromStdString(info.path);
  for (int i = 0; i < list_->count(); ++i) {
    QListWidgetItem* item = list_->item(i);
    if (item->data(Qt::UserRole).toString() != path) continue;
    item->setText(rowText(info));
    item->setData(kDurationRole, durationText(info));
    return;
  }
}

void LibraryWindow::setStatus(const QString& text) {
  statusLabel_->setText(text);
  statusLabel_->setVisible(!text.isEmpty());
}

}  // namespace wamp::ui
