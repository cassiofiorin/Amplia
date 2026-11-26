#include "cover_cache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QStandardPaths>
#include <QThreadPool>

#include <vector>

#include "wamp/tag_editor.h"
#include "wamp/work_key.h"

namespace wamp::ui {
namespace {

QString suffixForMime(const std::string& mime) {
  if (mime.find("png") != std::string::npos) return QStringLiteral("png");
  if (mime.find("webp") != std::string::npos) return QStringLiteral("webp");
  if (mime.find("gif") != std::string::npos) return QStringLiteral("gif");
  return QStringLiteral("jpg");
}

/// Acha a imagem do álbum: embutida primeiro, capa da pasta depois. Roda fora da interface.
QImage loadCoverImage(const QString& path, QString* diskSuffix) {
  std::vector<wamp::TagPicture> pictures;
  std::string error;
  if (wamp::TagEditor::read(path.toStdString(), nullptr, nullptr, &pictures, &error) &&
      !pictures.empty() && !pictures.front().data.empty()) {
    const wamp::TagPicture& picture = pictures.front();
    QImage image;
    image.loadFromData(reinterpret_cast<const uchar*>(picture.data.data()),
                       static_cast<int>(picture.data.size()));
    if (!image.isNull()) {
      if (diskSuffix != nullptr) *diskSuffix = suffixForMime(picture.mime);
      return image;
    }
  }

  const QString folder = QString::fromStdString(wamp::TagEditor::folderCover(path.toStdString()));
  if (folder.isEmpty()) return QImage();
  QImage image;
  image.load(folder);
  if (diskSuffix != nullptr) {
    const QString suffix = QFileInfo(folder).suffix().toLower();
    *diskSuffix = suffix.isEmpty() ? QStringLiteral("jpg") : suffix;
  }
  return image;
}

}  // namespace

CoverCache::CoverCache(QObject* parent) : QObject(parent) {
  directory_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
               QStringLiteral("/covers");
  QDir().mkpath(directory_);
}

QString CoverCache::albumKey(const QString& artist, const QString& album) {
  // Reaproveita a identidade da obra: o mesmo álbum casa mesmo se a tag alternar caixa ou
  // acento entre as faixas.
  const std::string key =
      wamp::workKey(artist.toStdString(), album.toStdString(), std::string("album"));
  return QString::fromStdString(key);
}

QString CoverCache::filePath(const QString& albumKey, int size) const {
  return files_.value(albumKey + QLatin1Char('|') + QString::number(size));
}

QPixmap CoverCache::cover(const QString& albumKey, const QString& samplePath, int size) {
  const QString key = albumKey + QLatin1Char('|') + QString::number(size);
  const auto found = memory_.constFind(key);
  if (found != memory_.constEnd()) return found.value();

  // Já foi extraído em outra sessão: o arquivo do cache serve direto.
  const QString stored = files_.value(key);
  if (!stored.isEmpty()) {
    QPixmap pixmap;
    if (pixmap.load(stored)) {
      store(key, size, pixmap);
      return pixmap;
    }
    files_.remove(key);
  }

  enqueue(key, samplePath, size);
  return QPixmap();
}

void CoverCache::enqueue(const QString& key, const QString& samplePath, int size) {
  if (pending_.contains(key)) return;
  pending_.insert(key);

  const QString albumPart = key.section(QLatin1Char('|'), 0, 0);
  const QString directory = directory_;
  QPointer<CoverCache> self(this);

  // O arquivo de cache leva o tamanho no nome: a lista quer 18 px, o painel quer 96 px.
  const QString target =
      directory + QLatin1Char('/') + albumPart + QLatin1Char('-') + QString::number(size);

  QRunnable* task = QRunnable::create([self, key, samplePath, size, target]() {
    QString suffix = QStringLiteral("jpg");
    const QImage image = loadCoverImage(samplePath, &suffix);
    if (!image.isNull()) {
      const QImage scaled = image.scaled(size, size, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
      const QString file = target + QLatin1Char('.') + suffix;
      scaled.save(file);
      if (!self) return;
      // A interface é a única dona dos pixmaps: a conversão volta para a thread dela.
      QMetaObject::invokeMethod(self, [self, key, size, file] {
        if (!self) return;
        QPixmap pixmap;
        if (pixmap.load(file)) {
          self->files_.insert(key, file);
          self->store(key, size, pixmap);
        } else {
          self->pending_.remove(key);
        }
        emit self->loaded();
      }, Qt::QueuedConnection);
      return;
    }

    if (!self) return;
    QMetaObject::invokeMethod(self, [self, key] {
      if (!self) return;
      self->pending_.remove(key);
      // Sem capa: avisa mesmo assim, para a célula parar de mostrar o marcador.
      emit self->loaded();
    }, Qt::QueuedConnection);
  });
  QThreadPool::globalInstance()->start(task);
}

void CoverCache::store(const QString& key, int size, const QPixmap& pixmap) {
  if (memory_.size() >= kMemoryLimit) memory_.clear();
  memory_.insert(key, pixmap);
  pending_.remove(key);
  Q_UNUSED(size);
}

}  // namespace wamp::ui
