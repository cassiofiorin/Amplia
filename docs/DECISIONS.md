# Decisões de arquitetura

Formato curto: contexto, decisão, consequência. Uma decisão só é revista com uma nova
entrada que a substitua explicitamente.

---

## D-001 — Qt 6 Widgets como front-end

**Contexto.** As opções eram GTK4, Qt Widgets e Qt Quick/QML.

**Decisão.** Qt 6 Widgets para a UI da F0–F4; QML fica reservado para a F5.

**Consequência.** Widgets resolvem bem janela compacta, always-on-top e desenho com
`QPainter` — que é o que a UI "aproximada" do Winamp pede. QML entra depois, quando o
requisito for skin com layout arbitrário e animação, sem reescrever o core. Custo: dependência
do runtime `org.kde.Platform`, que é o maior download do projeto.

---

## D-002 — FFmpeg como backend único de decode

**Contexto.** O Winamp usa um plugin por formato (`in_mp3`, `in_flac`, `in_vorbis`…), cada
um com seu decoder e suas peculiaridades.

**Decisão.** Um único caminho de decode via `libavformat`/`libavcodec`/`libswresample`.

**Consequência.** Um só código para 6 formatos, com metadados, capa e gapless no mesmo
lugar. Perde-se o "um plugin por formato" — que só faria sentido se o requisito fosse
compatibilidade com plugins Win32, que está fora de escopo. Se algum formato exigir
decoder próprio no futuro, a interface `IDecoder` já permite.

---

## D-003 — Saída PipeWire nativa, com fallback

**Contexto.** O ponto de atrito do uso via Bottles é a camada de tradução no caminho de
áudio. ALSA direto funciona em qualquer lugar; PipeWire é o servidor real na maioria dos
desktops atuais.

**Decisão.** `pw_stream` como saída principal. Se o serviço não estiver acessível, cair
para o PulseAudio (que conversa com o PipeWire-pulse) e, por último, ALSA.

**Consequência.** Latência baixa e integração com o mixer do desktop. Custo: três backends
para manter; a interface `IAudioOutput` mantém isso isolado em um arquivo por backend.

---

## D-004 — Formato interno fixo: F32 estéreo 48 kHz

**Contexto.** Arquivos vêm em 44,1/48/96 kHz, mono/estéreo, 8–32 bits.

**Decisão.** Converter tudo uma vez, na entrada, para F32 intercalado 48 kHz estéreo.

**Consequência.** EQ, plugins e saída lidam com um único layout — sem `switch` de formato
espalhado. Custo: um resample para arquivos de 44,1 kHz, feito pelo swresample (decente),
em vez de pelo servidor.

---

## D-005 — SQLite para a biblioteca

**Contexto.** O `ml_local` do Winamp tem banco próprio e é acoplado a COM/Wasabi.

**Decisão.** Reimplementar sobre SQLite, com esquema próprio.

**Consequência.** Consultas, FTS5 e migração trivial; independência total do código antigo.
Custo: nenhuma compatibilidade com o banco antigo do Winamp (não é requisito).

---

## D-006 — Plugins próprios em `.so`, sem carregador PE

**Contexto.** "Suporte a plugins" é o item que decide se o projeto precisa de emulação.

**Decisão.** API própria, bibliotecas `.so`, ABI versionada em C.

**Consequência.** Nada de Wine no caminho. O ecossistema de plugins Winamp não é
compatível — decisão consciente, registrada aqui para não ser reaberta por acidente. Um
bridge Wine dedicado a plugins específicos fica como experimento isolado, nunca como
dependência.

---

## D-007 — Projeto privado; o fonte do Winamp é especificação

**Contexto.** A licença do Winamp (WCL 1.0, cláusula 5) proíbe distribuir versões
modificadas e forks.

**Decisão.** Código em repositório privado, sem publicação. O fonte do Winamp é lido como
referência de comportamento, nunca copiado para `wamp/`.

**Consequência.** Portfólio se apoia em demo, screenshots e documentação. Copiar trechos
do Winamp para dentro de `wamp/` contaminaria o projeto inteiro — a regra é "reimplementar
a partir do comportamento, escrever o código do zero".

---

## D-008 — Identificador `io.github.cassiofiorin.Amplia`

**Contexto.** O Flatpak exige um app-id estável, usado também em D-Bus e nos caminhos de
configuração.

**Decisão.** `io.github.cassiofiorin.Amplia`.

**Consequência.** Renomear depois implica migrar `~/.var/app/...`, MPRIS e o desktop file.
Como é local, o custo de errar agora seria baixo — mas não há motivo para errar.

---

## D-009 — Scrobbling por sinks nativos + MPRIS2

**Contexto.** O plugin de Last.fm do Winamp é uma DLL Win32; os dados que ele envia são
apenas HTTP assinado.

**Decisão.** `ScrobbleController` com dois sinks (`LastFmSink`, `ListenBrainzSink`),
alimentados pelos eventos do engine; MPRIS2 publicado sempre.

**Consequência.** Independência de daemons externos e controle exato de "now playing".
MPRIS correto também permite que um daemon externo faça o trabalho, como compatibilidade.

---

## D-010 — sd-bus para MPRIS2

**Contexto.** Opções: `sdbus-c++`, `libdbus`, `sd-bus`.

**Decisão.** `sd-bus` (`libsystemd`).

**Consequência.** Sem dependência extra no runtime, API enxuta, `sd_event` para o loop.
Custo: API em C, mais verbosa que `sdbus-c++`.

---

## D-011 — Configuração em INI

**Contexto.** JSON ou TOML seriam escolhas modernas.

**Decisão.** INI, em `~/.config/amplia/amplia.ini`.

**Consequência.** Legível e editável à mão, como o `winamp.ini` que o público-alvo conhece;
parser trivial de escrever. Custo: sem tipos aninhados — irrelevante para o volume de
configuração aqui.

---

## D-012 — CMake + Ninja, compilando dentro do SDK

**Contexto.** Meson é mais agradável, mas o SDK do KDE e o `flatpak-builder` tratam CMake
como cidadão de primeira classe.

**Decisão.** CMake ≥ 3.25 com `cmake-ninja` no manifest; presets para debug/release.

**Consequência.** Um comando para build local e o mesmo para o Flatpak. Custo: nenhum
relevante.

---

## D-013 — `flatpak-builder` do host, não o `org.flatpak.Builder`

**Contexto.** O caminho natural seria `flatpak run --command=flatpak-builder
org.flatpak.Builder`. Ele falha no `flatpak info --arch=x86_64 --show-commit org.kde.Sdk
6.10` com `error: org.kde.Sdk/x86_64/6.10 not installed`, embora o mesmo comando funcione
no host e dentro do próprio sandbox do Builder — a falha vem do escape para o host que o
flatpak-builder faz com ambiente próprio, onde a instalação de usuário do Flatpak (a única
que tem o SDK, porque não há root aqui) não é encontrada.

**Decisão.** Usar o binário `flatpak-builder` do host. Sem root, ele vem de
`dnf download flatpak-builder` + `rpm2cpio`, extraído em
`~/.local/share/wamp-tools/flatpak-builder/`; `scripts/build-flatpak.sh` resolve a ordem
de preferência e documenta o caso no README.

**Consequência.** Build funciona sem root e sem depender do app Builder. Custo: um passo
manual de bootstrap documentado (trivial assim que houver root para `dnf install`).

---

## D-014 — Metadados em fila própria, lendo só o cabeçalho

**Contexto.** A playlist precisa mostrar "Artista - Título" como no Winamp, mas abrir
cada arquivo com `avcodec_open2` custa uma thread e um decoder por faixa — inviável para
pastas grandes, e bloquearia a janela se fosse feito na UI.

**Decisão.** Uma `MetadataScanner` no core, com thread própria e fila FIFO, que usa
`probeTrack()`: `avformat_open_input` + `avformat_find_stream_info` + leitura de tags, sem
abrir codec. A UI enfileira ao adicionar (arquivo, drop, `.m3u`) e atualiza as linhas
conforme os resultados chegam por `invokeMethod`. `fillTrackInfo` é compartilhada com o
decoder, então a lista e o player nunca discordam do que é uma faixa.

**Consequência.** Importar milhares de faixas é instantâneo e a reprodução não espera a
leitura. Custo: duração de alguns formatos pode diferir ligeiramente da decodificada
(estimativa do container); quando a faixa toca, o valor do decoder prevalece.

---

## D-015 — Delegate próprio para as colunas da lista

**Contexto.** Número da faixa e posição na fila parecem texto, mas alinhar à direita com
espaços depende da fonte e quebra ao passar de 9/99 itens; além disso o número da fila
precisa de cor própria.

**Decisão.** `PlaylistDelegate` (em `src/ui/playlist_delegate.h`) desenha três colunas:
gutter do número, rótulo elidido no meio e badge da fila. A posição vem do role
`kQueueRole`, recalculada por `MainWindow::refreshQueue()` depois de enfileirar, consumir
a fila ou remontar a lista. `Playlist::queuePosition(index)` devolve 1-based ou 0.

**Consequência.** Colunas estáveis em qualquer fonte e contagem. Custo: `tools/preview.cpp`
(`wamp-ui-preview`) renderiza a lista num PNG para conferir o desenho sem abrir janela —
foi assim que a dupla pintura de texto do estilo padrão foi pega.

## D-016 — Sorteio com janela deslizante (o *morph rate* do Winamp)

O Winamp não sorteava a playlist inteira: tinha um nível que misturava sequencial e
aleatório. Reaproveitamos a ideia como janela deslizante: com nível `L`, o próximo
índice sai de `(atual + 1 + U[0, round(L × N))) % N`. Em `L = 0` a janela é 1 e vira a
próxima faixa; em `L = 1` a janela cobre a lista toda. Valores intermediários mantêm
coerência (o sorteio "pula perto" em vez de teleportar) e o histórico das últimas
faixas evita repetição imediata, com desempate quando a lista é pequena demais.

Alternativa descartada: sortear duas listas e intercalar (padrão de "shuffle on/off"
de outros players). Não tem nível — com ele, ou é sequencial ou é aleatório.

## D-017 — Playlist na mesma janela, crescendo para baixo

Primeiro a lista era um painel embutido que esticava a janela; depois virou janela
própria para a principal não se mexer. Nenhum dos dois ficou bom: no Wayland o cliente
não posiciona janelas, então o "acoplamento" do Winamp (uma janela arrastar a outra) é
impossível, e duas janelas viram duas entradas na barra de tarefas.

Voltamos para **uma janela**: a lista entra embaixo e `PL` acrescenta 300 px de altura,
crescendo para baixo. Como só existe uma janela, "acoplado" é o estado natural e a barra
de tarefas mostra um item só. A geometria vai para a INI (`saveGeometry`): o tamanho
sempre volta; a posição, só onde o compositor deixa (X11).

Trade-off aceito: sem posicionamento não dá para "empurrar" a janela para cima quando
ela está no rodapé da tela, então a lista pode nascer parcialmente fora da área visível
nesse caso.

## D-018 — INI na mão, com `QSettings` e caminho explícito

`QSettings` com organização/app grava em `~/.config/cassiofiorin/WAmp.conf` (hoje `Amplia.conf`); a
arquitetura promete `~/.config/amplia/amplia.ini`. Passando o caminho completo no
construtor (`QSettings(path, QSettings::IniFormat)`) o formato e o lugar ficam iguais
aos documentados, e continuamos sem dependência nova.

## D-019 — Gapless por prefetch com troca de metadados pendente

O engine abre o decoder seguinte 2 s antes do fim e troca o ponteiro no `EOF`, sem limpar
o ring — é o que garante ausência de silêncio. Mas anunciar a faixa nova no `EOF` faria o
título mudar 2 s antes de o som virar.

Por isso a UI recebe `onTrackChanged` só quando o contador de frames da faixa antiga zera
no `pull()`: o anúncio acontece no sample em que a faixa nova começa a soar. Um seek nessa
janela reabre a faixa visível antes de reposicionar, para não cair na próxima.

Alternativa descartada: notificar no `EOF` e aceitar a antecedência; o título adiantado é
visível e passaria por bug para quem conhece o Winamp.

## D-020 — Slider de posição próprio e limpeza com confirmação

O `QSlider` do Qt trata clique na trilha como *page step*: com faixa em milissegundos isso
salta minutos, e o usuário lê como "só funciona arrastando". `wamp/src/ui/click_slider.h`
(`ClickSlider`) posiciona o valor no ponto clicado e deixa o arrasto seguir dali,
mantendo os sinais `sliderPressed`/`sliderReleased` de que a janela já depende para não
brigar com o timer. O mesmo widget serve ao volume (0–100), onde o clique parecia não
fazer nada; lá o `valueChanged` já mexe no ganho, então o ajuste é imediato.

Limpar a playlist passou a pedir confirmação: a lista pode representar horas de curadoria
e fica a um clique de um botão do rodapé. As demais ações ficam num menu, não em botões
soltos, para reduzir clique acidental sem esconder o que é frequente.

## D-021 — Arrasto aceita só caminhos locais; rede fica para depois

Diagnóstico do arrasto que "não funcionava": os eventos chegam ao app (confirmado com log),
mas o Dolphin entrega URLs `smb://` quando a música está num compartilhamento aberto por
KIO — e `QUrl::toLocalFile()` devolve vazio para elas. O mount do `kio-fuse`
(`/run/user/1000/kio-fuse-XXXX`) não existe dentro do sandbox Flatpak, então nem dá para
traduzir a URL em caminho.

Decisão desta rodada: aceitar apenas caminhos locais e responder com mensagem na barra de
status quando o arrasto trouxer outra coisa. Tocar da rede é recurso próprio (KIO, ou
streaming por FFmpeg com protocolo remoto), não conserto de arrasto — entra no roadmap
quando o usuário decidir se a biblioteca fica no NAS.

Alternativa que já funciona sem código: montar o compartilhamento como caminho real
(`cifs` no fstab ou `gio mount`), porque aí a URL é `file://` e o drag entra.

## D-022 — MPRIS2 com sd-bus, não com QtDBus

Contexto: a primeira versão do servidor MPRIS usava QtDBus (adaptadores
`QDBusAbstractAdaptor` + `registerObject`). O nome subia no barramento e o caminho
respondia a `Introspect`, mas as interfaces MPRIS **não** apareciam: a introspecção só
listava `org.freedesktop.DBus.Properties/.Introspectable/.Peer` e `GetAll` devolvia
dicionário vazio. Registrar dois objetos no mesmo caminho também não serve — o segundo
`registerObject` devolve false.

Decisão: implementar com **sd-bus** (`libsystemd`, já previsto no D-010), com o
bombeamento integrado ao laço do Qt por `QSocketNotifier`.

Por quê: o sd-bus publica as propriedades direto da vtable (sem a camada de
adaptadores do Qt), dá controle explícito de quando emitir `PropertiesChanged` e não
depende do QtDBus para negociar interface. Custo: ~400 linhas de vtable e um ponto de
atenção — todo método precisa responder com `sd_bus_reply_method_return()`, senão a
conexão cai.

## D-023 — Aviso de arrasto de rede em caixa, não na barra de status

Contexto: arquivos arrastados do Dolphin via KIO chegam como `smb://…`; o texto na barra
de status desaparecia antes de a pessoa ler.

Decisão: `QMessageBox` não modal, com botão único **Entendido**, listando os esquemas
recusados e explicando o caminho alternativo (montar o compartilhamento / copiar para o
disco).

Por quê: o erro é de expectativa (o atalho parece um arquivo), não de digitação; precisa
ficar na tela até ser lido, mas sem travar a reprodução.

## D-024 — Tamanho da janela salvo como largura + altura compacta

Contexto: guardar `saveGeometry()` fazia a janela reabrir com a altura de quando a lista
estava aberta, deixando um vão vazio quando ela voltava fechada.

Decisão: persistir `window/width` e `window/collapsedHeight`; ao carregar, redimensionar
para a altura compacta e, se a lista estava aberta, crescer os 300 px de novo.

Por quê: o estado da lista é guardado separadamente (`playlist/visible`); a geometria
não deve depender dele.

## D-025 — Adicionar à playlist não inicia a reprodução

Contexto: arrastar arquivos para a janela, ou usar `Adicionar…`, começava a tocar assim
que a lista estava vazia. Quem estava organizando a biblioteca ouvia som sem querer.

Decisão: só inicia a reprodução quando o pedido é explícito — `Abrir…` (`Ctrl+O`), linha
de comando, duplo clique/`Enter` na lista, botão `Play` ou `OpenUri` pelo MPRIS. Arrastar
e `Adicionar…` carregam a primeira faixa em pausa (a janela mostra título e posição) e
avisam na barra de status.

Por quê: "adicionar" e "tocar" são intenções diferentes; o Winamp clássico também separa
`Enqueue` de `Play`. Carregar em pausa mantém a janela informativa sem fazer barulho.

## D-026 — Scrobbling com Qt Network e fila em JSON

Contexto: o plano original da F1 previa libcurl + lotes de 50 scrobbles para o Last.fm.

Decisão: usar `QNetworkAccessManager` (Qt6::Network, já presente no runtime KDE) num módulo
próprio `wamp/src/scrobble/`, um scrobble por requisição, e uma fila de pendências num JSON
em `QStandardPaths::AppDataLocation`. O envio é assíncrono: nada de bloquear a interface
numa requisição de rede.

Por quê: libcurl exigiria mais uma dependência de runtime e código de HTTP manual (TLS,
timeouts); o Qt Network é assíncrono por natureza e o `QCryptographicHash` já resolve o
`api_sig` do Last.fm. Lote de 50 só compensa para atrasados em massa — quando a fila tiver
volume, agrupar é uma mudança localizada. A fila em disco evita perder scrobble quando o
app fecha ou a rede cai.

## D-027 — Sem "ouvindo agora" (now playing) no Last.fm

Contexto: o `track.updateNowPlaying` não tem contraparte para encerrar o anúncio, e o
Last.fm não aceita removê-lo. Se o player parasse antes do mínimo de scrobble, a faixa
ficava presa como "ouvindo agora" no perfil até outro anúncio substituí-la.

Decisão: o anúncio existe, mas **desligado por padrão** (`ScrobbleConfig::announceNowPlaying`,
caixa em Preferências ▸ Scrobbling, chave `scrobble/nowPlaying` na INI). Ligado, ele só sai
depois de 5 s de reprodução real, para faixa que nem começou a tocar não pintar o perfil.
Com a caixa desligada o app manda só o scrobble, quando a regra é atingida (>30 s de
duração e 50%/4 min tocados).

Por que desligado por padrão: o anúncio é opcional no protocolo e criava um estado que o
app não podia limpar. A opção existe porque o "ouvindo agora" é útil para quem gosta de
mostrar o que está tocando, e a limitação é do Last.fm, não do Amplia.

## D-028 — Título da janela mostra a faixa tocando

Contexto: o Winamp original mostrava a faixa no botão da barra de tarefas; no KDE o painel
de tarefas usa o título da janela, então com o título fixo em `WAmp` (hoje `Amplia`) a informação se perdia.

Decisão: `updateWindowTitle()` (chamado em `refresh()`) põe `Artista - Título — Amplia`
enquanto há faixa carregada e o estado não é `Idle`, e volta ao nome do app ao parar. O sufixo
`— Amplia` entra no próprio título porque o compositor só o anexa quando quer (no Wayland o
cliente não controla isso), e o painel de tarefas pode mostrar apenas o título.

Junto: `compactHeight_` (medida uma vez no layout compacto) passou a ser a referência de
altura da janela. Antes a altura compacta era recalculada de `height() - 300`, o que, num
fechamento com a lista aberta, gravava uma altura inflada e a janela reabria com um vão
vazio.

## D-029 — Sessão: a lista anterior volta, em pausa

Contexto: abrir o app e encontrar a playlist vazia obriga a remontar a escuta de ontem,
principalmente para quem usa fila e sorteio.

Decisão: `saveSettings()` grava `playlist/items` (caminhos) e `playlist/current` (índice);
`loadSettings()` devolve a lista, seleciona a faixa atual e a carrega **em pausa** — tocar
sozinho no start seria surpresa, e a D-025 já diz que carregar não é tocar. Arquivo passado
na linha de comando desliga a restauração (o que veio de fora é a intenção).

Bug encontrado no caminho: o construtor chamava `saveSettings()` (via `setPlaylistVisible`)
**antes** de `loadSettings()`, e aquela gravação apagava `playlist/items` da INI a cada
abertura. Corrigido com a guarda `settingsLoaded_`, ligada no fim de `loadSettings()`.

Alternativa descartada: retomar também a posição em ms; hoje o custo de decidir (retomar
do ponto ou do início?) não paga, e o Play começa do zero.

## D-030 — Pasta entra varrida, em ordem natural

Contexto: adicionar uma pasta colocava ela mesma na lista, e o item não tocava
(`avformat_open_input: É um diretório`) — o Winamp sempre varreu o conteúdo.

Decisão: `expandAudioPaths()` roda antes da playlist e troca cada pasta pelos arquivos de
áudio dentro dela, recursivamente, com lista branca de extensões (mp3, flac, ogg, opus, wav,
m4a, aac, wma, ape, wv, mpc, tta, aiff, dsf, dff, ac3, spx, caf…). A ordem é **natural**
(`QCollator` com `setNumericMode`), então uma discografia em `CD1/`, `CD2/`, `CD10/` sai na
ordem certa e não em `CD1, CD10, CD2`. Arquivos passados direto mantêm a ordem recebida, e
o conjunto todo passa por dedupe.

Arquivos que não são áudio dentro da pasta são contados e informados na barra de status
(`N arquivo(s) de M pasta(s) • K ignorado(s)`), em vez de entrarem mudos na lista.

Na linha de comando, pasta no primeiro argumento vai para `addFiles` (varredura) em vez de
`openFile`: o que se espera de `wamp ~/Música/Artista` é a lista montada, não um erro.

## D-031 — Cache de tags em SQLite (chave path + size + mtime)

Contexto: a cada abertura o app relia o cabeçalho de todas as faixas da playlist para
mostrar artista/título/duração. Com algumas centenas de arquivos isso já era perceptível,
e a ordenação por título (próxima da fila) depende de metadado imediato.

Decisão: `wamp::TagCache` em `wamp/src/library/`, SQLite em WAL no diretório de dados.
Chave `path`, validade por `size` + `mtime` (dois inteiros, sem hash de conteúdo: mover o
arquivo ou reescrever as tags muda o `mtime`). Leitura só do que falta; gravação em
transação confirmada a cada 32 faixas e no fechamento.

Alternativas descartadas: JSON (cresce e não indexa por caminho), reescrever a INI (conflita
com configuração), Relacionar com `plays` desde já (a biblioteca completa é F3; aqui o escopo
é só o cache).

## D-032 — File Info com TagLib e gravação segura

Contexto: editar tags de fora do app (EasyTAG, Kid3) quebra o fluxo, e gravar com o FFmpeg
significaria remuxar o áudio — risco desnecessário na biblioteca de alguém.

Decisão: ler e escrever com **TagLib 2.1.1**, que já existe no SDK e no runtime do KDE
(nada a empacotar) e reescreve só a região de tags. Campos genéricos pelo `Tag()`;
os que não são de primeira classe (album artist, compositor, gravadora, BPM, disco,
Replay Gain) pelo `PropertyMap`. Imagem embutida por formato (ID3v2 APIC, FLAC picture
block, MP4 `covr`).

Gravação: **cópia temporária + releitura + rename atômico** por padrão (`<arquivo>.wamp-tmp`).
Gravar não é o difícil; provar que gravou, sim — por isso a releitura antes de promover a
cópia. Ligável/desligável no rodapé da janela.

Alternativa descartada: gravar direto no arquivo (mais rápido, mas um desligamento no meio
deixa a tag pela metade) e FFmpeg para escrita (remuxa o áudio inteiro).

## D-033 — Jump to File no menu, com a vírgula vinda de `Qt::Key_Comma`

A tecla `,` não funcionava porque `QKeySequence(",")` interpreta a vírgula como separador
de acorde (`QKeySequence("Ctrl+A, B")`) e devolve uma sequência vazia — o `QShortcut`
nunca casava. Passa a ser uma `QAction` com `QKeySequence(Qt::Key_Comma)`.

Segundo detalhe: ações dentro de um `QMenu` preso a um `QPushButton` (os menus do rodapé)
não registram o atalho na janela — o menu só existe aberto. Por isso, ao montar o rodapé,
as ações com atalho são registradas também no widget principal (`addAction`), exceto as
compartilhadas com o menu do topo, que já vivem lá (registrar de novo daria atalho ambíguo).

## D-034 — PlayPause com debounce de 400 ms

O BlueZ entrega o toque do fone **duas vezes** (AVRCP + proxy do KDE). O método alternava
duas vezes e voltava ao estado inicial: para o usuário, "o botão do fone não faz nada",
enquanto o Firefox funcionava porque engolia a repetição.

Decisão: `playerPlayPause` ignora chamadas dentro de 400 ms da anterior (relógio monotônico),
respondendo D-Bus normalmente. 400 ms é folgado para o dedo e curto para quem aperta duas
vezes de propósito. Os demais métodos (Next/Previous/Stop) não têm repetição e ficaram sem
filtro.

## D-035 — File Info avisa quando não dá para gravar

Antes, `OK` tentava gravar e falhava com mensagem genérica do TagLib. Agora
`TagEditor::canSave(path, safeSave, &reason)` responde antes:

- arquivo fora do disco;
- pasta em **sistema de arquivos somente leitura** (`statvfs` + `ST_RDONLY`) — é o caso do
  NAS do usuário, montado `ro` por CIFS com modo 0755, onde o `access(W_OK)` sozinho mentiria;
- pasta sem permissão de escrita (gravação segura, que troca o arquivo por rename);
- arquivo sem permissão de escrita (gravação direta).

O diálogo mostra "Somente leitura — <motivo>" em âmbar acima do rodapé e desabilita `OK`, com
guardar também no `saveCurrent()`. Nada é escrito para descobrir isso: sem arquivo de teste,
sem tocar no disco do usuário.

## D-036 — Agente na bandeja (ícone próprio)

Contexto: o ícone que aparecia na bandeja não era nosso — é o applet "Reprodutor de mídia" do
Plasma, que se registra sozinho quando um player publica MPRIS2. Quem fechava a janela perdia o
player; o agente do Winamp resolvia isso ficando na bandeja.

Decisão: `QSystemTrayIcon` com StatusNotifierItem, criado só quando há observador de bandeja
(`isSystemTrayAvailable()`), e menu com o que o agente original oferece: Mostrar/Ocultar,
Reproduzir/Pausar, Anterior, Próxima, Aleatório, Repetir e Sair. Tooltip mostra
`Artista - Título` e o estado. Clique esquerdo alterna a janela; clique do meio alterna
play/pause.

Por que menu, e não botões: SNI não tem botões — o ícone é só um item com menu de contexto.
Os "botões" do agente do Winamp aparecem aqui como itens do menu; um painel flutuante com
botões seria outra janela para manter.

"Fechar para a bandeja" é opção (Preferências ▸ Reprodução), desligada por padrão: esconder
sem avisar quem só queria fechar seria pior que fechar. Na primeira vez o ícone mostra um
balão explicando que a música continua. O `Quit` do MPRIS e o "Sair" da bandeja encerram de
verdade (a flag `quitting_` atravessa o `closeEvent`).

Permissão: `--talk-name=org.kde.StatusNotifierWatcher` no manifest, sem a qual o registro no
observador falha dentro do sandbox.

Armadilha encontrada no teste: com o item SNI no ar, o Qt **deixa de encerrar** ao fechar a
última janela — o processo ficava vivo e sem janela. Por isso o `closeEvent`, depois do
`QMainWindow::closeEvent`, agenda `QCoreApplication::quit()` quando o fechamento é para sair
(`quitting_` ou bandeja desligada). `AMPLIA_NO_TRAY=1` desliga o agente para depurar isso.

## D-037 — Instância única com socket abstrato

Contexto: com "fechar para a bandeja", abrir o player de novo criaria um segundo processo
disputando `org.mpris.MediaPlayer2.amplia` e duas trilhas de áudio independentes.

Decisão: `QLocalServer`/`QLocalSocket` com o nome `io.github.cassiofiorin.Amplia.instance`
(socket abstrato no Linux). A segunda instância manda os caminhos recebidos na linha de
comando (um por linha), espera o envio e sai. A primeira adiciona à playlist e traz a janela
para a frente.

Por que socket abstrato: não cria arquivo, sobrevive ao `/tmp` privado do Flatpak e não exige
`XDG_RUNTIME_DIR` gravável dentro do sandbox. Alternativa descartada: lockfile em
`AppDataLocation` (sobra lixo depois de queda e não carrega os caminhos).

## D-038 — Repetir lista

Contexto: o agente do Winamp tem botão de repetir, e o Amplia não tinha repetição nenhuma —
`Playlist::nextIndex()` sempre dava a volta, mas `updateNextUp()` parava no fim por não
haver `repeat_`.

Decisão: `bool repeat_` (Opções ▸ Repetir lista, tecla `R`; acionável pela bandeja e pelo
MPRIS `LoopStatus`). `nextTrackIndex()` devolve `kNone` no fim quando não há sorteio nem
repetição; com repetição, dá a volta. O MPRIS publica `LoopStatus: Playlist` quando ligado e
emite `PropertiesChanged` na troca; "Track" (repetir uma) é ignorado de propósito até o
motor saber repetir um arquivo só, em vez de mentir para o applet.

## D-039 — Capa no MPRIS (`mpris:artUrl`)

Contexto: o applet do KDE mostrava a faixa e os controles, mas nenhuma capa — o Metadata não
tinha `mpris:artUrl`.

Decisão: a capa embutida (ID3v2 APIC, FLAC picture, MP4 covr) é extraída para
`QStandardPaths::CacheLocation/art/<sha1(caminho|mtime)>.<ext>` e o MPRIS publica
`file://...`. A chave inclui a data de modificação, então editar a capa no File Info muda o
nome do arquivo e o applet não mostra a antiga. Faixa sem capa simplesmente não publica a
chave.

Por que arquivo em cache e não `data:` URI: o KDE carrega por URL de arquivo; um data URI de
vários MB iria inteiro em cada `Metadata` e em cada `PropertiesChanged`.

## D-040 — Formatos de playlist num só lugar

Contexto: o `.m3u` já existia dentro de `Playlist`; o Winamp também lê `.pls` (rádio),
`.b4s` (o formato dele próprio) e `.wpl` (Windows Media Player). Espalhar um leitor por
formato em cada canto que abre lista (diálogo, linha de comando, arrasto) criaria quatro
caminhos com regras diferentes de caminho relativo.

Decisão: `playlist_file.{h,cpp}` concentra leitura e gravação dos quatro formatos, sem Qt
(só `std`), e `Playlist::loadPlaylist`/`savePlaylist` são invólucros finos. Quem abre lista
passa sempre por lá: diálogo "Abrir playlist", linha de comando e arquivo solto no arrasto.

Regras que valem para os quatro: caminho relativo é resolvido contra a pasta do arquivo
(é o que o Winamp faz e o que faz uma pasta de álbum portátil funcionar); `%HH` é decodificado
(`file:///x/Com%20Espaco.mp3`); `&amp;` e companhia são desfeitos nos XML; BOM é descartado;
URL de rede (`http://`, `mms://`, `rtsp://`…) é preservada como está — o FFmpeg abre stream,
então não faria sentido recusar.

Gravação escolhe o formato pela extensão; sem extensão conhecida, `.m3u`. O `.b4s` e o `.wpl`
gravam `file://` percent-encoded, que é o que os players do Windows esperam.

## D-041 — Aparência por esquema de cores, não por skin

Contexto: o pedido é dar a camada nostálgica das skins modernas do Winamp sem renderizar
arquivo externo. Um leitor de `.wsz` (F5) é um projeto à parte: extrai bitmaps, posiciona
dezenas de sprites, desenha janelas irregulares.

Decisão: a F2D troca **paleta**, não forma. O menu se chama **Aparência ▸ Esquema de cores**
e os presets têm nomes descritivos — Verde LCD, Âmbar, Grafite, Azul aço, Papel —, não nomes
de skin da Nullsoft: a cor é nossa, o nome de marca não.

O trabalho de verdade é centralizar os tokens que hoje estão espalhados (constante de
stylesheet em `main_window.cpp`, cores fixas no `PlaylistDelegate`, âmbar do aviso de
somente leitura). Depois disso um esquema é um vetor de ~12 cores.

Fica depois da F2C (visualização) e antes da F3 (biblioteca) porque o visualizador e a curva
do EQ são os consumidores de cor pesados, e porque as janelas novas da biblioteca devem
nascer lendo os tokens em vez de serem repintadas depois.

## D-042 — Capa de pasta e colunas da lista

Capa: o `mpris:artUrl` tenta primeiro a imagem embutida (ID3v2 APIC, FLAC picture, MP4
covr). Só quando a tag não traz nenhuma é que a pasta é olhada, em ordem de preferência —
`cover`, `folder`, `front`, `album`, `albumart`, `artwork`, nas extensões `.jpg`, `.jpeg`,
`.png`, `.webp` — e a comparação ignora caixa, porque discografia vinda do Windows tem
`Cover.JPG`. A busca mora em `wamp::TagEditor::folderCover` (testável), não na janela.

Cache: cada origem tem seu próprio nome (`sha1(caminho|mtime|origem)`), então trocar a
`cover.jpg` da pasta muda o arquivo no cache e ninguém vê capa velha.

Colunas: a duração saiu do meio do rótulo e virou coluna própria (`kDurationRole`,
alinhada à direita, com a largura reservada para `00:00:00`), porque antes ela desalinhava
a cada faixa com e sem tag. O rodapé da lista ganhou **Mostrar caminho completo**, guardado
em `playlist/showPath`: quando ligado, a coluna do título mostra o caminho (o caso de duas
pastas com `01 - Intro.mp3`). O elide continua no meio — é o que preserva o começo e o fim.

## D-043 — Arquivo que não toca: avisa uma vez, filtra em silêncio no lote

Contexto: abrir um `.mkv` (ou uma imagem, um `.txt`) fazia o player tentar decodificar e
falhar com "falha ao abrir" — mensagem de erro técnica para algo que é apenas fora do
escopo. E o Winamp original tocava alguns vídeos, o que levanta a pergunta de novo.

Decisão: **não** entra pipeline de vídeo. O app é de áudio e continua leve; a F6 de vídeo
foi avaliada e descartada (o custo não é o decode — é a saída de imagem com sincronia).

Em troca, o usuário recebe uma explicação quando **ele pediu aquele arquivo**: abertura
explícita (linha de comando, "Abrir…", um arquivo solto arrastado) mostra uma caixa com o
nome, o motivo (vídeo / extensão não áudio) e a lista de formatos que o Amplia toca, mais um
botão "Abrir no player do sistema". Importação em massa não pode virar 200 avisos: pasta
ou vários arquivos entram filtrando o que é áudio e só contam os ignorados na barra.

Antes de recusar por extensão, um arquivo solto é perguntado ao FFmpeg
(`wamp::probeAudioOnly`): `.mka`, `.m4b` e contêineres exóticos com **áudio e nenhum vídeo
de verdade** tocam normalmente. Capa embutida não conta como vídeo — vem como stream de
imagem com `AV_DISPOSITION_ATTACHED_PIC`, e é justamente o caso de todo MP3 com arte.

`wamp/src/ui/format_support.h` guarda as listas de extensões (áudio, playlist, vídeo) para
que o aviso, a varredura de pastas e a linha de comando não divirjam.

## D-044 — Equalizador de 10 bandas com biquads, na thread de controle

Contexto: o EQ precisa mexer em todo sample e não pode fazer a thread de áudio esperar
por um slider. Também precisa ser desligável sem custo quando está flat.

Decisão: `Equalizer` (core) é uma cascata de dez filtros **peaking** (receita do RBJ) com
Q 1.0 — a largura de oitava aproximada que o Winamp usa —, centros 60, 170, 310, 600,
1k, 3k, 6k, 12k, 14k e 16k Hz, ganhos de −12 a +12 dB, mais pré-amplificação de −12 a +12.
Coeficientes recalculados no `setGains`/`setPreamp` e trocados por `try_lock`: a thread de
áudio usa a tabela anterior se a UI estiver no meio de um ajuste — nunca bloqueia, e o pior
caso é um bloco de 1024 frames com a curva velha.

O processamento acontece na **thread de controle**, logo depois do decode e antes do
`ring.write`, junto do gapless. A thread RT continua só copiando amostras. Bandas com
ganho abaixo de 0,05 dB ficam inativas e são puladas, então um EQ flat custa quase nada.

Alternativas descartadas: FFT por bloco (mais caro, latência de janela e pré-eco) e
equalizador shelving por oitava (menos parecido com o Winamp).

Estado: `eq/enabled`, `eq/preamp`, `eq/band0..eq/band9` na INI; gravação com atraso de
500 ms para não escrever o arquivo a cada pixel de arrasto. As curvas predefinidas vivem no
diálogo (18 curvas clássicas); importar/exportar `.eqf` fica para a F2.2.

## D-045 — Limitador do EQ10 e a escala do pré-amp

Pergunta do usuário (m03607): no Winamp original a pré-amplificação não distorce; no nosso
sim. A resposta está no código do original, que está no repositório de referência
(`Src/Winamp/eq10dsp.cpp`, EQ10 da 4Front/George Yohng).

**A escala é logarítmica nos dois lugares.** `eq10_db2gain(dB) = 10^(dB/20) − 1` — o `−1`
existe porque o filtro de banda devolve uma *diferença* que é somada à entrada
(`out = y0 + in`), então o ganho aplicado é a diferença em relação a 1.0. A pré-amplificação
também é dB convertido com `10^(dB/20)`. Não é uma escala linear "mais fraca".

**O que o original tem e nós não tínhamos: um limitador dinâmico.** `EQ10_TRIM_CODE 0.930`
(teto em −0,6 dB) e `EQ10_TRIM_RELEASE 0.700` s. Um detector de pico com ataque instantâneo
e queda lenta; quando o pico passa de 0,93, a saída inteira é multiplicada por `0.93/pico`,
e o detector volta devagar. Por isso empurrar o pré-amp no Winamp comprime em vez de estourar.

Decisão: adotamos o mesmo limitador, nos mesmos números, por amostra
(`limiterDetect_ *= pow(0.001, 1/(rate*0.7))`). Ele roda depois das bandas, dentro do
`process()`, e é sempre ligado — no original o `config_eq_limiter` é o padrão.

Aproveitamos para alinhar a tabela de frequências ao original: 70, 180, 320, 600, 1k, 3k,
6k, 12k, 14k, 16k Hz (antes usávamos 60/170/310, a tabela "de livro").

Fica registrado como refinamento opcional da F2.2: o EQ10 usa **duas** tabelas de
coeficientes por banda (larguras diferentes para reforço e corte, Q globais 2.82 e 0.705),
e o filtro dele é de primeira ordem somado à entrada. Hoje usamos peaking biquad com Q 1.0
para os dois sentidos; com o limitador, a diferença audível é pequena, mas quem quiser o
timbre idêntico ao original sabe onde mexer.

Também nesta rodada: o preset **Flat** saiu do combo (o botão **Zerar** faz isso), e o
**Zerar** agora pede confirmação — apaga a curva inteira de uma vez.

## D-046 — Linhagem: reimplementação independente, não fork

Classificação honesta do que o `wamp/` é:

- **Não é fork.** Fork implica continuar a mesma árvore de código (histórico, arquivos, build).
  O `wamp/` nunca compilou uma linha do Winamp, nunca incluiu um header deles e não tem
  ancestral comum no git. A árvore original segue intocada no mesmo repositório.
- **É reimplementação independente com consulta ao fonte.** Escrevemos tudo do zero, mas eu li
  `Src/Winamp/eq10dsp.cpp` para entender por que o EQ original não distorce. Isso tira o
  projeto do conceito estrito de *clean room* (onde quem escreve o código nunca vê o original):
  a consulta existiu, e três fatos foram aproveitados — a tabela de frequências, o teto e o
  tempo de retorno do limitador.
- **O que nunca entrou**: arquivos, headers, objetos, imagens de skin, `.eqf`, textos ou ícones
  do Winamp. A UI, os parsers, o ring buffer, o gapless, o MPRIS via sd-bus e o módulo de
  scrobble são código novo.

Decisão (m03653): mantivemos os valores do limitador (0,930 e 0,7 s). Balística de limitador
com teto em −0,6 dB e retorno subsegundo é escolha genérica de engenharia de áudio, presente
em vários limitadores — não é a parte expressiva do EQ10. Se algum dia isso incomodar, o custo
de trocar por valores próprios é de duas linhas.

Consequência prática: o código do `wamp/` não é obra derivada do Winamp e pode ser licenciado
como o autor quiser. O que continua fora de alcance é o **nome e o logo** do Winamp (marca) e
a distribuição de qualquer artefato deles (`wsz`, `eqf`, binários). Se o app for publicado,
as obrigações são outras: Qt, FFmpeg (LGPL), TagLib (LGPL/MPL) e libsystemd — todas resolvidas
por linkagem dinâmica, sem modificar as bibliotecas.

## D-047 — Presets: ler o `.eqf` do Winamp, compartilhar em CSV, timbre do EQ10

**O `.eqf` tem dois formatos.** O texto (`[Equalizer Preset]`, `PresetName=`, `Band0=…`) é o
que circula em sites de preset e o que players clonados importam. Mas o próprio Winamp grava
um **binário**: assinatura `Winamp EQ library file v1.1\x1a!--`, seguida de registros fixos de
261 bytes (nome) + 10 bytes (bandas) + 4 bytes (pré-amp), com os valores no lugar do dB —
slider 0..63, meio em 31. Lemos os dois e detectamos pelo primeiro byte. Isso é ler o
**formato**, não o gerador: a conversão slider→dB é uma tabela do arquivo, não uma decisão
de design que copiamos.

**O tamanho do registro não é fixo.** A primeira versão assumia 261 bytes de nome + 10
de bandas + 4 de pré-amp (275 por curva) e recusava como "truncado" um `.eqf` real do
Winamp 5, que grava 257 + 10 + 1 = 268 (um preset de teste com 299 bytes = 31 de
assinatura + 268). Agora o registro é descoberto dividindo o que sobra da assinatura por
candidatos de 251..262 bytes de nome, com pré-amp de 1 ou 4 bytes, e só vale o candidato
cujas dez bandas caem no domínio do slider (0..63). O `.eqf` de texto e o binário seguem
detectados pela assinatura.

**Compartilhar em CSV.** Formato nosso, documentado no cabeçalho:

```
nome,preamp_db,70,180,320,600,1000,3000,6000,12000,14000,16000
Rock,0,8,5,-2,-3,-1,3,6,7,8,8
```

Abre em qualquer planilha, não precisa de código para ler, e um amigo pode te mandar uma curva
por mensagem. O `.eqf` continua sendo o caminho de entrada para os presets que você já tem.

**Timbre: agora é o EQ10.** Antes tínhamos um peaking biquad de livro. Agora a banda é a do
original — `H(z) = 1 + g·A(z)`, com `g = 10^(dB/20) − 1` (o desvio em relação a 1.0, porque
`A(z)` devolve só a diferença da banda), Q 1.41 com **reforço de Q dobrado** e **corte com Q
pela metade**. São as duas tabelas de coeficientes do EQ10 que dão o timbre.

Sobre "não é cópia": matemática de filtro não é protegida, e a mesma transferência pode ser
escrita de várias formas. O que eu **não** fiz foi transladar a função deles linha a linha—
em vez disso, escrevi a mesma transferência como **um único biquad**
(`b0 = 1 + g·a0`, `b1 = −r1`, `b2 = −r2 − g·a0`, `a1 = −r1`, `a2 = −r2`), que roda na nossa
estrutura Direct Form II já existente e produz exatamente a mesma resposta. Se a tradução
tivesse sido literal (mesmas variáveis, mesma ordem, mesmo laço), aí sim seria derivação de
fato, "outra sintaxe" não salvaria.

## D-048 — Curvas salvas: uma pasta de CSV, não um formato novo

O `.eqf` não guarda a pré-amplificação (o Winamp escreve 31 fixo) e é binário. Guardar as
curvas do usuário nele seria perder justamente o dado que o nosso CSV preserva.

Decisão: **cada curva salva é um `.csv` nosso**, em `AppDataLocation/presets/<slug>.csv`, e
a lista do diálogo é o conteúdo dessa pasta, em ordem alfabética, depois das 17 embutidas.

- "Salvar curva…" pede o nome, grava (sobrescreve o mesmo slug) e já seleciona a curva.
- "Excluir" só aparece ativo quando a selecionada é do usuário; apaga o arquivo, não mexe no
  ajuste atual.
- O nome vai para a INI em `eq/name`, então reabrir o app mostra a curva com o nome que você
  deu, e o próximo "Salvar curva…" já sugere esse nome.
- Como é o mesmo CSV do compartilhamento, copiar o arquivo para outra máquina é literalmente
  copiar o arquivo.

Alternativa descartada: uma única INI com todas as curvas — funciona, mas some do formato
aberto e complica exportar só uma.

## D-049 — ReplayGain: três modos, teto pelo pico e um bug de locale no caminho

**Onde aplicar.** No estágio de ganho do motor, multiplicado pelo volume — o mesmo ponto que
o EQ usa depois. Assim funciona com gapless (cada faixa entra com o fator dela), com seek
(nada a recalcular) e com o volume da interface sem interação.

**Modos** (`Opções ▸ ReplayGain`, em `replaygain/mode`): desligado, por faixa, por álbum.
No modo álbum, se a faixa não tiver `ALBUM_GAIN`, cai no `TRACK_GAIN` em vez de não fazer nada.

**Teto anticlipping.** ReplayGain é ganho *relativo*: somar +6 dB num arquivo que já bate no
teto estoura. O padrão é limitar o fator por `1/pico` quando o arquivo declara
`REPLAYGAIN_*_PEAK`. É o que os players sérios fazem, e é por isso que o fator final não é
só `10^(dB/20)`.

**Os picos não vêm de graça.** O `PropertyMap` do TagLib expõe `REPLAYGAIN_TRACK_GAIN` mas
**não** os `REPLAYGAIN_*_PEAK` do ID3v2 — os picos só aparecem lendo os quadros `TXXX` na
mão (MP3). Em FLAC/Ogg o `PropertyMap` traz tudo. Sem esse caminho, o teto simplesmente não
existia para MP3, que é a maioria da biblioteca.

**O bug do locale (achado testando com arquivo real).** `std::strtod("0.900000")` devolve 0
quando o locale do processo usa vírgula decimal — o app roda em `pt_BR`, então "0.9" virava
"0" e o pico era descartado silenciosamente. Trocamos o `strtod` por um leitor manual
(sinal, dígitos, ponto, dígitos), independente de locale. O teste `wamp-smoke` cobre
"0,900000" e "−4,5 dB" para não voltar.

## D-050 — Curva do EQ desenhada a partir dos coeficientes, não dos sliders

A curva é a resposta **calculada** da cascata das dez bandas, com os mesmos coeficientes que
o áudio usa (`Equalizer::bandBiquad` + `Biquad::magnitudeDb`), amostrada em 220 pontos numa
escala logarítmica de 20 Hz a 20 kHz. Interpolar os pontos dos sliders teria sido mais
simples e mentiroso: em 70 Hz a banda tem Q baixo e "vaza" para 100 Hz, e o desenho precisa
mostrar isso — é o que explica por que a curva não fica em picos separados.

**O pré-amp fica fora do desenho.** Ele muda o nível, não a forma; incluir o offset fazia o
ponto de arrasto de cada banda flutuar em relação à curva. O aviso sobre a pré-amplificação
continua no texto embaixo.

**Arrasto.** Pressionar perto de um ponto (raio de 14 px) arrasta a banda na vertical, com
passo de 1 dB e limite ±12 dB — o mesmo valor dos sliders, que ficam sincronizados. O ponto
só muda de banda quando a distância horizontal coincide, então bandas próximas (12k, 14k,
16k) continuam pegáveis.

**Teste sem capturar tela.** `wamp-ui-smoke` confere que o ponto de +8 dB fica acima do de
0 dB, que as bandas estão em ordem crescente no eixo x e que arrastar o ponto da banda 3 para
cima move o slider dela. `wamp-ui-preview --equalizer` renderiza a janela inteira com
`widget.grab()`, sem tocar na tela do usuário.

## D-051 — Texto da interface não reivindica origem

A janela do equalizador dizia "As curvas são as do Winamp". Saiu. Não porque os números
tenham mudado (as curvas continuam sendo as formas que todo player usa para Rock, Pop e
companhia), mas porque uma frase dessas, dentro do produto, é uma afirmação de origem — e
afirmação de origem é justamente o que não queremos fazer sem necessidade.

Ficou: "O limitador de pico segura a saída em −0,6 dB, então a pré-amplificação comprime em
vez de estourar." Descreve o comportamento, não de onde ele veio.

Mesma limpeza no resto da interface: o tooltip do import passou a "Abre um arquivo .eqf ou um
CSV do Amplia", o filtro de arquivo virou "Equalizador (.eqf)", e o texto das preferências
sobre sorteio não cita mais o programa de referência.

**O que continua nos documentos.** O README e as decisões técnicas seguem citando
compatibilidade — "abre `.pls`, `.b4s` (Winamp 5), `.wpl`", "atalhos no estilo Winamp",
"lê o `.eqf` binário". Isso é uso nominativo: descrever com o que o arquivo é compatível, do
mesmo jeito que qualquer manual de player faz. Nenhuma frase afirma que o código é da
Nullsoft.

**Pendência consciente.** Alguns nomes de curva são bem específicos ("Full Bass & Treble",
"Laptop"). São descrições curtas e genéricas o bastante para não valerem mudança agora; se o
projeto for publicado, renomear os mais particulares é uma troca de dez strings.

## D-052 — Painel do player: sete segmentos, tempo e controle de posição

O painel (classe `wamp::ui::DisplayPanel`) ocupa o lugar do título e da barra de posição
na janela compacta. Ele é desenhado à mão, com polígonos, e não com uma fonte de display:
assim a aresta fica nítida em qualquer DPI e a espessura acompanha o tamanho do widget.

Decisões que custaram iteração:

- **Segmentos separados, com chanfro de 45°.** A primeira versão encostava as barras nos
  cantos e o topo do dígito virava um bloco só; a segunda inclinava todos os chanfros para
  o mesmo lado, e os dígitos da direita pareciam espelhados. A forma correta é um hexágono
  chanfrado por segmento, com um fio de vão até os vizinhos.
- **Casa dos minutos reservada desde o início.** O relógio mostra `0:07` com o dígito das
  dezenas apagado, mas a casa já existe: ao passar de `9:59` nada se desloca.
- **Duração no nome da faixa**, como no original: `Artista - Título (4:54)`. O relógio
  mostra o decorrido; clicar nele alterna para o restante (persistido em `play/remaining`).
- **A barra de posição é o controle**: clique e arrasto pedem posição proporcional. O
  slider separado saiu junto com a linha em que vivia.
- **Segmento apagado quase invisível** (`#111510`): com contraste maior, `0` lia como `8`.

A cor vem do âmbar do tema. Quando a F2D criar os tokens de paleta, o painel passa a ler
o esquema (é o caminho para o "Verde LCD").

## D-053 — Repetir em três estados, ícones próprios e pílulas clicáveis

O `⇄` e o `PL` ganharam companhia: um botão de repetição ao lado, com ícone desenhado à
mão. O motivo é que "repetir" não é booleano — o uso real tem três estados:

| Estado | Ícone | MPRIS `LoopStatus` |
|---|---|---|
| Desligado | laço, apagado | `None` |
| Repetir a lista | laço, aceso | `Playlist` |
| Repetir a faixa | laço com `1` | `Track` |

A tecla `R` e o menu **Opções ▸ Repetir** percorrem os três; o agente da bandeja continua
com o liga/desliga (lista), que é o caso comum. `play/repeatMode` guarda o estado; a chave
antiga `play/repeat` (booleano) é migrada na primeira leitura.

Repetir a faixa também mexeu no gapless: com esse modo ligado o motor **não** recebe
próxima faixa (`setNextTrack("")`), porque emendar a seguinte seria o oposto do pedido. No
fim, a mesma faixa é reaberta.

Ícones: `wamp/src/ui/icons.h/.cpp` desenha play, pause, stop, anterior, próxima, sorteio
(1‑2‑3 com a seta atravessando), repetir (laço com seta) e repetir-uma (laço com `1`) em
pixmap 2×, sem depender do tema do sistema — o mesmo desenho em qualquer desktop e nítido
em HiDPI. Os dígitos do sorteio usam matriz de pixels 3×5, não texto: a 20 px um texto de
5 pt vira borrão.

As pílulas `EQ`, `REP` e `RAND` do painel passaram a ser **botões**: clicar em `EQ` liga
ou desliga o equalizador, `REP` percorre os três estados e `RAND` liga o sorteio. Eram só
indicadores; viraram o caminho mais curto para o que mostram.

Ajuste de desenho: as barras horizontais dos dígitos passaram a ficar dentro da coluna dos
segmentos verticais (antes invadiam a largura do dígito e o topo parecia largo demais).

## D-054 — Transporte circular, mute e ações empilhadas ao lado do display

Pedido do usuário: botões redondos (ocupando menos largura e sobrando mais para o volume),
avançar e voltar do mesmo tamanho, botão de mute ao lado do volume, e sorteio/playlist/
repetir empilhados à direita do display, como no original.

- `wamp/src/ui/round_button.h/.cpp`: `class RoundButton final : public QAbstractButton`
  desenha o círculo com `QPainter`, em vez de `border-radius` no QSS. Motivo prático: o
  seletor por `objectName` não fechava círculo de forma confiável entre 26 e 30 px, e o
  desenho à mão garante o mesmo formato, com estados de hover, pressionado e marcado.
- Transporte: ⏮ ▶ ⏸ ■ ⏭ e **mute**, todos de 30 px; a barra de volume ficou com o resto da
  linha (`addWidget(volumeSlider_, 1)`), o que na prática dobrou a largura útil.
- Ações de estado: sorteio (1‑2‑3), repetir (laço) e `PL` empilhados à direita do display,
  em coluna de 26 px — a disposição do original, que empilha os botões de playlist.
- Os **LEDs dentro do display** mostram os mesmos glifos (`paintRepeat`/`paintShuffle` em
  `wamp/src/ui/icons.h`), acesos em âmbar e apagados em verde-escuro. Clicar neles continua
  ligando a função — são botões com desenho de LED.
- Mute: guarda o último volume em `lastVolume_`, zera e restaura; o ícone acompanha
  (alto-falante com ondas ou cortado). O `valueChanged` do volume mantém o ícone em dia,
  então arrastar até zero também aparece como mudo.

### Play com a faixa tocando volta ao início

Item novo de comportamento, pedido do usuário. `restartOnPlay_` (INI `play/restartOnPlay`,
padrão ligado) faz o botão de reproduzir chamar `engine_->seek(0)` quando o estado já é
`Playing`; do contrário chama `play()` como antes. Fica em **Preferências ▸ Reprodução**
como "Reproduzir com a faixa tocando volta ao início".

Alternativa descartada: transformar o botão em "restart" sempre — quem quer pausar/retomar
tem o botão de pausa ao lado, e o play virando "do início" é o atalho que o original oferece.

## D-055 — O display tem altura fixa e a barra de posição tem faixa própria

Dois problemas vieram do mesmo lugar: o painel de sete segmentos era um widget flexível.

- **Altura comprimida**: com a playlist aberta, o `QListWidget` reivindicava o espaço e o
  painel caía para 86 px (o `sizeHint` era 120) — a barra de posição encostava no nome da
  faixa e nos LEDs. Correção: `display_->setFixedHeight(display_->sizeHint().height())`.
  Widget desenhado à mão não negocia altura.
- **Piso da janela**: `compactHeight_` passou a ter `kCompactFloor = 240` e a ser calculado
  depois de `layout()->activate()` — `sizeHint()` de QMainWindow só é confiável com o
  layout ativado. A INI antiga (178) é promovida para o piso ao carregar.
- **Distribuição interna do painel** (120 px): linha 1 com glifo de estado, relógio e
  KBPS/KHZ/canais; linha 2 com o título e, à direita, os LEDs `EQ`, repetir e sorteio;
  linha 3, separada, só a barra de posição — que continua sendo o controle de seek.
- **Ícone de sorteio**: os dígitos 1‑2‑3 subiram para uma matriz 3×5 maior e a seta virou
  uma reta embaixo (`paintShuffle`). A curva simétrica anterior lia como um "sorriso" e os
  dígitos ficavam ilegíveis a 18 px; os botões da coluna passaram a 28 px para o glifo
  respirar.
- **Centralização dos glifos**: `RoundButton` deixou de usar `QIcon` e passou a receber a
  própria função de pintura (`setGlyph`), desenhando num quadrado centrado no círculo. Dois
  motivos: `QIcon::paint` com retângulo inteiro deslocava o desenho meio pixel, e o glifo
  precisa pintar na cor do estado (aceso/desligado) sem gerar ícone novo a cada mudança.

## D-056 — Display em três faixas, LEDs sob o relógio e letreiro no título

Pedido do usuário: tirar os LEDs da direita (onde competiam com o espaço do futuro
visualizador), alinhar `KBPS`/`KHZ`/`canal` ao topo dos dígitos, descer o título para a linha
acima da barra e fazê-lo **rolar** quando não couber, como no original.

Composição da faixa de 120 px:

| Faixa | y | Conteúdo |
|---|---|---|
| 1 | 10–54 | glifo de estado + relógio; `KBPS`/`KHZ`/canais encostados à direita |
| 2 | 56–76 | LEDs `EQ`/laço/1‑2‑3, alinhados à borda esquerda dos dígitos |
| — | 30–76 | área livre à direita, reservada para medidor e espectro (F2C) |
| 3 | 78–98 | título em largura cheia |
| 4 | 105–109 | barra de posição |

- Os LEDs passaram a ficar **sob** o relógio, formando uma coluna com os dígitos; a área da
  direita ficou contígua e sem texto no meio.
- `updateMarquee()` mede o texto com a mesma fonte do desenho e liga o `QTimer` de 33 ms
  quando ele não cabe; o desenho corre a **45 px/s** (1,5 px por quadro) com vão de 48 px antes
  de repetir. Ao trocar de faixa o deslocamento volta a zero.
- Bug de percurso: `setTrackText` devolvia cedo quando o texto era igual ao atual e o
  `updateMarquee()` só rodava no `resizeEvent` — o letreiro nunca ligava. Agora `setTrackText`
  zera o deslocamento e reavalia.

## D-057 — Artista e título em duas linhas, duração pinada

Pedido do usuário: separar artista e faixa em duas linhas que rolam de forma **independente**
(cada uma só quando não cabe), deixar a duração **pinada à direita da barra** e liberar a
altura restante para o visualizador.

| Faixa | y | Conteúdo |
|---|---|---|
| 1 | 10–54 | ▶ + relógio; `KBPS`/`KHZ`/canal à direita |
| 2 | 56–74 | LEDs `EQ`/laço/1‑2‑3 |
| 3 | 74–92 | artista (rola se não couber) |
| 4 | 92–110 | título (rola se não couber) |
| 5 | 117–121 | barra + `4:11` fixo à direita |

- `lines_[2]` (texto, largura, deslocamento, rolando) substituiu o estado único do letreiro;
  o `QTimer` de 33 ms avança só as linhas que estão rolando, a **1,5 px por quadro** (45 px/s)
  com vão de 40 px.
- A coluna de texto tem 168 px (a largura do relógio), então o bloco da direita nasce com
  ~240 × 100 px livres para o espectro da F2C.
- A duração sai do texto do título e vai para `durationRect()`: `formatClock(total)` à direita
  da barra, sempre visível. `barRect()` encolhe para terminar 8 px antes dela.
- Painel de 120 → **132 px**; piso da janela compacta 240 → **252**.
- Fallback sem tags: linha 1 vazia, linha 2 com o título (ou o caminho); nada é repetido nas
  duas linhas — o nome do arquivo já aparece na playlist e o editor de tags resolve o resto.

## D-058 — Visualizador: amostras do áudio que sai, análise na UI

Primeira metade da F2C: medidores L/R, espectro de 20 bandas e pulso de batida dentro do
display, mais o espaçamento do painel.

### De onde vêm as amostras

`wamp/src/core/src/player_engine.cpp` empurra cada bloco em `AudioAnalyzer::push()` **depois
do EQ e antes do ring** — é exatamente o sinal que o ouvinte escuta, e a thread de controle
já está com o buffer na mão (nenhuma cópia extra no caminho de áudio).

`AudioAnalyzer` guarda 4096 amostras em um anel, com um `std::mutex` curto: o `push` faz
soma mono + `memcpy` de 1024 frames; `analyze()` copia a janela mais recente e roda a FFT
**na thread da UI**, no ritmo da pintura. A thread de áudio nunca espera por desenho.

### Análise

- FFT radix-2 de 1024 pontos com janela de Hann, escrita à mão (sem dependência nova).
- 20 bandas logarítmicas de 25 Hz a 16 kHz, ±1/4 de oitava em torno de cada centro.
- Magnitude → dBFS normalizado em −70..0 dB; ataque instantâneo e queda com constante de
  0,12 s **em função do `dt`**, para 15 e 60 fps caírem na mesma velocidade.
- *Peak hold* caindo a 0,6 unidades/s.
- Medidores L/R por RMS da janela, em −60..0 dB.
- Batida: energia das três bandas graves contra a média lenta delas, com histerese de 1,35×,
  piso de 0,25 e espera de 220 ms.

### Pintura e cadência

`DisplayPanel::setVisual(frame)` desenha no bloco da direita: dois medidores de 4 px, barras
de 20 bandas com topo em âmbar claro e um ponto que acende na batida. O `MainWindow` tem um
`QTimer` de 66 ms (~15 fps, o padrão que o usuário escolheu), **parado quando a janela está
escondida ou minimizada** — o relógio do `dt` é recalculado na volta.

Menu **Opções ▸ Visualizador** liga/desliga (INI `visual/enabled`). A calibração de fps sob
demanda (10 s, p95 do custo, 40% de folga) e o slider manual ficam para a segunda metade.

### Espaçamento

O painel foi de 132 para **154 px** e as faixas ganharam ritmo constante: relógio 12–56,
LEDs 66–84, artista 94–112, título 112–130, barra 139. O piso da janela compacta subiu de
240 para **274**, o que impede o painel de ser esmagado quando a playlist abre.

### Medidores e alinhamento dos LEDs (ajuste de 2026-11-20)

- Medidores de nível passaram de 4 para **7 px**, com trilho visível, **topo segurado**
  (peak hold caindo a 0,5 unidade/s) e queda tipo limitador: sobe no ataque e desce a
  **0,9 unidade/s** (ajustado a pedido — 1,8/s piscava junto com o espectro).
- Medidores ocupam a **altura cheia do bloco** (decisão revista com o mock do usuário: em
  60% eles liam como barras curtas fora de padrão), com 14 px de respiro até a primeira
  barra e largura de **12 px** cada.
- A cor do medidor ganha **opacidade conforme o nível** (alfa 110..255): apagado no piano,
  vivo no forte. O traço de peak hold fica em alfa cheio.
- As barras do espectro têm **largura igual** (barW inteiro) e o conjunto é recuado para que
  a **última encoste na borda direita**, no mesmo eixo do fim do número da duração. A
  primeira tentativa deixava a última barra com a sobra toda (ficava mais larga que as
  outras); agora o recuo é no início, não na última.
- Os LEDs `EQ`/repetir/sorteio ficam **agrupados e centrados no relógio**: vão fixo de 12 px e
  o grupo inteiro centrado sobre a **tinta acesa** dos dígitos (a casa das dezenas existe mas
  fica apagada abaixo de 10 minutos, então contar a caixa toda puxava o grupo para a esquerda).
  Distribuí-los pela largura foi rejeitado: ficaram longe demais.
  O texto `EQ` é centralizado pela altura das maiúsculas (`capHeight`) e cada ícone tem um
  **ajuste óptico próprio** (-2 px no laço, -1,5 px no 1‑2‑3): a caixa é igual, mas a tinta
  de cada glifo não está no centro dela.

## D-059 — O visualizador olha o passado do ring, não o último bloco decodificado

Relato do usuário: cerca de 1 s de dessincronia entre o som e o espectro/medidores.

Causa: o analisador era alimentado **no decode**, e o decode corre à frente da reprodução —
o ring tem 2 s e o backpressure mantém ele quase cheio. Analisar a janela mais recente é
analisar o **futuro**: a tela mostrava o que só ia sair dali a ~1–2 s.

Correção: `AudioAnalyzer::analyze(dt, latencyFrames)` recua a janela em `latencyFrames`, e o
motor informa esse valor como **`ring.availableFrames()`** — os frames que já foram empurrados
e ainda não saíram na caixa. O histórico do analisador subiu de 2 s para **3 s** para caber o
ring cheio (2 s) com folga para a janela da FFT.

- O atraso é medido a cada quadro, então oscila com o backpressure e com o seek — não é um
  número fixo "chutado".
- A latência do dispositivo (buffer do PipeWire, ~20–50 ms) não é compensada: fica abaixo do
  limiar perceptível e exigiria consultar o relógio do servidor de áudio.
- `AMPLIA_VISUAL_LOG=1` imprime o atraso medido a cada 20 quadros (escape de depuração).

Teste em `wamp-smoke`: um tom de 1024 amostras seguido de 60000 de silêncio — sem atraso o
analisador vê silêncio, com atraso de 512 amostras encontra o tom, e atraso maior que o
histórico cai no silêncio. As três checagens existem para impedir que alguém "conserte" a
sincronia removendo o parâmetro.

## D-060 — Malha de pontos no espectro parado

Pedido do usuário: sem som o bloco do espectro virava um retângulo vazio. O visualizador do
original nunca fica vazio — tem a malha de pontos ao fundo.

Decisão: a **malha é sempre desenhada** (20 colunas, um ponto de 2 px a cada 7 px, cor
`#1e261b`), e as barras entram por cima quando há sinal. Assim:

- o bloco parado tem textura e comunica "aqui é o espectro", sem precisar de caixa ou rótulo;
- o bloco tocando ganha uma escala visível, o que ajuda a ler a altura das bandas;
- zero estados especiais no código — não existe caminho "sem áudio" separado.

## D-061 — Analisador de desempenho e taxa do visualizador em Preferências

Pedido do usuário: o analisador de desempenho/calibração de fps podia morar dentro de
Preferências, em vez de um menu ou janela própria.

Concordo, e por três motivos: (a) é **configuração**, não ação do dia a dia — o menu de opções
serve para o que se alterna enquanto se ouve; (b) quem ajusta fps quer ver o custo junto, e a
aba mostra os três números na mesma linha; (c) a F2D (esquema de cores) já vai precisar de uma
aba de aparência, e Preferências é o lugar onde essas escolhas se acumulam.

Aba **Visualização**:
- `Mostrar espectro e medidores` (equivale ao menu **Opções ▸ Visualizador**, mesma chave
  `visual/enabled`);
- `Quadros por segundo` — slider 10–60, aplicado na hora (`visualTimer_->setInterval`);
- `Calibrar…` — mede ~10 s e mostra `Medido: X ms por quadro, Y% de um núcleo a Z fps`.

Como a calibração mede: alimenta um `AudioAnalyzer` local com um tom, desenha o painel de
verdade num `QPixmap` (`display_->render`) quadro a quadro e mede o custo de cada desenho.
Descarta o **primeiro segundo** (primeira pintura e cache de fonte não representam o regime) e
usa o **p95** das amostras. O fps escolhido é `clamp(600 / p95, 10, 60)` — ou seja, o desenho
pode ocupar no máximo **60% do orçamento de um quadro** (40% de folga). O CPU% é
`p95 × fps / 10`, a fração de um núcleo.

Chaves novas na INI: `visual/enabled`, `visual/fps`, `visual/fpsCostP95Ms`, `visual/fpsCpuPercent`
(as duas últimas só registram a medição, para não recalibrar toda vez). Escapes de QA:
`AMPLIA_CALIBRATE=1` roda a calibração no start e `AMPLIA_CALIBRATE_LOG=1` imprime o resultado.

Medição na máquina do usuário: **p95 = 0,29 ms** → 60 fps custam ~1,7% de um núcleo, muito
abaixo do padrão de 15 fps.

Junto: a nota da regra de scrobble saiu do rodapé do diálogo e voltou para dentro da aba
**Scrobbling** — ela aparecia em todas as abas.

### Medidores: sem trilho, mesma silhueta das barras (ajuste final)

Três tentativas, registradas porque cada uma explica a anterior:

1. **Altura cheia com trilho de 12 px** — a coluna escura até o topo desenha uma barra visível
   mesmo com nível baixo; parecia um "poste" mais alto que o espectro.
2. **78% de altura** — encurtou a coluna e inverteu o problema: o medidor ficou abaixo das
   barras altas.
3. **Altura cheia, trilho de 2 px** — o fio ainda subia até o topo e continuava lendo como
   "medidor mais alto" (foi o que o usuário destacou em amarelo).

Decisão: **não há trilho**. O medidor é só a coluna de nível (12 px de largura) com o topo
segurado (2 px mais claro), apoiada na mesma base e com o mesmo teto de 0 dBFS das barras.
Sem coluna de fundo, a silhueta do medidor é a mesma das barras — e o que se compara é nível
com nível, não desenho com desenho.

Consequência aceita: no silêncio os medidores desaparecem junto com as barras — por isso a
malha de pontos passou a cobrir **a área inteira** (medidores incluídos), num passo único, e
as letras **L** e **R** aparecem na base das colunas **só quando o nível está em zero**. Com
música tocando as letras sumiriam dentro da barra acesa, então elas existem exatamente para o
estado em que não há nada para indicar.

### Ajustes finais do visualizador (stop, malha e vão)

Três correções pedidas depois de usar:

1. **No stop, as barras descem completamente.** Dois motivos somados: o `PlayerEngine::stop()`
   e o `load()` limpavam o ring e o equalizador, mas **não** o analisador — a janela de 3 s
   continuava com o último áudio e o espectro congelava; e, no fim da faixa, o `Idle` mantinha
   a última janela visível. Agora o analisador é resetado em `load()`/`stop()`, e o tique do
   visualizador só analisa quando o estado é `Playing` (fora disso, quadro vazio) — o que
   também resolve o pause e o fim da faixa.
2. **A malha estava à direita das barras.** Os pontos eram desenhados num laço próprio, com
   origem e passo próprios; as barras começam em `barsStart` e têm passo `barW`. Agora cada
   fileira de pontos é calculada como `barsStart + i * barW + (barW - 2) / 2` — o centro da
   barra correspondente — e o mesmo vale para os medidores.
3. **Pontos no vão entre medidores e espectro.** O campo contínuo enchia o espaço reservado
   entre os dois blocos; a impressão era de colunas que "não estavam sendo desenhadas". O vão
   ficou limpo: só medidores e bandas têm fileira de pontos.

### Pausa congela, stop desvanece, L/R no topo

Pedido do usuário, e as três fazem sentido por motivos diferentes:

- **Pausa congela.** Pausa é um estado que se retoma: a música volta dali a instantes. Decair
  ali daria a informação errada ("acabou"). O painel guarda o último quadro bom (`lastVisual_`)
  e o redesenha enquanto o estado for `Paused`.
- **Stop desvanece.** Corte seco parece pane de render, não fim de música. O quadro decai
  exponencialmente com constante de **0,25 s** (`exp(-dt / 0.25)` em bandas, topos e medidores)
  e, quando tudo passa de 1%, vira quadro vazio — a mesma queda de um VU analógico.
- **"L" e "R" no topo.** Embaixo, a letra ficava coberta pela barra acesa; agora ficam no topo
  da coluna, desenhadas **depois** dos medidores. Ali o nível só chega em 0 dBFS, então o
  rótulo fica visível o tempo todo sem sujar o nível.

O fim da faixa (`Idle` por EOF) usa o mesmo desvanecimento do stop — para quem ouve, é o mesmo
acontecimento.

### Medidores acompanham a largura da janela

Sintoma relatado: alargando a janela, o espectro se redistribui e os medidores ficam do mesmo
tamanho — dois fios perdidos à esquerda de barras largas.

Causa: os medidores tinham largura **fixa** (12 px) enquanto as barras são calculadas como
`barsSpace / kBands`, ou seja, proporcionais. Numa janela de 760 px a área do espectro passa de
196 para ~538 px e as barras vão a 23 px, mas os medidores continuavam em 12.

Correção: largura proporcional com teto e piso — `clamp(area.width() / 16, 8, 22)` — e o vão
entre eles também cresce (`max(3, meterW / 4)`). O `barsLeft` já derivava de `meterW`, então o
espectro se ajusta sozinho.

## D-062 — Skins: só as clássicas (`.wsz`), como modo de UI (fase F7)

Alinhado antes de começar a F2D, para não descobrir depois que a F5 custava o triplo.

- **Escopo**: skins clássicas do Winamp 2.x, carregadas de arquivo local. Modernas
  (`.wal`) ficam fora — são XML + Maki, um motor de interface inteiro; o autor do Webamp
  tentou e documentou por que não fecha.
- **Acervo**: o Winamp Skin Museum serve `.wsz` originais (100 mil+), então qualquer skin
  de lá carrega no nosso renderizador. Não redistribuímos skin nenhuma.
- **Arquitetura**: skin é um modo alternativo de desenhar a janela, com bitmaps e regiões
  de clique; o layout moderno (widgets) fica desativado nesse modo. Não existe "aplicar
  skin" a widgets Qt.
- **Pré-requisitos que a F2D já paga**: tokens de cor centralizados, desenho separado do
  layout e nenhuma geometria do app amarrada ao motor de áudio.
- **Ordem sugerida**: depois da F3 (biblioteca) — a biblioteca é o que faz o app ser usado;
  a skin é o que faz ele ser mostrado. Se a prioridade virar portfólio, a F5a sobe.
- **Limite de plataforma**: no Wayland não há como posicionar as três janelas coladas; a
  alternativa é desenhar tudo numa janela só (menos fiel).

### Renumeração (20/11)

- **F5 — Atalhos do Winamp**: paridade de teclado com o 2.x. Barata, aparece a cada uso e
  não depende de nenhuma outra fase.
- **F6 — Busca de tags**: em pausa, como já estava.
- **F7 — Skins clássicas**: era F5; desceu para deixar espaço a necessidades que possam
  aparecer antes de uma frente do tamanho de um renderizador de skin.

## D-063 — Nome: Amplia (era WAmp)

Decidido em 20/11, depois de passar por **NeWAmp** e **NeoAmp** (que tem dois players
Winamp-style e um amplificador de fone com o mesmo nome) e **WAmp** (evoca a Winamp de
perto demais para publicar).

- **Amplia** é verbo em português e espanhol (amplificar), a raiz `ampli-` atravessa
  idiomas, pronuncia-se sem ambiguidade ("am-PLEE-a") e não contém "win", "wamp" nem "amp".
- No nicho de players o nome está **livre**; a colisão conhecida é a **Lacuna Amplia**
  (identidade digital, com SDK `amplia-client` no npm) e uma org de IoT — o que, para uso
  privado, não muda nada.
- Identidade trocada: `io.github.cassiofiorin.Amplia`, binário `amplia`, MPRIS
  `org.mpris.MediaPlayer2.amplia`, ícone/desktop renomeados, INI em
  `~/.config/amplia/amplia.ini`, dados em `…/data/cassiofiorin/Amplia`.
- Ferramentas: `amplia-smoke`, `amplia-ui-smoke`, `amplia-ui-preview`, `amplia-tray-probe`
  (os alvos do CMake seguem `wamp*`; só o `OUTPUT_NAME` mudou, para não reescrever 5 mil
  linhas de namespace de uma vez).
- Escapes de depuração renomeados de `WAMP_*` para `AMPLIA_*`.
- **Ícone**: raio (áudio) + "+" (soma com a amplificação), sobre o quadrado escuro; o "+"
  em âmbar claro para não competir com o raio.
- Migração: INI, `library.db` e presets copiados do diretório do app antigo.

## D-064 — Esquema de cores por tokens (`Theme`), não por stylesheet solto

- `src/ui/theme.h/.cpp`: `struct Theme` com três grupos — moldura (fundo, borda, controles,
  texto, acento, groove, handle), lista (fundo, texto, seleção, número) e display (topo/base,
  aceso, apagado, off, rótulo, valor, malha, barra, topo da barra).
- `activeTheme()` é global e os widgets que se pintam à mão (`DisplayPanel`, `RoundButton`,
  `PlaylistDelegate`, `EqCurveView`) leem dela. O stylesheet é **gerado** por
  `styleSheet(theme)` e aplicado em `qApp` — assim diálogos e menus herdam sem cada um
  precisar de uma cópia.
- Troca de esquema: `setTheme(id)` grava `appearance/theme`, reaplica a folha e dá `update()`
  em todos os widgets (o stylesheet sozinho não repinta quem desenha com `QPainter`).
- Cinco presets: Grafite (padrão, o visual anterior), Verde LCD, Âmbar, Azul aço e Papel
  (claro). Nomes descrevem cor, não skin de origem — mesma regra da D-051.
- **Não** implementado ainda: "Seguir o sistema" e a verificação automática de contraste
  4,5:1. Ficaram no roadmap da F2D.

### Ajustes de contraste e alinhamento (20/11)

- **Display escuro recalibrado.** Fundo passou a preto de verdade (`#050505`→`#000`), o
  segmento **apagado** virou cinza neutro (`#262626`-ish) e o acento ficou só no que está
  aceso. Motivo: em LCD/CRT de matriz de pontos o segmento não desenhado **não tem cor** —
  ele é o vidro. Escurecer o próprio acento produzia "LED verde", não LCD. Valeu para
  Grafite, Verde LCD, Âmbar e Azul aço (Papel é claro e mantém o seu).
- **"L" e "R" subiram** para a linha de `KBPS`/`KHZ`: em cima do medidor, a letra ficava
  embolada com a coluna acesa dependendo da cor. Ali usam a mesma tipografia das outras
  informações e nunca encostam no nível.
- **Rótulos do EQ centrados pelo groove.** O `QLabel` do valor ocupava a largura da coluna
  (35 px) enquanto o slider tinha 26 px fixos e ficava à esquerda — o centro do texto caía
  5 px à direita do centro da barra. Correção: os rótulos de valor e de frequência têm a
  mesma largura do slider (26 px), então os três centros coincidem.
- **Sorteio agrupado** em Preferências: caixa, nível e explicação vivem num bloco indentado,
  deixando claro que o slider depende da caixa.

- **Sorteio virou `QGroupBox`.** O bloco tem moldura e título próprios, fica por último na aba
  Reprodução (depois de "fechar para a bandeja" e "play volta ao início") e desabilita
  inteiro quando a caixa está desligada. O texto da caixa virou "Ativar sorteio" para não
  repetir o título do grupo.
- **Lista preta nos temas escuros.** No Verde LCD o fundo da playlist era um verde quase
  preto; preto puro aumenta o contraste do texto claro e separa a lista do painel.

## D-065 — Misturador de cores: quatro bases e um contraste

- **Entradas**: fundo, destaque, secundária e **cor dos botões**, mais um **contraste global**
  (80–140%). O usuário mexeu naquelas quatro cores; todo o resto (33 tokens) é **derivado**.
- Derivação (`makeCustom`, em `src/ui/theme.cpp`): do fundo saem superfícies, bordas, trilhos
  e a lista (mais escura que a janela); do destaque saem display aceso, barras, seleção e
  controles marcados; da secundária saem texto, valores, rótulos e o cinza dos segmentos
  apagados; da cor de botões saem os três estados (normal, hover e pressionado). O contraste
  multiplica as distâncias de luminosidade (borda, trilho, texto fraco), sem tocar no matiz.
- **Texto sobre destaque é automático**: `readableTextOn()` escolhe preto ou branco pelo luma.
  É a única decisão que não fica na mão do usuário — sem ela, uma seleção clara com texto
  claro ficaria ilegível.
- **Editor** (`src/ui/theme_editor.{h,cpp}`): quatro amostras clicáveis (abrem o `QColorDialog`
  do Qt, **não** o nativo, para ter roda/HSV, hex e conta-gotas iguais em qualquer desktop),
  slider de contraste e "Partir de" um esquema existente. Cada mudança emite
  `previewChanged` e a janela **atrás** do diálogo repinta na hora; `Cancelar` restaura o
  esquema que estava ativo.
- Persistência: `appearance/theme=personalizado` + `customBg`, `customAccent`,
  `customSecondary`, `customButtons` (hex) e `customContrast`.
- Menu **Aparência**: presets, **Personalizado**, separador e **Editor de cores…**; o item
  marcado é sincronizado por `syncThemeChecks()` também na carga inicial.
- **Pendente**: aviso de contraste baixo (o texto de alerta quando fundo e botões ficam
  quase iguais) e o CSV de importação/exportação — desenhados, ainda não implementados.

## D-066 — Temas personalizados em CSV (um arquivo por tema)

- **Formato**: `id,nome,fundo,destaque,secundaria,botoes,contraste`, uma linha por tema, cores
  em `#RRGGBB`. Lê de volta construindo por `makeCustom` — o resto da paleta é derivado, então
  o CSV carrega quatro cores e um número, não 33.
- **Armazenamento**: `presets/themes/<id>.csv` em `AppDataLocation`, **um arquivo por tema**.
  O arquivo manda no id (`themeSlug(nome)`), assim renomear o arquivo renomeia o tema e apagar
  um não força reescrever os outros.
- **Limites**: 30 personalizados na aplicação e 30 linhas por arquivo importado. Ao encher, a
  importação é recusada inteira com mensagem — nada é truncado nem sobrescrito em silêncio.
  Linha inválida (hex quebrado, contraste fora de 80–140, campos faltando) é ignorada e vira
  aviso numerado; linha começando com `#` é comentário e não gera aviso.
- **Modelo CSV**: cabeçalho + uma linha de exemplo comentada, para servir de molde a outras
  ferramentas (inclusive geração por IA) sem que a linha de exemplo seja importada.
- **Fluxo completo**: editor de cores → **Salvar tema atual como…** (nome + arquivo) →
  exportar CSV → outra máquina → importar CSV. Sem o passo do nome, uma paleta do editor não
  teria arquivo para exportar; foi o buraco que a implementação fechou.
- `themeToCsvRow` grava as **bases** guardadas em `Theme::custom*` (não tenta "desderivar" as
  cores finais). Para temas embutidos usados como ponto de partida, a exportação usa os tokens
  equivalentes (fundo, destaque, texto, botões) com contraste 100.

## D-067 — Aparência: uma janela, sem menu suspenso; mais o tema Clássico

- **Menu**: `Aparência` deixou de ser um menu de topo. A barra voltou a três itens e a entrada
  vive em **Opções ▸ Aparência…** (`Ctrl+,`). O menu antigo misturava escolha (lista de rádio)
  com administração de arquivos (salvar/importar/exportar), e um menu suspenso não tem onde
  mostrar *como* um esquema é.
- **Janela** (`ThemeDialog`): lista à esquerda — Embutidos, Meus temas, Personalizado — e o
  editor de quatro cores à direita. Rodapé: Salvar como… · Importar/Exportar CSV… · Modelo
  CSV… · Excluir · Reverter · Fechar.
- **Aplica na hora**, sem OK/Cancelar: escolher na lista troca o esquema, mexer numa cor vira
  `Personalizado`, e **Reverter** devolve o esquema que estava ativo quando a janela abriu.
  Fechar mantém o que está na tela (e grava a paleta editada).
- **Excluir** pede confirmação, só vale para tema salvo (presets e "Personalizado" ficam
  desabilitados) e apaga o CSV do tema.
- **Contraste** passou a mexer no que se vê: o texto vai em direção a branco/preto conforme o
  valor (80% aproxima do fundo, 140% afasta), e não só bordas e trilhos. Antes a variação era
  de 0,22 → 0,31 na mistura da borda — invisível na prática.
- **Lista mais preta**: em tema escuro o fundo da playlist agora mistura 60% com preto (era
  35%), recuperando a leitura "playlist preta" do clássico.
- **Tema Clássico** (`makeClassic`): painéis azul-acinzentados, display preto com verde forte,
  playlist preta com texto verde e seleção azul — a leitura da skin base do original, feita
  só de contraste (sem relevo). Passa a ser o segundo item da lista de embutidos.

## D-068 — F2D: cada elemento é uma janela auxiliar

- **Playlist** deixou de ser painel da janela principal: virou `QWidget(this, Qt::Tool)` com
  título próprio. Consequências que motivaram a mudança:
  - o botão PL **não redimensiona mais a principal** — era isso que fazia a janela inteira
    se mexer no Wayland, onde o compositor decide posição e o crescimento obrigava o KWin a
    reencaixar a janela na área visível;
  - a auxiliar **não entra na barra de tarefas nem no Alt+Tab** (janela transiente do pai),
    então alternar entre player e playlist é um clique, não uma troca de aplicativo;
  - o espaço da lista dobra de altura sem espremer o display.
- **Equalizador** virou janela auxiliar também (`setWindowFlag(Qt::Tool)`), pelo mesmo motivo
  e porque é ele que a skin clássica desenha como janela `eqmain` na F7.
- Persistência: `window/playlistWidth` e `window/playlistHeight` (tamanho). No Wayland a
  posição é do compositor e não é salva; no X11 há espaço para guardar posição depois.
- Fechar a principal fecha a auxiliar (`closeEvent`); fechar a auxiliar só esconde e o botão
  PL desmarca (`eventFilter` no evento `Close`).
- A **Biblioteca (F3)** nasce no mesmo molde: janela auxiliar, sem entrada na barra, com
  tamanho salvo.

## D-069 — Wayland é o caminho; X11 é escape documentado

- O manifest entrega **os dois sockets** (`--socket=wayland` e `--socket=x11`). O app inicia
  em **Wayland quando existe** (verificado com `QT_DEBUG_PLUGINS=1`: carrega `libqwayland.so`
  com `DISPLAY=:0` e `WAYLAND_DISPLAY=wayland-1` presentes) e aceita
  `QT_QPA_PLATFORM=xcb` como escape para diagnóstico.
- **Não forçamos X11**: no Wayland o app é nativo (escala fracionária, latência direta); no
  XWayland ele ganharia acoplamento de janelas ao custo de tradução e de depender de um
  componente que as distros começam a reduzir.
- **Acoplamento de janelas fica parado** como **F2D.1** (só X11): trilha por delta, âncora
  eleita no início do arraste, seguidoras com *offset* preservado, encaixe a 8 px e
  desencaixe a 40 px, trava de reentrância e coalescência de `Move` em ~16 ms.
- Consequência aceita: as janelas do Amplia vivem **desacopladas** por enquanto. É a única
  limitação de produto que o Wayland nos impõe hoje.

## D-070 — Multiplataforma como direção, não como fase

O autor declarou a intenção futura de um player **multiplataforma** (Windows primeiro), com a
proposta de "retrofit": interface atual, personalizável, áudio de qualidade — não a
reconstrução pixel-a-pixel que outros projetos fazem.

Para que isso não custe retrabalho depois, valem desde já cinco regras:

1. **Nenhuma chamada POSIX no core**: `statvfs`, `::access`, `unistd.h`, sockets abstratos e
   sd-bus vivem atrás de interfaces (`IAudioOutput`, `MprisServer`, checagem de escrita), com
   `#ifdef` concentrado — nunca espalhado.
2. **Caminhos por `QStandardPaths`** e nenhuma string `XDG_*` hardcoded no core.
3. **FFmpeg em modo LGPL** no empacotamento, para o binário poder ser distribuído sem herdar
   GPL — decisão que precisa ser tomada antes do primeiro executável compartilhado.
4. **Interface independente de DPI**: já é vetorial (Qt), mas skins `.wsz` (F7) são bitmaps e
   vão exigir escala por inteiro ou *nearest neighbor* — problema do futuro, registrado.
5. **Licença do produto**: distribuir para amigos é "conveying" do **nosso** binário; o que
   continua fora de alcance é a marca e os artefatos da Nullsoft (D-007, D-046).

Estado: Windows entra no roadmap como **avaliado, não planejado**; a ordem segue
F5 (atalhos) → F7 (skins) na plataforma atual.

## D-071 — F5 camada A: teclas do original, sem inventar combinações

Antes de implementar, contei os atalhos do fonte de referência: **193 entradas** em quatro
tabelas de aceleradores (`Src/Winamp/Winamp.rc:1888`, `:1937`, `:2016`, `:2067`), que viram
**127 combinações únicas** e **119 ações**; a Biblioteca de Mídia soma mais ~120, e o
`gen_hotkeys` registra **15 atalhos globais** editáveis (`gen_hotkeys.cpp:49`).

Decisões que vieram da contagem:

- **Não copiamos os 193.** Metade depende de janelas acopladas, skins ou plugins que não
  temos; e muitos são duplicatas (teclado numérico e letra para a mesma ação).
- **Nada de dígitos para volume**: no original `1`–`0` fazem outra coisa (`3` abre o editor
  de ID3). Volume é `↑`/`↓`, como lá.
- **`Ctrl+T`**, não `T`, para alternar decorrido/restante (`Winamp.rc:2085`).
- **`Num. 1` e `Num. 3`** pulam dez faixas (`Winamp.rc:1921`); como laptop sem teclado
  numérico é maioria, `Ctrl+←`/`Ctrl+→` fazem o mesmo.
- **Tamanho do texto da playlist** aceita `Ctrl+=`, `Ctrl+-`, `Ctrl+Shift+=`, `Ctrl+Shift+-`
  e as teclas do teclado numérico (`KeypadPlus`/`KeypadMinus`, que o Qt entrega com
  `Qt::KeypadModifier` e não com `Shift`) — o "+" do numérico ficava de fora porque só
  `Ctrl+Shift+=` estava registrado. Reset em `Ctrl+Shift+0`,
  entre 9 e 22 px, salvo em `playlist/fontPx`. O QSS deixou de fixar `font-size` na lista
  (era ele que impedia qualquer ajuste), e a fonte passa a ser definida em código.
- Atalhos registrados como `QAction` com `Qt::WindowShortcut` na janela principal: valem
  quando o foco está nela, e **não roubam as setas** da playlist, que agora é outra janela.

## D-072 — Idioma como arquivos, não como código

- **Fonte em português**, tradução por arquivos `.ts` (XML do Qt Linguist) — um por locale em
  `i18n/`. Quem quiser contribuir abre um PR só com o `.ts`; `lrelease` gera o `.qm` no build.
- Dois caminhos de entrega:
  1. **embutido** — `qt_add_translations` compila pt/en/es no Flatpak;
  2. **avulso** — no Linux, `.qm` colocado em `~/.local/share/amplia/i18n/` é carregado no
     start. Isso permite traduzir sem recompilar e testar na hora; no Windows, `%APPDATA%`.
- **Sem reinício** fica fora de escopo por ora: trocar idioma pede recriar menus e diálogos
  abertos; reiniciar é mais barato e não mente para o usuário.
- Regras que entram já (mesmo antes da F6): toda string nova nasce em `tr()`; contagens usam
  `%n` (`tr("%n faixa(s)", "", n)`) — russo tem três formas plurais e concatenação quebra a
  tradução; nada de montar frase por pedaços.
- Limites conhecidos: RTL (árabe/hebraico) exigiria espelhamento de layout, fora de escopo;
  rótulos de largura fixa do display (KBPS/ESTÉREO) precisam ser conferidos idioma a idioma.
- Fluxo de tradução: PR com `.ts` (ou Weblate, que entende `.ts` nativamente) + checagem de
  que `lrelease` compila e que nenhum `<translation>` ficou malformado.

## D-073 — Termos universais no display; o resto é interface

O display fala a língua do hardware, não a do país. Ficam **fora da tradução** e são escritos
com `QStringLiteral` (nunca `tr()`), com comentário no código explicando o porquê:

| Termo | Situação |
|---|---|
| `KBPS`, `KHZ` | unidades — universais |
| `STEREO` / `MONO` | **termo de áudio**, não tradução: equipamento no mundo inteiro estampa STEREO. Em português é "estéreo", mas o rótulo do display segue o padrão do equipamento |
| `EQ`, `L` / `R` | universais |
| `dB`, `Hz`, `kHz` | unidades |
| `PL` | abreviação herdada do editor de playlist; mantida como marca visual do painel |

Consequências:

- Menos superfície de tradução na F6: o painel não precisa de arquivo de idioma.
- Layout do painel é calculado por `QFontMetrics`, então a troca de `ESTÉREO` por `STEREO`
  (7 → 6 caracteres) só desloca a linha de informações, sem quebrar o desenho.
- O texto **da interface** (menus, diálogos, barra de status, tooltips) continua em português
  até a F6; a distinção é: display = hardware, interface = idioma.

## D-074 — F5 camadas B e C: o que o original tem e o que não fazia sentido copiar

- **Camada B (playlist)**: navegação `Home`/`End`/`PgUp`/`PgDn` e setas já vinha do
  `QListWidget`; acrescentamos **`Space` = mostrar a faixa tocando** e **`Ctrl+A` = selecionar
  tudo na janela da playlist**. As ações de edição (selecionar nada, inverter, remover,
  manter só a seleção, limpar, remover inexistentes, ordenar por título/arquivo/caminho,
  inverter, embaralhar) já existiam nos menus do rodapé desde a F1.
- **Camada C (janelas)**: `Alt+W`/`Alt+E`/`Alt+G` focam player/playlist/equalizador
  (mostrando quando preciso), `Ctrl+Tab` alterna entre as janelas visíveis, `Ctrl+A` no
  player é **sempre no topo** e `Ctrl+E` liga o **arrastar de qualquer ponto**.
- **Mesma tecla, significados diferentes por janela** é intencional: `Ctrl+A` no player é
  "sempre no topo" e na playlist é "selecionar tudo" — é o que o original faz, e as janelas
  são independentes; `Space` no player é play/pause e na playlist é "voltar para a tocando".
- **`Shift+R` — parar depois desta faixa**: flag que também **desliga o prefetch do gapless**
  (`updateNextUp()` devolve sem próxima quando a flag está ligada); `onTrackEnded()` para,
  limpa a flag e avisa. Sem isso, o gapless emendaria a próxima antes da regra valer.
- **`Ctrl+L` — abrir URL**: aceita `http`, `https`, `mms`, `mmsh`, `rtsp`, `rtmp`. URL entra
  na playlist **sem** passar pela varredura de pastas nem pela fila de metadados (sondar
  rede por item atrasaria a lista); o FFmpeg abre o stream direto.
- **`Ctrl+E` — arrastar de qualquer ponto**: `QWidget::windowHandle()->startSystemMove()` no
  painel do display e no fundo da janela — botões, sliders e lista ficam de fora para não
  roubar o clique. No Wayland é o único caminho possível (o cliente não escolhe posição).
- Persistência: `window/alwaysOnTop`, `window/easyMove`, `playlist/fontPx`. Flags restauradas
  em `loadSettings()`, com os `QAction` sincronizados.

## D-075 — F6 dividida em infraestrutura, tradução e documentação

Traduzir a interface e traduzir o README são trabalhos de naturezas diferentes e foram
separados:

- **F6a (infraestrutura)**: `tr()`, `.ts`, `lrelease`, `QTranslator`, seletor nas Preferências.
  É invisível para o usuário e não muda nenhuma frase na tela.
- **F6b (tradução)**: preencher `amplia_en.ts` e revisar layout por idioma. Precisa da F6a.
- **F6c (documentação pública)**: `README.md` em inglês + `README.pt-BR.md`, `NOTICE` de
  terceiros e o aviso de não-afiliação. **Não é tradução**: é um documento para outro leitor
  (quem baixa o app), e depende da decisão de publicar.

Racional: a F6a é pré-requisito técnico de tudo e deve vir antes de novas telas (F7); a F6b é
conteúdo; a F6c é comunicação e pode esperar o primeiro pacote compartilhado. Misturar as três
numa fase só faria a fase parecer "quase pronta" por semanas.

## D-076 — F6a: infraestrutura de idioma

- **Idioma-fonte é o português.** As strings vivem em `tr()` já em português; não existe
  `.ts` de português. O inglês é tradução, em `i18n/amplia_en.ts`.
- **Carga antes da janela.** `wamp::ui::installTranslators()` roda no início do `main()`,
  antes de qualquer widget: o Qt resolve `tr()` na construção do objeto, não no `show()`.
  O idioma vem da INI (`general/language`, lido por `wamp::ui::languageFromSettings()`, que
  não depende da janela existir).
- **Dois caminhos de `.qm`**, nesta ordem: `AppDataLocation/i18n/amplia_<code>.qm` (tradução
  sem recompilar, o caminho de quem contribui) e `:/i18n/amplia_<code>.qm` (embutido pelo
  `lrelease` no build).
- **`Sistema`** resolve por `QLocale::system()`; se for português, não carrega nada (fonte).
  Escape `AMPLIA_LANG` força um idioma em testes sem mexer na INI.
- **Troca exige reabrir o app** — retraduzir ao vivo (`LanguageChange`) é possível, mas cada
  widget precisa reimplementar `changeEvent`; não vale o custo agora. A caixa avisa.
- **O display não é traduzido** (D-073): `KBPS`, `KHZ`, `STEREO`/`MONO`, `EQ`, `L`/`R`, `dB`
  ficam `QStringLiteral`. `ESTÉREO` virou `STEREO` justamente para não depender do idioma.
- `lupdate` roda à mão (binário do SDK) para gerar/atualizar o `.ts`; o build roda `lrelease`
  e embute o `.qm` nos três alvos que desenham telas (`amplia`, `amplia-ui-preview`,
  `amplia-ui-smoke`).
- Testes: `QLocale::setDefault(pt_BR)` no começo do smoke (as asserções são em português) e
  duas checagens novas — o `.qm` traduz `&Arquivo` → `&File` e o idioma-fonte não é alterado.

## D-077 — "Reiniciar agora" recria a janela no mesmo processo

Trocar idioma exige retraduzir tudo, e o Qt não retraduz widgets que já nasceram. Em vez de
abrir um segundo processo (impossível dentro do Flatpak sem `flatpak-spawn`, além da corrida
com o socket de instância única), o app **recria a `MainWindow` no mesmo processo**:

1. a janela atual grava a INI (`saveSettingsNow()`);
2. `installTranslators()` carrega o `.qm` novo;
3. uma `MainWindow` nova nasce e é mostrada (restaura a playlist da INI, em pausa);
4. a antiga é destruída com `deleteLater()`.

A posse fica num `std::shared_ptr<std::unique_ptr<MainWindow>>` no `main()`, então os lambdas
de instância única e de linha de comando continuam apontando para a janela viva.

**Custo aceito pelo usuário**: a reprodução para. A playlist, o tema, o volume e as
preferências voltam; a posição da faixa volta para o começo, em pausa (a sessão não guarda
posição — D-029). Se um dia a posição importar, o caminho é extrair o estado do player para
fora da janela (`PlayerSession`), o que também destrava a F3 (biblioteca) e a F7 (skins).

Fluxo na interface: ao confirmar o idioma, `QMessageBox` com **Reiniciar agora** (padrão) e
**Na próxima**; o primeiro chama `saveSettingsNow()` + `restartHandler_()` e **retorna sem
tocar em membros da janela** — ela será destruída durante a chamada.

Escape de teste: `AMPLIA_RESTART_TEST=<idioma>` exercita o caminho 1,2 s depois de abrir,
sem diálogo; usado para validar que a interface volta em inglês e que o processo não cai.

## D-078 — F6b: tradução do inglês

- `i18n/amplia_en.ts` passou de 0 para **317 mensagens**, nenhuma `unfinished`: 268 traduzidas
  em três lotes e 49 que apareceram depois (rótulos curtos que a heurística da conversão
  automática pulou, mais as strings extraídas depois de corrigir o diálogo de temas).
- **Rótulos em `tr()` com literal, nunca com variável.** O diálogo de temas montava as linhas
  com `QString::fromUtf8(item.label)`; `tr()` em tempo de execução com variável não entra no
  `.ts` e não é traduzível. As quatro linhas viraram literais explícitos.
- **Nomes de esquema** (`Grafite`, `Clássico`, …) são dados em `Theme::name`, não `tr()`. Para
  traduzi-los sem duplicar fonte de verdade, `localizedThemeName(id)` guarda os literais num
  único lugar (contexto `Theme`) e a lista/combo chamam essa função.
- **Funções livres** não têm `tr()`: `warnAboutNetworkDrop` usa
  `QCoreApplication::translate("MainWindow", …)`; o mesmo vale para `theme.cpp`/`theme_store.cpp`
  (contexto `Theme`).
- `CONTRIBUTING-TRANSLATIONS.md` descreve o fluxo completo (lupdate → Linguist → lrelease →
  `.qm` avulso em `AppDataLocation/i18n`), as regras (não traduzir termos do display, preservar
  `%1`/`&`/`▸`) e o teste por `AMPLIA_LANG`.
- QA visual em `AMPLIA_LANG=en`: menus `File / Playback / Options`, janela de temas inteira em
  inglês (`Built-in`, `Graphite`, `Classic`, `LCD Green`, `Amber`, `Steel Blue`, `Paper`,
  `Custom`, `Background/Accent/Secondary/Buttons`, `Start from`), equalizador
  (`Enabled`, `Presets…`, `Import .eqf/CSV…`, `Export CSV…`, `Save curve…`, `Delete`, `Reset`,
  `Curve`, `Close`, rodapé do limitador).

## D-079 — Licença: GPL-3.0-or-later com licença comercial sob consulta

O projeto nasceu privado. Com a F6c (documentação pública) entrou a pergunta que não dava
mais para adiar: sob que licença o código sai.

Descartado: **CC BY-NC-SA 4.0** (não foi escrita para software — sem cláusula de código-fonte,
"não comercial" ambíguo, e o *share-alike* não conversa com o ecossistema de bibliotecas);
**MIT/Apache-2.0** (máxima adoção, mas permitem fork fechado — o oposto do que o autor quer);
**PolyForm Noncommercial** (proíbe comércio de verdade, mas sai do Flathub sem revisão manual,
não entra em distro nenhuma, afasta contribuição e a proteção é difícil de exercer).

Escolhido: **GPL-3.0-or-later** (`COPYING`, texto canônico verbatim) **+ licença comercial sob
consulta** (`NOTICE`). O raciocínio:

- Uso livre continua livre — inclusive comercial — mas derivado distribuído permanece aberto.
- Quem precisa de produto fechado (o uso comercial que incomodava) tem um caminho legítimo:
  negociar. Na prática, ninguém monta produto fechado sobre GPL sem falar com o autor.
- Distros e Flathub aceitam sem discussão, então o projeto continua distribuível.
- Copyleft é aplicável em tribunal; NC não é, na prática, para projeto pequeno.

Consequência registrada: o README passa a dizer em uma linha o que isso significa para quem
chega (usar/estudar/modificar/redistribuir; derivado aberto; licença comercial para fechado),
e o `NOTICE` lista dependências e a não-afiliação à Nullsoft/Winamp.

## D-080 — Estado da playlist ao fechar, e onde a posição de janela pode existir

Dois problemas relatados com o app em uso, de naturezas diferentes.

### A playlist não voltava aberta

`closeEvent()` fechava a janela auxiliar **antes** de `saveSettings()`, e o salvamento
perguntava `playlistWindow_->isVisible()` — que já era falso. Resultado: a INI aprendia
"fechado" em toda saída, independentemente do que o usuário via na tela.

Correção em duas partes: o estado é anotado no começo de `closeEvent()` (`playlistOpenAtExit_`)
e a flag `closing_` faz `saveSettings()` usar essa anotação em vez de reler a janela. Sem a
flag, o `hide()` da auxiliar dispara um `saveSettings()` tardio que regrava "fechada" —
foi o que aconteceu na primeira tentativa de conserto.

### Posição de janela

No Wayland **o cliente não posiciona janela**: `move()` é ignorado e o compositor escolhe
onde a janela nasce. Portanto "lembrar a posição" não é um recurso que o app possa
implementar ali — nem para a principal, nem para a playlist, nem para o equalizador.

O que foi feito: `saveGeometry()`/`restoreGeometry()` são usados **só onde posicionamento
existe** (`windowPositionSupported()` = plataforma que não é Wayland), incluindo
`window/playlistGeometry`. Em Wayland o app continua salvando largura/altura, que são
respeitadas.

No Wayland, quem resolve isso é o compositor: **KWin → janela → Mais Ações → Configurar
Configurações Especiais da Janela → Posição**, fixando a posição por aplicação. É regra do
ambiente, não do produto — e vale para qualquer app, não só este.

## D-081 — Janelas auxiliares e a barra de tarefas no Wayland

`Qt::Tool` com pai tira a janela da barra de tarefas no X11 porque o Qt define
`_NET_WM_WINDOW_TYPE_UTILITY`. No Wayland **não existe tipo de janela**: o que marca
subordinação é o `xdg_toplevel.set_parent`, e o Qt só o monta quando as duas janelas já têm
handle. Como a playlist é criada dentro de `buildUi()`, antes de a principal ser mostrada, o
parentesco nunca chegava ao compositor — resultado: duas entradas na barra de tarefas.

Correção: `createWinId()` na auxiliar seguido de `windowHandle()->setTransientParent(...)`, e
a mesma chamada depois de cada `show()` (o handle da principal pode nascer depois). O
equalizador segue o mesmo caminho. Efeito colateral: a auxiliar também passa a ficar acima da
principal e a minimizar junto, que é o comportamento esperado de janela dependente.

Verificação pelo lado do compositor: `org.kde.KWin.getWindowInfo(uuid)` devolve
`hasTransientParent`, `skipTaskbar` e `type` de cada janela (uuid vem de `kdotool search`).
Resultado medido nesta sessão: **a playlist continua com `hasTransientParent: false`** mesmo com
o cliente reportando `transiente=1`. A mesma consulta na janela de Preferências (QDialog
**modal**) devolve `true` — ou seja, no Wayland o Qt só envia `xdg_toplevel.set_parent` para
diálogos modais, não para janelas `Qt::Tool`/`QDialog` não modais.

Consequência: com playlist em janela separada, **o Wayland mostra uma entrada na barra de
tarefas** e não há API de aplicativo que mude isso (o protocolo não tem "skip taskbar"). O
caminho de sistema é a regra do KWin; o caminho de produto é a playlist voltar a ser painel da
mesma janela. Decisão pendente com o usuário.

### Sobra de copiar/colar corrigida no caminho

`setReplayGainMode()` gravava `visual/enabled`, `visual/fps`, `visual/fpsCostP95Ms` e
`visual/fpsCpuPercent` junto de `replaygain/mode` — herança de um bloco vizinho. Os valores
eram os mesmos em memória, então não corrompia nada, mas gravava quatro chaves fora de
propósito a cada troca de modo. Agora grava só a sua, e o nome exibido na barra de status
passou a vir de `tr()`.

## D-082 — Lista instantânea, tag sob demanda e "missing file"

Problema: a abertura restaurava a sessão validando o cache com `stat` de **cada** arquivo e
sondando com FFmpeg o que não batia. Com 406 faixas num share CIFS, isso era o custo inteiro
do startup (2,5–7,3 s medidos) — e não tinha nada a ver com a separação em janelas.

Agora, como no original:

| Quando | O que acontece |
|---|---|
| Abrir o app | a lista vem da INI; a tag vem do cache **sem** `stat` (`TagCache::lookupFast`); nada de sondagem |
| Adicionar arquivos/pasta/playlist | fila de metadados como antes (ação explícita, o usuário está olhando) |
| Tocar uma faixa | o decoder entrega as tags; a linha e o cache são atualizados |
| Salvar no editor de tags | reprobe + atualização da linha e do cache |
| Arquivo ausente ao tocar | linha ganha `⚠` e é **pulada** (`playIndex` percorre a lista uma vez) |

Consequências assumidas:

- Um arquivo editado por ferramenta externa mostra tag velha até ser tocado ou editado aqui.
  O cache não é revalidado em massa no startup — é o preço de abrir instantâneo.
- Faixa sem cache aparece com o **nome do arquivo** até ser tocada. A coluna de duração fica
  vazia nesse meio-tempo.
- Nada é removido da lista automaticamente: share desmontado volta ao normal quando voltar.
  "Remover inexistentes" continua sendo ação manual do menu.

O aviso `⚠` só é descoberto na hora de tocar — que é exatamente o comportamento pedido. Por
isso ele não substitui uma verificação em massa; se um dia isso for desejado, o caminho é
uma ação explícita de "verificar a lista", não um custo escondido na abertura.

## D-083 — Dois modos de playlist: integrada (padrão) e janela própria

O modo externo continuou mostrando dois problemas no Wayland — duas entradas na barra de
tarefas e as duas janelas abertas sobrepostas no centro — porque o Qt só envia
`xdg_toplevel.set_parent` para diálogos **modais**, e o protocolo não tem "skip taskbar".
Nenhum ajuste de atraso resolve isso: é capacidade ausente, não corrida de tempo.

Decisão: a lista é **um painel com dois donos**, escolhidos por `playlist/mode`.

| | `integrada` (padrão) | `externa` |
|---|---|---|
| Onde vive | `mainLayout_` da janela principal | janela auxiliar `Qt::Tool` |
| `PL` | cresce/encolhe a principal | abre/fecha a auxiliar |
| Geometria | `window/integratedHeight` | `window/playlistWidth/Height` + `playlistGeometry` |
| Barra de tarefas | uma entrada | duas no Wayland, uma no X11 |

- `applyPlaylistMode()` é o único lugar que troca o pai do painel (`setParent`) e o
  reencaixa no layout certo; `setPlaylistMode()` guarda a altura do painel antes de trocar,
  aplica e regrava.
- As teclas do Playlist Editor (`Espaço` = ir para a faixa tocando, `Ctrl+A` = selecionar
  tudo) passaram a ser `QAction` presas ao **painel** com `Qt::WidgetWithChildrenShortcut`.
  Presas à janela auxiliar, deixariam de funcionar no modo integrado; presas à principal,
  brigariam com o `Espaço` do player.
- `playlistOpen_` virou a fonte da verdade da visibilidade (antes era
  `playlistWindow_->isVisible()`, que no modo integrado é sempre falso). `closeEvent`,
  `saveSettings` e o gancho de depuração passaram a usá-la.
- Por que manter o modo externo: no Windows o posicionamento funciona e o modelo do original
  é multi-janela. O modo externo é o molde do porte, não um resto do passado.

## D-084 — Modo da playlist na aba Geral e guarda de sessão

- O seletor `Playlist:` (integrada / janela própria) saiu da aba **Reprodução** e foi para
  **Geral**, logo abaixo de `Idioma`. Os dois são escolhas estruturais — onde a lista mora e
  em que língua a interface fala —, não ajustes de audição; e são as únicas que valem para a
  próxima abertura ou para o layout inteiro.
- **Guarda de sessão**: abrir o Amplia com um arquivo na linha de comando desliga a
  restauração (D-029) e a lista começa vazia. Até agora, sair gravava essa lista vazia por
  cima da sessão anterior. Agora `saveSettings()` só grava `playlist/items`/`playlist/current`
  quando `sessionRestored_` (a lista veio da sessão) **ou** `playlistDirty_` (o usuário mexeu
  na lista nesta execução).
- `playlistDirty_` é marcado nas mutações de conteúdo: adicionar (diálogo, arrasto, pasta,
  `.m3u`, URL), remover, limpar, inverter, embaralhar, ordenar, remover inexistentes e
  "manter só a seleção". **Abrir um arquivo para tocar não marca** — inclusive pela linha de
  comando, que é justamente o caso que destruía a sessão. Quem marca ao escolher arquivo é o
  diálogo "Abrir…", porque ali a intenção é do usuário.
- Verificado nos dois sentidos: rodar com um arquivo e sair → `[playlist]` intacta; rodar
  apontando para uma pasta (adição explícita) → lista nova gravada.

## D-085 — Tamanho da janela vale em qualquer plataforma; posição só no X11

Contexto: no Wayland a janela voltava sempre ao tamanho padrão. Diagnóstico: `saveSettings()`
gravava `window/geometry` (largura + altura + posição) **atrás de `windowPositionSupported()`**,
que é falso no Wayland — então nada de tamanho era salvo, e o que sobrava
(`window/width` + `window/collapsedHeight`) não cobria a playlist aberta.

Decisão: separar as duas coisas de vez.

- **Tamanho**: `window/size` (QSize total) e `window/panelHeight` são gravados em qualquer
  plataforma; a restauração lê `window/size` e **deriva** a altura compacta
  (`total − painel`, quando a playlist veio aberta), com piso `kCompactFloor` (274).
- **Posição**: continua em `window/geometry` + `restoreGeometry`, só sob
  `windowPositionSupported()`.
- **Janela da playlist** (modo externo): mesmo raciocínio — tamanho sempre, geometria só no X11.

Por que não guardar só a compacta (como antes): com o painel aberto, `height()` inclui os 300
do painel; gravar isso como compacta reabriria a janela 300 px alta demais quando a playlist
fosse fechada. Guardar o total e a altura do painel, e derivar na leitura, mantém as duas
formas coerentes.

Verificação: resize real para 760×600 com o painel fechado → log `saveSettings: tamanho=760x572`
→ INI `size=@Size(760 572)` → reabrir → KWin reporta `760×600`. Antes disso, a medição estava
sendo feita na janela errada (`kdotool search --name "Amplia"` casa também com a aba do Firefox
que abriu o repositório): lição registrada — filtrar por `caption` exata do KWin.

## D-086 — Teclas da lista: ShortcutOverride em vez de QAction

Sintoma relatado: no modo integrado, **Ctrl+A não selecionava tudo** e as **setas mexiam no
volume** em vez de rolar a lista.

Causa: as ações da lista usavam `Qt::WidgetWithChildrenShortcut` presas ao painel, mas a
janela principal tinha os mesmos atalhos com `Qt::WindowShortcut` (`Ctrl+A` = sempre no topo,
setas = volume/seek). Com o foco na lista, os dois conjuntos ficam ativos → ambiguidade, e o
Qt simplesmente não dispara nenhum: daí "Ctrl+A não faz nada" e "seta muda o volume".

Correção: as teclas que pertencem à lista passam a ser resolvidas no `eventFilter` do
`QListWidget`, tratando `QEvent::ShortcutOverride` e chamando `accept()`. Aceitar o override
faz o Qt entregar o evento como `KeyPress` ao widget em vez de disparar o atalho da janela —
então a lista rola com as setas e recebe Ctrl+A/Espaço. As `QAction` de menu continuam
existentes (com o mesmo efeito), só deixaram de declarar atalho para não recriar o conflito.

Consequência de desenho, igual ao original: `Ctrl+A` e as setas têm significados **por
janela/foco** — no player, sempre no topo e volume; na lista, selecionar tudo e rolar.

### Correção: abrir com a playlist ligada (21/11)

Três defeitos somados, todos no caminho "abrir com o painel ligado":

1. **`showEvent` não era confiável para agendar.** Em alguns ambientes a janela é mapeada
   durante a construção, então o `showEvent` acontecia **antes** de `loadSettings()` ler
   `playlist/visible` — o agendamento nunca era feito e a lista não abria. O agendamento
   virou `schedulePendingPlaylistOpen()`, idempotente, chamado do `loadSettings()` **e** do
   `showEvent`.
2. **Altura do painel com dois nomes.** `saveSettings()` gravava `window/panelHeight`, mas
   `loadSettings()` lia `window/integratedHeight`: o painel voltava sempre com o padrão (300)
   ou com um valor velho (160), e daí a janela abria com 160 px a menos.
3. **Altura compacta derivada do tamanho total.** `compactHeight = size - panelHeight` e
   `panelHeight = height - compactHeight` se realimentavam: cada fechamento com o painel
   aberto empurrava os dois valores. Agora `window/compactHeight` é gravado explicitamente e
   a derivação só serve de migração para INIs antigas.

### Barra de status: fila de metadados e versão (21/11)

Pedido do usuário: acompanhar a leitura de tags numa lista grande ("não feche durante o
processamento") e ver a versão da build para reportar erro.

- Dois rótulos fixos na `QStatusBar`: `queueLabel_` (esquerda, `addWidget(…, 1)`) e
  `versionLabel_` (direita, `addPermanentWidget`). Mensagem passageira (`showMessage`) não
  serve: some sozinha e é sobrescrita por qualquer outro aviso.
- Texto: `Lendo metadados… 12/400 — não feche`, com o total acumulado das filas somadas
  (`metadataTotal_ += missing.size()` em cada `enqueueAll`) e `done = total - pending`.
  O aviso completo fica no tooltip.
- **Detalhe que custou duas tentativas**: os rótulos têm fundo transparente, e o texto de
  uma mensagem passageira por baixo aparecia nos vãos, deixando um "s" solto ao lado da
  versão. Correção: `statusBar()->clearMessage()` enquanto o contador está visível.
- Versão: `0.1.0 (96c818a)` — número de `project(VERSION)` e commit curto, **sem o nome do
  app** (ele já está na janela, na bandeja e no menu; repetir só gastava a largura da barra). Vem de
  **header gerado** (`src/ui/version.h.in` + `configure_file`), não de
  `target_compile_definitions`: as aspas das definições se perdem e o pré-processador
  recebe `0.1.0`, o que quebra a compilação.
- `AMPLIA_GRAB_DELAY_MS` permite capturar estados transitórios nos testes de interface.

## D-089 — Biblioteca: índice no mesmo banco, varredura separada da exibição

A F3 começou pelo caminho mais curto que entrega uso real: listar o que já está indexado
e apontar pastas-raiz.

- **Mesmo banco**: a tabela `roots(path TEXT PRIMARY KEY)` entrou no `library.db` que já
  guarda as tags. Não há segundo arquivo para manter, nem cópia de metadados: a Biblioteca
  é uma **visão** das faixas cujo caminho começa por alguma raiz.
- **`refreshLibrary()` é só banco** (`roots` + `allPaths` filtrado + `lookupFast`,
  ordenado por artista/álbum/título). Varrer pasta de rede dentro da abertura de janela
  travava a interface inteira — inclusive o timer de captura dos testes — porque cada
  arquivo novo dispara `avformat_open_input` pelo CIFS.
- **`rescanLibrary()` é a varredura**: `expandAudioPaths` por raiz, cursor de espera, e
  enfileira só o que falta (`metadataTotal_ += missing.size()`), o que faz o contador da
  barra de status funcionar igual ao de "Adicionar pasta".
- **Contador compartilhado**: `updateMetadataStatus()` também alimenta
  `library_->setStatus(...)`. A Biblioteca não tem fila própria; mostrar o andamento na
  própria janela evita fechar o app no meio da indexação.
- **Tags chegam uma a uma**: `onMetadata()` chama `library_->updateTrack(info)` quando a
  janela está visível, então a linha troca de nome de arquivo para "Artista - Título" sem
  recarregar a lista.
- Tocar da biblioteca: `playFromLibrary()` adiciona a faixa à playlist e toca — a Biblioteca
  não é um player paralelo.

---

## D-090 — Indexação que sobrevive ao fechamento (fila em disco, guarda e playcount)

**Data:** 21/11/2026

- **A fila mora no banco**: `scan_queue(path TEXT PRIMARY KEY, added_at INTEGER)`. Os
  caminhos entram **antes** da leitura e saem na mesma transação em que a tag é gravada
  (`TagCache::store()` faz o `DELETE`). Um `kill` no meio perde no máximo o arquivo em
  leitura — não a lista inteira.
- **Flush no fim do lote**: `onMetadata()` chama `cache_->flush()` quando
  `scanner_->pending() == 0`, inclusive para arquivo ilegível (que sai da fila por
  `dropFromScanQueue`). Sem isso a baixa ficava na transação aberta e um kill a desfazia.
- **Retomada automática**: `resumePendingScan()` no fim do construtor lê a fila e a
  recoloca no scanner, somando em `metadataTotal_` para o contador já nascer certo.
- **Guarda de fechamento**: `closeEvent` pergunta quando há pendentes — **diálogo próprio**
  (`QDialog`), porque o `QMessageBox` fixa o layout no tamanho do texto e corta rótulos
  longos ("Fechar mesmo assim"); "Continuar lendo" é o botão padrão e o texto avisa que a
  indexação continua na próxima abertura. Esconder para a bandeja não pergunta.
- **Playcount**: incrementa 350 ms depois de começar a tocar, e só se ainda estiver
  tocando (o original usa o mesmo timer — `wa_subclass.cpp`, `SetTimer(..., 350, ...)`).
  `lastplay` é gravado junto. Trocar de faixa antes disso não conta, igual ao Winamp.
- **Notas**: `rating INTEGER` de 0 a 5, com 0 = sem nota; a tela desenha as estrelas e o
  banco guarda só o inteiro. "Top Rated" segue o original: `rating >= 3`.
- **`dateadded`**: preenchido no `INSERT` e **preservado** no `ON CONFLICT DO UPDATE`, então
  reindexar não move a faixa para o topo de "Adicionados recentemente".

## D-091 — Biblioteca com a árvore de visões do ML

O Media Library original é uma árvore de **consultas** sobre o banco (Most Played
`playcount > 0`, Recently Added `dateadded > [3 days ago]`, Recently Modified
`lastupd > [3 days ago]`, Recently Played `lastplay > [2 weeks ago]`, Never Played
`playcount = 0`, Top Rated `rating >= 3` — `Src/Plugins/Library/ml_local/queries.cpp`).
Copiamos as consultas, não a tela: `TagCache::queryLibrary(view, filter, days)` devolve as
linhas já ordenadas por visão, e a janela só desenha.

- **Janela** 1120×620, `Qt::Tool`: árvore à esquerda (220 px) com as oito visões, busca no
  topo com debounce de 250 ms, tabela de oito colunas e rodapé com resumo
  ("135 faixa(s) — 8h 30min — 1177 MB") e ações Tocar/Enfileirar.
- **Rótulos curtos na árvore** ("Adicionadas (3 dias)"): os nomes do original não cabem em
  220 px e o parêntese diz a janela de tempo, que é a informação que o nome longo carrega.
- **Ordenação numérica**: `NumericItem` compara `Qt::UserRole`, senão a duração ordenaria
  como texto ("10:02" antes de "3:05") e as estrelas por quantidade de caracteres.
- **Nota por clique** na coluna Nota: a estrela sai da posição do clique (largura/5), clicar
  na nota atual limpa; o banco guarda o inteiro, a tela guarda as estrelas.
- **Recarga no fim do lote**: enquanto o scanner trabalha, a tabela **não** recarrega a cada
  tag (as linhas pulariam); recarrega quando `scanner_->pending() == 0`.
- A consulta é sobre o banco inteiro e a janela filtra pelas raízes: um arquivo tocado da
  playlist entra no cache, mas só aparece na Biblioteca se estiver sob uma pasta indexada.

## D-092 — Coluna de estado com quatro botões (sorteio, repetir, ML, PL)

A coluna à direita passou a ter quatro botões, na ordem do original: **em cima os modos de
reprodução** (sorteio, repetir) e **embaixo as janelas** (ML, PL). O PL fica por último
porque é o único que cresce para baixo — o botão encosta na borda de onde a expansão sai.

- A coluna **desce pela faixa do transporte**, como no Winamp: quatro círculos de 28 px com
  6 de respiro pedem 130 px, mais que a altura do display (154 com a faixa do transporte,
  não caberia só ali). Por isso a linha do topo deixou de ser "display + coluna" e virou um
  bloco `display + transporte` com a coluna ao lado.
- A margem direita de 50 px da faixa de transporte saiu: quem reserva o espaço agora é o
  próprio layout, e a barra de volume termina junto com a barra de posição.
- **ML abre a Biblioteca** (`Alt+L`) e esconde se já estiver visível; ele não pinta estado
  "aberto" como o PL, porque a Biblioteca é janela própria, não um modo do player.
- **Bug de tamanho consertado de passagem**: a altura compacta era aceita até 1200 px no
  INI, então uma sessão com a playlist esticada reabria a janela com um vão enorme (1008 px
  gravados). Agora o valor é limitado à altura natural do bloco compacto, e o painel
  integrado tem altura fixa (`kPlaylistHeight` = 300) — abrir a lista soma exatamente isso.

## D-093 — Quem absorve o espaço vertical da janela

A coluna de quatro botões obrigou a linha de cima a virar um bloco `display + transporte`,
e isso expôs uma pergunta que antes não aparecia: quando a janela é esticada, quem fica com
a altura extra?

- **Display com altura fixa** (`QSizePolicy::Fixed` na vertical). Enquanto ele era elástico,
  esticar a janela abria um vão em cima e embaixo do painel e empurrava o transporte para o
  meio da janela.
- **Playlist absorve**: `mainLayout_->addWidget(playlistPanel_, 1)`. Com a lista aberta,
  aumentar a janela só aumenta a lista — nada de espaço morto.
- **Sem a lista, a janela não cresce**: `resizeEvent` devolve `height()` para
  `compactHeight_` quando a playlist está fechada. É o comportamento do Winamp, cuja janela
  principal é de tamanho fixo; e evita que um arrasto na borda deixe um vão embaixo do
  transporte sem nada para preencher.
- A altura compacta gravada no INI é limitada à altura natural do bloco
  (`sizeHint().height() + 8`); antes aceitava até 1200 px e uma sessão esticada reabria a
  janela com 1008 px de vão (o mesmo problema, visto pela porta do estado salvo).

## D-094 — Posição de janela no Wayland: a opção XWayland

O protocolo `xdg_toplevel` **não tem pedido de posição**: o compositor decide onde a janela
abre, e `QWindow::setPosition`/`move()` não têm efeito em janela de topo. O atraso que
resolveu o `set_parent` da playlist externa não muda isso — ele só adiou o mapeamento para o
pai já estar mapeado, o que é sobre subordinação, não sobre geometria.

- **O padrão continua Wayland nativo**, com a playlist integrada (a posição nem entra na
  conversa, é a mesma janela).
- **Preferências ▸ Geral ▸ "Guardar a posição das janelas (usa XWayland)"** liga o único
  backend em que o cliente posiciona a si mesmo. O `main()` lê a chave **antes do
  `QApplication`** (`qputenv("QT_QPA_PLATFORM", "xcb")`), porque é o plugin de plataforma que
  a consome na construção; a troca pede reinício, como a do idioma.
- Com a opção ligada, a janela externa da playlist e a Biblioteca salvam e restauram
  geometria (`window/playlistGeometry`, `window/libraryGeometry`); com ela desligada, só o
  tamanho é guardado — o compositor escolhe o lugar.
- Medido: Wayland nativo reabre a playlist em `1060,304` (escolha do KWin) ignorando o
  `320,210` salvo; em XWayland reabre em `320,210`, e a Biblioteca em `120,90`.
- O `--socket=x11` já estava nas `finish-args`, então isso não muda empacotamento.
- **Áudio saiu da árvore** da Biblioteca: não há outro tipo de mídia na interface, então a
  visão só repetia "todas as faixas". O enum `LibraryView::Audio` fica (é a consulta base),
  mas sem item na árvore.
- **Altura da playlist é lembrada de novo**: `window/panelHeight` volta a ser lido, agora com
  piso `kPlaylistHeight` (300) e teto 900 — esticar a janela com a lista aberta vale para a
  próxima sessão, sem reabrir o vão que a D-093 fechou (a altura compacta continua limitada
  ao bloco natural).

## D-095 — Quando a geometria é gravada (por que "não funcionava")

A opção XWayland existia, mas a posição **nunca era gravada** na maioria dos caminhos: só o
`closeEvent` chamava `saveSettings()`. Quem fechava pela bandeja (`closeToTray`) ou perdia o
app num kill reabria com a geometria antiga — ou com nenhuma, no primeiro teste depois de
ligar a opção, e aí o compositor centralizava as duas janelas.

- **Gravação por movimento**: `moveEvent` da janela principal e um `eventFilter` em
  `QEvent::Move` da playlist externa e da Biblioteca agendam `saveSettings()` 700 ms depois
  do último movimento (`scheduleSettingsSave`). Assim a posição sobrevive a kill e a queda,
  não só a um fechamento limpo.
- **Fechar para a bandeja grava antes de esconder**: `saveSettings()` no ramo do
  `closeToTray` do `closeEvent`. Esconder é o "terminei por agora" de quem usa o agente.
- Condições para gravar continuam: `windowPositionSupported()` (backend X11/XWayland) e
  janela não maximizada/nem em tela cheia. O tamanho é gravado em qualquer plataforma.
- **Medido**: mover para `300,200`, `flatpak kill` e reabrir → `300,200`; playlist em
  `420,300` idem; `restoreGeometry` aplica `300,228` no widget (o X reporta o quadro em
  `300,200`, que é o que o usuário vê).
- Escape de diagnóstico: `AMPLIA_DEBUG_GEOM=1` loga `save: gravando posição …` e
  `restore: geometria salva aplicada em …`; `AMPLIA_TEST_HIDE=1` fecha pelo caminho normal.

## D-096 — Coluna centrada no display, atraso só na janela externa

- **Coluna de estado centralizada**: os quatro botões ficam ao lado do display e centrados
  nele (`AlignVCenter`), não mais descendo pela faixa do transporte. Com 28 px de botão e 4
  de respiro o bloco dá 124 px e cabe nos 154 do display; o transporte continua ocupando a
  largura inteira logo abaixo.
- **Atraso de abertura só quando serve**: os 200 ms existem para o KWin receber o
  `xdg_toplevel.set_parent` da janela **externa** (que só é enviado com a principal já
  mapeada). Com a playlist integrada não há janela para subordinar, e em X11 o parentesco
  não depende do mapeamento — então o atraso passou a valer só para "externa + Wayland".
- **Trocar de modo em tempo de execução** agora funciona nos dois sentidos: ao ir para
  externa no Wayland, a janela é mostrada um ciclo depois (`QTimer::singleShot(200, …)`) com
  o mesmo cuidado da abertura inicial; ao voltar para integrada, o painel é reparentado ao
  widget central e entra no layout com stretch 1. Antes, a troca em execução deixava a
  janela externa saindo sem `set_parent` (e o painel parecia não voltar).
- **Nomes padronizados**: "Janela integrada" e "Janela externa" no combo de Preferências, com
  a dica reescrita (o que cada modo faz, sem falar em "própria"/"combinada").
- **Texto do play**: "Reproduzir com a faixa tocando volta ao início" virou "O botão
  Reproduzir recomeça a faixa do zero" — o sujeito agora é o botão, não a ação ambígua.
- Verificação: alternância por `AMPLIA_TEST_MODE=externa|integrada` (escape de depuração),
  log `teste modo=… externa_visivel=… pai_do_painel=…`; captura da janela compacta com a
  coluna centrada (`/home/cassio/sideD.png`).

## D-097 — Troca de backend exige processo novo (e o que travava a janela)

- **A opção XWayland não podia ser desligada** porque o "Reiniciar agora" chamava o mesmo
  `restartHandler_` do idioma, que **recria a janela no mesmo processo**. Plataforma é
  escolhida na construção do `QApplication`, então nada mudava: o app continuava em xcb e a
  impressão era "não desmarca". Agora existe `restartWithPlatformChange()`: limpa
  `QT_QPA_PLATFORM` (senão o filho herda xcb), inicia um processo novo (`/bin/sh -c 'sleep 1;
  exec "$0"'`) e encerra o atual — o servidor de instância única já morreu, então o filho
  vira a instância.
- **A janela não encolhia** porque `setPlaylistVisible(false)` deixava `setMaximumHeight`
  preso em `compactHeight_` e o mínimo podia ficar em `compactHeight_ + 160` (vindo do modo
  integrado com lista aberta). A troca para janela externa acontece sem passar por esse
  ramo, então a janela ficava grande com vão morto e sem poder diminuir. Agora os limites
  são recalculados em **todo** `setPlaylistVisible` (`mín = compacto` ou `compacto + 160`,
  `máx = livre`) e o modo externo sempre devolve a principal ao tamanho compacto.
- **"Sempre no topo" voltava ligado** porque o save podia rodar antes de o menu refletir o
  estado; `saveSettings()` passou a gravar `alwaysOnTopAction_->isChecked()` (fonte da
  verdade na interface). Verificado com `AMPLIA_TEST_TOGGLE_TOP=1`: `true → trigger → false`,
  e `false` depois de fechar.
- Escapes de teste novos: `AMPLIA_TEST_TOGGLE_TOP`, `AMPLIA_TEST_MODE=externa|integrada`,
  `AMPLIA_TEST_XWAYLAND_OFF` (grava `false` e reinicia de verdade).
- Verificação da troca de backend: log mostra `plataforma xcb` na sessão antiga e
  `plataforma wayland` na nova, com `xwaylandPosition=false` no INI.

## D-098 — Piso da janela integrada: o dobro do player compacto

Ao voltar da janela externa para a integrada, a altura ia para `compacto + painel` e, alguns
milissegundos depois, um `resize` de 582 → 282 chegava **de fora do layout** (a troca de dono
do painel) e espremia display e transporte um sobre o outro. `setMinimumHeight` sozinho não
segurava: o pedido não vem do layout, então Qt o aplica.

- **Regra** (decisão do usuário): com a playlist **integrada e aberta**, o piso é **2× o
  player compacto** (564 px com o compacto de 282) — garante display + transporte inteiros e
  uma faixa de lista utilizável. Fechada, ou em janela externa, o piso volta ao compacto.
- **Reasserção no `resizeEvent`**: se a altura cair abaixo do piso com a lista aberta, a
  janela volta para `max(piso, compacto + altura da lista)`. É a única forma de segurar um
  pedido que não passa pelo layout.
- **Lembrar o que foi esticado**: quando o usuário aumenta a janela de propósito, a altura
  extra vai para `integratedHeight_` (clamp 160–900) e é gravada em `window/panelHeight`.
- O máximo **nunca** é fixado (`QWIDGETSIZE_MAX`): era o que impedia diminuir o reprodutor
  depois de fechar a lista (D-097).
- Medido: externa → integrada reaplica 564 e depois 582, com `piso: 282 < 564, voltando para
  582` no log; integrada → externa volta a 282 (`min=282`).
- Escape: `AMPLIA_DEBUG_WINDOWS=1` loga `resize:`, `piso:` e os estados de `setPlaylistVisible`.
