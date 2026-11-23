#pragma once

#include <QStyledItemDelegate>

/// Role com a posição da faixa na fila (1 = próxima a tocar, 0 = fora da fila).
inline constexpr int kQueueRole = Qt::UserRole + 1;

/// Role com a duração já formatada ("4:11"), desenhada em coluna própria à direita.
inline constexpr int kDurationRole = Qt::UserRole + 2;

/// Role com 1 quando o arquivo não abriu ("missing file"): o rótulo sai em âmbar.
inline constexpr int kMissingRole = Qt::UserRole + 4;

/// Role com a origem do item na fila ("PLAYLIST", "BIBLIOTECA", "RÁDIO"). O bloco da
/// fila usa o mesmo delegate da playlist e mostra a origem antes do tempo.
inline constexpr int kOriginRole = Qt::UserRole + 5;

/// Desenha a linha da playlist em colunas: número da faixa num gutter à esquerda,
/// título no meio (elidido), duração alinhada à direita e, colado na borda, o número
/// da posição na fila quando a faixa está enfileirada.
///
/// É um delegate em vez de texto formatado porque alinhar à direita com espaços depende
/// da fonte e quebra assim que a lista passa de 9 ou 99 itens.
class PlaylistDelegate final : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit PlaylistDelegate(QObject* parent = nullptr);

  /// Quantidade de faixas; define a largura do gutter (quantos dígitos cabem).
  void setTrackCount(int count);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

 private:
  int count_ = 0;
};
