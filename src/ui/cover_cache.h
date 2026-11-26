// Capa de álbum em cache, por álbum e não por arquivo.
//
// A capa vem do próprio acervo: imagem embutida no arquivo (ID3v2 APIC, FLAC picture, MP4 covr)
// ou `folder.jpg`/`cover.jpg` ao lado das faixas. Não há rede aqui, de propósito — buscar capa na
// internet é outra fase.
//
// O cache é por **álbum**: as doze faixas de um disco compartilham uma leitura só. A extração
// acontece fora da thread da interface, porque abrir vinte arquivos enquanto a lista rola é o
// tipo de coisa que aparece como engasgo.
#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

namespace wamp::ui {

class CoverCache final : public QObject {
  Q_OBJECT

 public:
  explicit CoverCache(QObject* parent = nullptr);

  /// Chave estável do álbum (usa `norm/v1`, a mesma identidade do dataset compartilhado).
  static QString albumKey(const QString& artist, const QString& album);

  /// Devolve a capa se já estiver pronta; se não, agenda a carga e devolve nulo.
  /// Quem chama pinta um marcador e escuta `loaded()`.
  QPixmap cover(const QString& albumKey, const QString& samplePath, int size);

  /// Caminho no cache de disco, ou vazio quando o álbum não tem capa. Só depois do `loaded`.
  QString filePath(const QString& albumKey, int size) const;

  /// Limite de entradas em memória (pixmaps) — o suficiente para uma tela grande e um pouco.
  static constexpr int kMemoryLimit = 400;

 signals:
  void loaded();

 private:
  void enqueue(const QString& key, const QString& samplePath, int size);
  void store(const QString& key, int size, const QPixmap& pixmap);

  QHash<QString, QPixmap> memory_;
  QHash<QString, QString> files_;
  QSet<QString> pending_;
  QString directory_;
};

}  // namespace wamp::ui
