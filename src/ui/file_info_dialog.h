#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "wamp/tag_editor.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace wamp::ui {

/// Janela "File Info" (Alt+3): mostra e edita as tags de uma faixa, com as abas Basic Info
/// e Artwork, no desenho do Winamp.
///
/// A gravação só acontece quando a pessoa confirma (OK); a conferência pós-gravação e a
/// gravação segura ficam no `TagEditor`.
class FileInfoDialog final : public QDialog {
  Q_OBJECT

 public:
  FileInfoDialog(std::vector<std::string> paths, std::size_t index, bool safeSave,
                 QWidget* parent = nullptr);

  /// Preferência de gravação segura, para a janela principal guardar.
  bool safeSaveEnabled() const;

  /// Falso quando o arquivo (ou a pasta, na gravação segura) não aceita escrita.
  bool savingAllowed() const;

 signals:
  /// Emitido quando um arquivo foi gravado com sucesso (a lista precisa se atualizar).
  void fileSaved(const QString& path);

 private:
  void loadCurrent();
  void updateSaveability();
  bool saveCurrent();
  bool confirmDiscard();
  void markDirty();
  void updateFormat();
  void updatePictures();
  void goTo(int delta);
  void choosePicture();
  void savePictureAs();
  void removePicture();

  std::vector<std::string> paths_;
  std::size_t index_ = 0;
  bool dirty_ = false;
  bool loading_ = false;

  wamp::TagFields fields_;
  wamp::TagFormat format_;
  std::vector<wamp::TagPicture> pictures_;
  int pictureIndex_ = 0;

  // Imagem escolhida nesta sessão: guardada até o OK.
  bool pictureChanged_ = false;
  bool pictureRemoved_ = false;
  wamp::TagPicture newPicture_;

  QLabel* pathLabel_ = nullptr;
  QLineEdit* trackEdit_ = nullptr;
  QLineEdit* discEdit_ = nullptr;
  QLineEdit* bpmEdit_ = nullptr;
  QLineEdit* titleEdit_ = nullptr;
  QLineEdit* artistEdit_ = nullptr;
  QLineEdit* albumEdit_ = nullptr;
  QLineEdit* albumArtistEdit_ = nullptr;
  QLineEdit* yearEdit_ = nullptr;
  QLineEdit* genreEdit_ = nullptr;
  QLineEdit* commentEdit_ = nullptr;
  QLineEdit* composerEdit_ = nullptr;
  QLineEdit* publisherEdit_ = nullptr;
  QLabel* formatLabel_ = nullptr;
  QLabel* readonlyLabel_ = nullptr;
  QPushButton* okButton_ = nullptr;
  QLabel* replayGainLabel_ = nullptr;
  QLabel* warningLabel_ = nullptr;
  QCheckBox* safeSaveBox_ = nullptr;

  QListWidget* pictureList_ = nullptr;
  QLabel* picturePreview_ = nullptr;
  QLabel* pictureInfo_ = nullptr;
};

}  // namespace wamp::ui
