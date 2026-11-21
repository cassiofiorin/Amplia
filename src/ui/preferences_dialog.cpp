#include "preferences_dialog.h"

#include "translator.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QtGlobal>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QUrl>
#include <QGroupBox>
#include <QVBoxLayout>

#include "wamp/scrobbler.h"

PreferencesDialog::PreferencesDialog(bool shuffle, int level, bool closeToTray,
                                     bool restartOnPlay, bool visualEnabled, int visualFps,
                                     double costP95Ms, double cpuPercent,
                                     const QString& playlistMode, bool xwaylandPosition,
                                     const wamp::ScrobbleConfig& scrobble, const QString& language,
                                     QWidget* parent)
    : QDialog(parent), net_(std::make_unique<wamp::Scrobbler>()) {
  // O consumo mostrado é derivado do fps atual (custo por quadro × taxa), não o número
  // medido no dia da calibração — por isso o parâmetro não é guardado.
  Q_UNUSED(cpuPercent);
  setWindowTitle(tr("Preferências"));
  setModal(true);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 12, 14, 12);
  layout->setSpacing(8);

  auto* tabs = new QTabWidget(this);
  layout->addWidget(tabs, 1);

  // --- Reprodução ---------------------------------------------------------
  auto* playback = new QWidget(tabs);
  auto* playbackLayout = new QVBoxLayout(playback);
  playbackLayout->setContentsMargins(10, 10, 10, 10);
  playbackLayout->setSpacing(8);

  // Sorteio e o nível dele formam um bloco só: o slider não é uma opção independente, é
  // consequência da caixa. Uma moldura separa o conjunto dos dois itens soltos acima, e o
  // bloco fica por último — depois de "fechar para a bandeja" e "play volta ao início".
  auto* shuffleGroup = new QGroupBox(tr("Sorteio"), playback);
  auto* shuffleLayout = new QVBoxLayout(shuffleGroup);
  shuffleLayout->setContentsMargins(12, 6, 12, 8);
  shuffleLayout->setSpacing(4);
  shuffleBox_ = new QCheckBox(tr("Ativar sorteio"), shuffleGroup);
  shuffleBox_->setChecked(shuffle);
  shuffleLayout->addWidget(shuffleBox_);

  trayBox_ = new QCheckBox(tr("Fechar para a bandeja (agente)"), playback);
  trayBox_->setChecked(closeToTray);
  trayBox_->setToolTip(
      tr("Ao fechar a janela o Amplia continua tocando e volta pelo ícone da bandeja."));
  playbackLayout->addWidget(trayBox_);

  restartBox_ = new QCheckBox(
      tr("O botão Reproduzir recomeça a faixa do zero"), playback);
  restartBox_->setChecked(restartOnPlay);
  restartBox_->setToolTip(
      QStringLiteral("Clicar em reproduzir enquanto a faixa toca recomeça do zero, "
                     "como o play do player original."));
  playbackLayout->addWidget(restartBox_);
  playbackLayout->addWidget(shuffleGroup);

  levelLabel_ = new QLabel(shuffleGroup);
  shuffleLayout->addWidget(levelLabel_);

  levelSlider_ = new QSlider(Qt::Horizontal, shuffleGroup);
  levelSlider_->setRange(0, 100);
  levelSlider_->setValue(level);
  levelSlider_->setTickPosition(QSlider::TicksBelow);
  levelSlider_->setTickInterval(25);
  shuffleLayout->addWidget(levelSlider_);

  hintLabel_ = new QLabel(
      QStringLiteral("0% toca na ordem da lista; 100% sorteia por toda a playlist. "
                     "Valores no meio sorteiam dentro de um trecho à frente, que é como "
                     "mistura a ordem da lista com o sorteio."),
      shuffleGroup);
  hintLabel_->setWordWrap(true);
  hintLabel_->setEnabled(false);
  shuffleLayout->addWidget(hintLabel_);
  playbackLayout->addStretch(1);
  auto* general = new QWidget(tabs);
  auto* generalLayout = new QVBoxLayout(general);
  generalLayout->setContentsMargins(12, 12, 12, 12);
  generalLayout->setSpacing(8);

  auto* languageRow = new QHBoxLayout();
  languageRow->addWidget(new QLabel(tr("Idioma:"), general));
  languageBox_ = new QComboBox(general);
  for (const QString& id : wamp::ui::availableLanguages()) {
    languageBox_->addItem(wamp::ui::languageLabel(id), id);
  }
  const int current = languageBox_->findData(language);
  languageBox_->setCurrentIndex(current < 0 ? 0 : current);
  languageRow->addWidget(languageBox_, 1);
  generalLayout->addLayout(languageRow);

  auto* languageHint = new QLabel(
      tr("O idioma vale para a interface; o display mantém os termos do hardware "
         "(KBPS, KHZ, STEREO). A troca vale na próxima abertura do Amplia."),
      general);
  languageHint->setWordWrap(true);
  languageHint->setObjectName(QStringLiteral("hint"));
  generalLayout->addWidget(languageHint);
  {
    // Onde a lista mora. No Wayland a janela própria ganha uma segunda entrada na barra de
    // tarefas (o protocolo não sabe marcar subordinação em toplevel não modal), por isso o
    // padrão é a integrada; a janela própria fica pronta para o porte de Windows.
    auto* modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel(tr("Playlist:"), general));
    playlistModeBox_ = new QComboBox(general);
    playlistModeBox_->addItem(tr("Janela integrada"), QStringLiteral("integrada"));
    playlistModeBox_->addItem(tr("Janela externa"), QStringLiteral("externa"));
    const int index = playlistModeBox_->findData(playlistMode);
    playlistModeBox_->setCurrentIndex(index >= 0 ? index : 0);
    modeRow->addWidget(playlistModeBox_, 1);
    generalLayout->addLayout(modeRow);

    auto* modeHint = new QLabel(
        tr("Janela integrada mantém uma janela só: a lista entra embaixo do player e o botão "
           "PL cresce a janela. Janela externa separa a lista num painel próprio, que pode "
           "ser movido para outro canto da tela."),
        general);
    modeHint->setWordWrap(true);
    modeHint->setObjectName(QStringLiteral("hint"));
    generalLayout->addWidget(modeHint);

    // Posição de janela é o único ponto em que o Wayland nativo limita de verdade: o
    // protocolo não deixa o cliente dizer onde a janela fica. XWayland deixa.
    xwaylandBox_ = new QCheckBox(tr("Guardar a posição das janelas (usa XWayland)"), general);
    xwaylandBox_->setChecked(xwaylandPosition);
    generalLayout->addWidget(xwaylandBox_);
    auto* xwaylandHint = new QLabel(
        tr("O compositor Wayland decide onde a janela abre e não aceita posição do programa. "
           "Com XWayland o Amplia guarda e devolve a posição da playlist e da Biblioteca. "
           "Vale na próxima abertura; em troca, as janelas deixam de ser Wayland nativas."),
        general);
    xwaylandHint->setWordWrap(true);
    xwaylandHint->setObjectName(QStringLiteral("hint"));
    generalLayout->addWidget(xwaylandHint);
  }

  generalLayout->addStretch(1);

  tabs->addTab(general, tr("Geral"));
  tabs->addTab(playback, tr("Reprodução"));

  // --- Scrobbling ---------------------------------------------------------
  auto* scrobbling = new QWidget(tabs);
  auto* scrobblingLayout = new QVBoxLayout(scrobbling);
  scrobblingLayout->setContentsMargins(10, 10, 10, 10);
  scrobblingLayout->setSpacing(8);

  nowPlayingBox_ = new QCheckBox(
      tr("Anunciar \"ouvindo agora\" quando a faixa começa"), scrobbling);
  nowPlayingBox_->setChecked(scrobble.announceNowPlaying);
  nowPlayingBox_->setToolTip(
      QStringLiteral("O Last.fm não tem como encerrar o anúncio: ele fica no perfil até outro "
                     "scrobble chegar."));
  scrobblingLayout->addWidget(nowPlayingBox_);

  auto* lastfmGroup = new QGroupBox(QStringLiteral("Last.fm"), scrobbling);
  auto* lastfmLayout = new QVBoxLayout(lastfmGroup);
  lastfmBox_ = new QCheckBox(tr("Enviar scrobbles para o Last.fm"), lastfmGroup);
  lastfmBox_->setChecked(scrobble.lastfmEnabled);
  lastfmLayout->addWidget(lastfmBox_);

  auto* lastfmForm = new QFormLayout();
  lastfmForm->setContentsMargins(0, 0, 0, 0);
  lastfmKey_ = new QLineEdit(QString::fromStdString(scrobble.lastfmApiKey), lastfmGroup);
  lastfmKey_->setPlaceholderText(tr("API key (last.fm/api/account/create)"));
  lastfmForm->addRow(tr("API key"), lastfmKey_);
  lastfmSecret_ = new QLineEdit(QString::fromStdString(scrobble.lastfmSharedSecret), lastfmGroup);
  lastfmSecret_->setEchoMode(QLineEdit::Password);
  lastfmSecret_->setPlaceholderText(tr("shared secret"));
  lastfmForm->addRow(QStringLiteral("Secret"), lastfmSecret_);
  lastfmLayout->addLayout(lastfmForm);

  lastfmButton_ = new QPushButton(tr("Conectar com o Last.fm…"), lastfmGroup);
  lastfmButton_->setToolTip(
      tr("Abre o navegador para autorizar; depois volte e confirme."));
  lastfmLayout->addWidget(lastfmButton_);

  lastfmStatus_ = new QLabel(lastfmGroup);
  lastfmStatus_->setWordWrap(true);
  lastfmStatus_->setEnabled(false);
  lastfmLayout->addWidget(lastfmStatus_);
  scrobblingLayout->addWidget(lastfmGroup);

  auto* listenbrainzGroup = new QGroupBox(QStringLiteral("ListenBrainz"), scrobbling);
  auto* listenbrainzLayout = new QVBoxLayout(listenbrainzGroup);
  listenbrainzBox_ =
      new QCheckBox(tr("Enviar escutas para o ListenBrainz"), listenbrainzGroup);
  listenbrainzBox_->setChecked(scrobble.listenbrainzEnabled);
  listenbrainzLayout->addWidget(listenbrainzBox_);

  auto* listenbrainzForm = new QFormLayout();
  listenbrainzForm->setContentsMargins(0, 0, 0, 0);
  listenbrainzToken_ =
      new QLineEdit(QString::fromStdString(scrobble.listenbrainzToken), listenbrainzGroup);
  listenbrainzToken_->setEchoMode(QLineEdit::Password);
  listenbrainzToken_->setPlaceholderText(tr("token do perfil em listenbrainz.org/settings"));
  listenbrainzForm->addRow(QStringLiteral("Token"), listenbrainzToken_);
  listenbrainzLayout->addLayout(listenbrainzForm);

  auto* testButton = new QPushButton(tr("Testar token"), listenbrainzGroup);
  listenbrainzLayout->addWidget(testButton);
  listenbrainzStatus_ = new QLabel(listenbrainzGroup);
  listenbrainzStatus_->setWordWrap(true);
  listenbrainzStatus_->setEnabled(false);
  listenbrainzLayout->addWidget(listenbrainzStatus_);
  scrobblingLayout->addWidget(listenbrainzGroup);
  scrobblingLayout->addStretch(1);
  tabs->addTab(scrobbling, QStringLiteral("Scrobbling"));

  // --- Visualização -------------------------------------------------------
  auto* visual = new QWidget(tabs);
  auto* visualLayout = new QVBoxLayout(visual);
  visualLayout->setContentsMargins(10, 10, 10, 10);
  visualLayout->setSpacing(8);

  visualBox_ = new QCheckBox(tr("Mostrar espectro e medidores"), visual);
  visualBox_->setChecked(visualEnabled);
  visualLayout->addWidget(visualBox_);

  auto* fpsRow = new QHBoxLayout();
  fpsRow->addWidget(new QLabel(tr("Quadros por segundo:"), visual));
  fpsSlider_ = new QSlider(Qt::Horizontal, visual);
  fpsSlider_->setRange(10, 60);
  fpsSlider_->setValue(std::clamp(visualFps, 10, 60));
  fpsSlider_->setTickPosition(QSlider::TicksBelow);
  fpsSlider_->setTickInterval(10);
  fpsRow->addWidget(fpsSlider_, 1);
  fpsLabel_ = new QLabel(tr("%1 fps").arg(fpsSlider_->value()), visual);
  fpsLabel_->setMinimumWidth(52);
  fpsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  fpsRow->addWidget(fpsLabel_);
  visualLayout->addLayout(fpsRow);

  auto* calibrateRow = new QHBoxLayout();
  auto* calibrateButton = new QPushButton(tr("Calibrar…"), visual);
  calibrateButton->setToolTip(
      tr("Mede ~10 s o custo de desenhar o painel e escolhe a taxa que sobra folga."));
  connect(calibrateButton, &QPushButton::clicked, this, &PreferencesDialog::calibrateRequested);
  calibrateRow->addWidget(calibrateButton);
  calibrationStatus_ = new QLabel(visual);
  calibrationStatus_->setEnabled(false);
  calibrateRow->addWidget(calibrationStatus_, 1);
  visualLayout->addLayout(calibrateRow);

  setCalibrationResult(fpsSlider_->value(), costP95Ms);

  auto* visualHint = new QLabel(
      QStringLiteral("A 15 fps o visualizador quase não custa CPU. A calibração mede quanto "
                     "tempo cada quadro leva para desenhar e escolhe a maior taxa que ainda "
                     "deixa 40% de folga — acima disso, o desenho começa a competir com o "
                     "áudio. O slider permite fixar um valor à mão a qualquer momento."),
      visual);
  visualHint->setWordWrap(true);
  visualHint->setEnabled(false);
  visualLayout->addWidget(visualHint);
  visualLayout->addStretch(1);
  tabs->addTab(visual, tr("Visualização"));

  connect(fpsSlider_, &QSlider::valueChanged, this, [this](int value) {
    fpsLabel_->setText(tr("%1 fps").arg(value));
    if (costP95Ms_ > 0.0) setCalibrationResult(value, costP95Ms_);
  });
  connect(visualBox_, &QCheckBox::toggled, this, [this, fpsRow, calibrateRow](bool on) {
    fpsSlider_->setEnabled(on);
    fpsLabel_->setEnabled(on);
    calibrationStatus_->setEnabled(on);
    Q_UNUSED(fpsRow);
    Q_UNUSED(calibrateRow);
  });
  fpsSlider_->setEnabled(visualEnabled);
  fpsLabel_->setEnabled(visualEnabled);
  calibrationStatus_->setEnabled(visualEnabled);

  // AMPLIA_PREFS_TAB=1 abre direto no Scrobbling e =2 no Visualização (conferir o desenho).
  const int startTab = qEnvironmentVariableIntValue("AMPLIA_PREFS_TAB");
  if (startTab > 0 && startTab < tabs->count()) tabs->setCurrentIndex(startTab);

  // A nota da regra de scrobble pertence à aba de scrobbling, não ao rodapé do diálogo.
  auto* note = new QLabel(
      QStringLiteral("Só entram faixas com mais de 30 segundos, depois de 50% ou 4 minutos "
                     "tocados — a mesma regra do Last.fm. O que falhar por rede fica numa "
                     "fila e é reenviado."),
      scrobbling);
  note->setWordWrap(true);
  note->setEnabled(false);
  scrobblingLayout->addWidget(note);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(levelSlider_, &QSlider::valueChanged, this, &PreferencesDialog::updateLevelLabel);
  connect(shuffleBox_, &QCheckBox::toggled, this, [this](bool on) {
    levelSlider_->setEnabled(on);
    levelLabel_->setEnabled(on);
    hintLabel_->setEnabled(on);
  });
  connect(lastfmBox_, &QCheckBox::toggled, this, [this] { updateScrobbleState(); });
  connect(listenbrainzBox_, &QCheckBox::toggled, this, [this] { updateScrobbleState(); });
  connect(lastfmButton_, &QPushButton::clicked, this, &PreferencesDialog::connectLastfm);
  connect(testButton, &QPushButton::clicked, this, &PreferencesDialog::testListenbrainz);

  lastfmSession_ = scrobble.lastfmSessionKey;
  if (!lastfmSession_.empty()) {
    lastfmStatus_->setText(tr("Conectado — session key já salva."));
    lastfmButton_->setText(tr("Reconectar com o Last.fm…"));
  }

  updateLevelLabel();
  updateScrobbleState();
  setMinimumWidth(460);
}

PreferencesDialog::~PreferencesDialog() = default;

bool PreferencesDialog::shuffleEnabled() const { return shuffleBox_->isChecked(); }

bool PreferencesDialog::restartOnPlay() const { return restartBox_->isChecked(); }

bool PreferencesDialog::closeToTray() const { return trayBox_ != nullptr && trayBox_->isChecked(); }

int PreferencesDialog::shuffleLevel() const { return levelSlider_->value(); }

bool PreferencesDialog::visualEnabled() const {
  return visualBox_ != nullptr && visualBox_->isChecked();
}

int PreferencesDialog::visualFps() const {
  return fpsSlider_ != nullptr ? fpsSlider_->value() : 15;
}

double PreferencesDialog::visualCostP95Ms() const { return costP95Ms_; }

double PreferencesDialog::visualCpuPercent() const {
  if (costP95Ms_ <= 0.0) return 0.0;
  return costP95Ms_ * visualFps() / 10.0;
}

void PreferencesDialog::setCalibrationResult(int fps, double costP95Ms) {
  if (calibrationStatus_ == nullptr) return;
  costP95Ms_ = costP95Ms;
  if (fpsSlider_ != nullptr) {
    QSignalBlocker blocker(fpsSlider_);
    fpsSlider_->setValue(std::clamp(fps, 10, 60));
  }
  if (fpsLabel_ != nullptr) {
    fpsLabel_->setText(tr("%1 fps").arg(visualFps()));
  }
  if (costP95Ms > 0.0) {
    calibrationStatus_->setText(
        tr("Medido: %1 ms por quadro, %2% de um núcleo a %3 fps")
            .arg(costP95Ms, 0, 'f', 2)
            .arg(visualCpuPercent(), 0, 'f', 1)
            .arg(visualFps()));
  } else {
    calibrationStatus_->setText(tr("Sem medição — padrão de 15 fps."));
  }
}

wamp::ScrobbleConfig PreferencesDialog::scrobbleConfig() const {
  wamp::ScrobbleConfig config;
  config.lastfmEnabled = lastfmBox_->isChecked();
  config.lastfmApiKey = lastfmKey_->text().trimmed().toStdString();
  config.lastfmSharedSecret = lastfmSecret_->text().trimmed().toStdString();
  config.lastfmSessionKey = lastfmSession_;
  config.listenbrainzEnabled = listenbrainzBox_->isChecked();
  config.listenbrainzToken = listenbrainzToken_->text().trimmed().toStdString();
  config.announceNowPlaying = nowPlayingBox_->isChecked();
  return config;
}

void PreferencesDialog::updateLevelLabel() {
  const int value = levelSlider_->value();
  QString text;
  if (value == 0) {
    text = tr("Nível 0% — sequencial");
  } else if (value == 100) {
    text = tr("Nível 100% — aleatório");
  } else {
    text = tr("Nível %1%").arg(value);
  }
  levelLabel_->setText(text);
}

void PreferencesDialog::updateScrobbleState() {
  const bool enabled = shuffleBox_->isChecked();
  levelSlider_->setEnabled(enabled);
  levelLabel_->setEnabled(enabled);
  hintLabel_->setEnabled(enabled);

  const bool lastfm = lastfmBox_->isChecked();
  lastfmKey_->setEnabled(lastfm);
  lastfmSecret_->setEnabled(lastfm);
  lastfmButton_->setEnabled(lastfm);

  const bool listenbrainz = listenbrainzBox_->isChecked();
  listenbrainzToken_->setEnabled(listenbrainz);

  nowPlayingBox_->setEnabled(lastfm || listenbrainz);
}

void PreferencesDialog::connectLastfm() {
  wamp::ScrobbleConfig config;
  config.lastfmEnabled = lastfmBox_->isChecked();
  config.lastfmApiKey = lastfmKey_->text().trimmed().toStdString();
  config.lastfmSharedSecret = lastfmSecret_->text().trimmed().toStdString();
  config.lastfmSessionKey = lastfmSession_;
  net_->setConfig(config);

  if (lastfmToken_.isEmpty()) {
    // Primeiro passo: pedir o token e mandar o usuário autorizar no navegador.
    QString token;
    QString authUrl;
    QString error;
    if (!net_->requestLastfmToken(&token, &authUrl, &error)) {
      lastfmStatus_->setText(tr("Não deu para pedir o token: %1").arg(error));
      return;
    }
    lastfmToken_ = token;
    QDesktopServices::openUrl(QUrl(authUrl));
    lastfmButton_->setText(tr("Já autorizei — concluir"));
    lastfmStatus_->setText(
        tr("Autorize no navegador que abriu e clique de novo em \"Já autorizei\"."));
    return;
  }

  // Segundo passo: trocar o token pela session key.
  std::string session;
  QString error;
  if (!net_->exchangeLastfmToken(lastfmToken_, &session, &error)) {
    lastfmStatus_->setText(tr("Autorização não concluída: %1").arg(error));
    return;
  }
  lastfmSession_ = session;
  lastfmToken_.clear();
  lastfmBox_->setChecked(true);
  lastfmButton_->setText(tr("Reconectar com o Last.fm…"));
  lastfmStatus_->setText(tr("Conectado — session key recebida."));
}

void PreferencesDialog::testListenbrainz() {
  QString error;
  if (net_->validateListenbrainzToken(listenbrainzToken_->text().trimmed(), &error)) {
    listenbrainzStatus_->setText(tr("Token válido."));
    listenbrainzBox_->setChecked(true);
  } else {
    listenbrainzStatus_->setText(tr("Token recusado: %1").arg(error));
  }
}

bool PreferencesDialog::xwaylandPosition() const {
  return xwaylandBox_ != nullptr && xwaylandBox_->isChecked();
}

QString PreferencesDialog::playlistMode() const {
  if (playlistModeBox_ == nullptr) return QStringLiteral("integrada");
  return playlistModeBox_->currentData().toString();
}

QString PreferencesDialog::language() const {
  return languageBox_ != nullptr ? languageBox_->currentData().toString()
                                 : QStringLiteral("sistema");
}
