# Amplia

Player de áudio nativo para Linux, escrito em C++ com Qt 6 — playlist, fila, equalizador de 10 bandas, visualizador, scrobbling e controle de mídia, sem camada de emulação.

## Motivação e histórico

Uso o Winamp há mais de vinte anos. Ele faz coisas que nenhum player moderno com a mesma competência (na minha opinião):
fila separada da playlist, o sorteio com nível, o display de tempo condensado com visualizador de picos de volume e visualização de bandas, o jeito de saltar para uma faixa digitando parte do nome. Tudo o que existe hoje é limitado e/ou burocrático demais ou exige Wine, então resolvi "modernizar" justamente o que eu gostava.

Em setembro de 2024 a Llama Group abriu o código-fonte do Winamp, mas sob uma licença que proíbe distribuir versões modificadas — ou seja, um port direto não pode ser publicado a partir dele.
Então a saída foi outra: usar o comportamento como **especificação** e escrever o player do zero, nativo, em C++ com Qt 6, PipeWire e FFmpeg. Nenhum arquivo do fonte original entrou neste repositório.

O código foi escrito em parceria com um agente de IA (DeepSeek, através do DeepSeek Harness para Linux): eu definia o comportamento, revisava cada tela, testava com a minha biblioteca e decidia o que entrava ou seria repriorizado. As decisões de projeto, o escopo, os testes de aceitação e este README são meus. O registro de como o projeto foi construído está em [`docs/NOTAS-DE-SESSAO.md`](docs/NOTAS-DE-SESSAO.md).

> **Sobre a origem do código:** não há cópia direta de código de fontes conhecidas.
> Bibliotecas entram por linkagem (Qt, FFmpeg, TagLib, libsystemd, SQLite), e o código do Winamp serviu como referência de **comportamento e formato**, nunca como fonte. Mas como parte deste projeto foi escrita por um agente de IA, treinado com as mais diversas fontes, sob as quais eu não tenho controle — então é possível que algum trecho reproduza, sem intenção, uma solução que já exista em outro lugar. Se você reconhecer um trecho assim, abra uma issue: a correção ou a atribuição é feita tão logo quanto possível.

> O Amplia é um projeto independente. Não tem vínculo com a Nullsoft, com a Winamp ou com a Llama Group, e não distribui código do Winamp nem artefatos da marca. O nome, a interface e os recursos são originais; a compatibilidade com formatos e hábitos de teclado é intencional.

![A janela do Amplia tocando](/docs/img/player.png)

## Estado

| Fase | Entrega | Situação |
|---|---|---|
| F0 | Primeiro som nativo (decode + PipeWire + janela mínima) | **concluída** |
| F1a | Player e playlist (fila, sorteio, `.m3u`, metadados) | **concluída** |
| F1b | Integração (gapless, MPRIS2, Last.fm, ListenBrainz) | **concluída** — validada no uso real |
| F1c | Formatos e última milha (`.pls`, `.b4s`, `.wpl`, pastas, sessão, colunas, capa de pasta) | **concluída** |
| F2 | Equalizador de 10 bandas, presets `.eqf`, ReplayGain, curva | **concluída** |
| F2B | Display do player (relógio de 7 segmentos, LEDs, transporte) | **concluída** |
| F2C | Visualização (medidor L/R, espectro, beat, calibração de fps) | **concluída** |
| F2D | Janelas (playlist e equalizador em janela própria) | **concluída** |
| F2E | Aparência (esquemas de cor, editor de quatro cores, CSV) | **concluída** |
| F5 | Atalhos (paridade de teclado com o original) | **concluída** |
| F6a/b | Idioma (infraestrutura e inglês completo) | **concluída** |
| F3 | Biblioteca SQLite, importação, playcount | próxima |
| F4 | Plugins `.so` (DSP/visualização) | não iniciada |
| F6c | Documentação pública (este README, `NOTICE`) | em andamento |
| F7 | Skins clássicas `.wsz` | não iniciada |
| F2D.1 | Acoplamento de janelas (só X11) | avaliado, não planejado |

O que já funciona está descrito em [**O que funciona hoje**](#o-que-funciona-hoje), mais
abaixo. O detalhe de cada decisão está em [`docs/DECISIONS.md`](docs/DECISIONS.md) e o
plano em [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Como compilar

O alvo de build é o SDK do KDE, que já traz Qt 6, FFmpeg e PipeWire — não é preciso
instalar nada de desenvolvimento no host.

```bash
# 1. runtime e SDK (uma vez, ~3 GB na instalação de usuário)
flatpak install --user flathub org.kde.Sdk//6.10

# 2. build + instalação local
cd wamp
./scripts/build-flatpak.sh

# 3. executar (abre a janela; passe um arquivo para tocar na hora)
flatpak run io.github.cassiofiorin.Amplia ~/Música/alguma-faixa.mp3
```

### Sobre o `flatpak-builder`

O `org.flatpak.Builder` **não serve** neste caso: rodando dentro do seu sandbox, ele
consulta o SDK via portal do host com um ambiente em que a instalação de usuário do
Flatpak não aparece, e falha com `Unable to find sdk org.kde.Sdk version 6.10` —
mesmo com `flatpak info org.kde.Sdk//6.10` funcionando no host. Use o binário do
host, um dos dois caminhos:

```bash
# caminho normal, com root
sudo dnf install flatpak-builder

# alternativa sem root: extrair o RPM em ~/.local/share/amplia-tools
mkdir -p /tmp/fpb && cd /tmp/fpb
dnf download flatpak-builder
mkdir -p root && cd root && rpm2cpio ../flatpak-builder-*.rpm | cpio -idm --quiet
mkdir -p ~/.local/share/amplia-tools
cp -r /tmp/fpb/root ~/.local/share/amplia-tools/flatpak-builder
```

O script procura, nesta ordem: `$FLATPAK_BUILDER_BIN`, `flatpak-builder` no `PATH`,
o binário extraído em `~/.local/share/amplia-tools/flatpak-builder/usr/bin/` e, por
último, o app `org.flatpak.Builder` (com aviso).

Artefatos de build ficam em `~/.cache/amplia-build` (fora do repositório, porque o
manifest usa `type: dir` e copiaria qualquer lixo junto).

## O que funciona hoje

Botões `⇄` (sorteio) e `PL` ficam na linha do título, como os botões `PL`/`ML` da skin
clássica; a linha de transporte fica só com os controles e o volume, que ocupa a largura
livre. O botão `PL` abre o editor de playlist **dentro da mesma janela**: ela cresce para baixo
(300 px) e volta ao tamanho compacto quando você fecha a lista, sem virar uma segunda
entrada na barra de tarefas. Duplo clique toca uma faixa. Arrastar arquivos para a janela
(ou usar `Adicionar…`) apenas **organiza** a lista: nada começa a tocar sozinho — o
primeiro item fica carregado em pausa, pronto para o `Play`. Quem abre por `Abrir…`, pela
linha de comando ou por duplo clique escolheu tocar, e toca. `wamp --playlist` já abre com
a lista à mostra. Tamanho e estado da lista ficam guardados na INI; no Wayland a posição
da janela é do compositor, então só o tamanho volta.

Atalhos no estilo Winamp: `Z` anterior, `X` toca, `C` pausa, `V` para, `B` próxima,
`Espaço` alterna play/pause, `S` liga/desliga o sorteio, `Ctrl+P` preferências.
No padrão do Playlist Editor: `L` adiciona arquivo(s), `Shift+L` adiciona pasta,
`Ctrl+O` abre playlist, `Ctrl+N` começa uma nova, `Ctrl+S` salva `.m3u`,
`Alt+3` abre o **File Info**, `,` abre o **Jump to File**.

O rodapé da lista tem três menus, como o Playlist Editor: **Arquivo ▾** (nova, abrir,
adicionar arquivo/pasta, salvar), **Playlist ▾** (selecionar tudo/nada, inverter,
remover, manter só a seleção, limpar, remover inexistentes) e **Ordenar ▾** (por
título, por nome de arquivo, por caminho, inverter lista, embaralhar lista) — com os
mesmos atalhos do Winamp (`Ctrl+Shift+1..3`, `Ctrl+R`, `Ctrl+Shift+R`). Ordenar fica
bloqueado enquanto a leitura de tags ainda está na fila, porque a ordem por título
mudaria por baixo.

O sorteio tem nível (0–100%) em **Opções ▸ Preferências…**, como o *shuffle morph rate*
do Winamp: 0% segue a ordem da lista, 100% sorteia por toda a playlist e os valores do
meio sorteiam dentro de um trecho à frente, sem repetir a faixa atual nem as últimas que
tocaram. Botão `⇄` na barra de transporte liga e desliga.

Na lista, `Enter` toca a faixa selecionada e `Delete` remove; `Alt+clique esquerdo`
enfileira (de novo, tira da fila) e clique direito abre o menu com reproduzir, enfileirar
e remover. *Limpar playlist* pede confirmação, porque um clique acidental ali custa a
lista inteira.

Os sliders de posição e de volume pulam para onde você clica (não só arrastando o
indicador): clique posiciona, arraste continua de onde clicou.

Dá para soltar arquivos em qualquer lugar da janela, inclusive em cima da própria lista —
mas só arquivos que existem como caminho local. Arrastar de um compartilhamento de rede
aberto pelo Dolphin entrega `smb://…`, que o app ainda não lê; nesse caso ele avisa na
barra de status. Montar a pasta no sistema (cifs) resolve, porque aí o caminho é local.

Cada linha mostra o número da faixa à esquerda; a que está tocando ganha `▶`. Faixas
enfileiradas mostram a posição na fila colada à direita (e o tooltip diz "Fila: posição
N"), atualizando conforme a fila anda. Importar várias faixas já abre a playlist.

Os metadados chegam sozinhos: ao adicionar arquivos a lista mostra o nome do arquivo e
vai trocando por "Artista - Título (m:ss)" conforme a fila de leitura avança (uma faixa
por vez, na barra de status). Nada bloqueia a janela — dá para tocar enquanto o resto é
lido.

A fila (botão **Enfileirar**) toca antes de seguir a ordem da playlist — é a "JTF" do
Winamp. O contador aparece na barra de status.

### Scrobbling

`Preferências` (`Ctrl+P`, ou `wamp --preferences`) ▸ aba **Scrobbling**:

- **Last.fm**: cole a API key e o secret (criados em <https://www.last.fm/api/account/create>)
  e clique em **Conectar com o Last.fm…**. O navegador abre na página de autorização; depois
  de autorizar, volte e clique em **Já autorizei — concluir**. A session key fica guardada
  na INI (`~/.var/app/io.github.cassiofiorin.Amplia/config/amplia/amplia.ini`, permissão 600) e
  vale até você revogar o acesso no site.
- **ListenBrainz**: cole o token do seu perfil e use **Testar token**.

A caixa **Anunciar "ouvindo agora"** fica desligada por padrão: o Last.fm não tem chamada
para encerrar o anúncio, então ele fica no perfil até outro scrobble chegar. Ligada, o
anúncio só sai depois de 5 segundos tocando (faixa que nem começou não aparece).

Só entra faixa com mais de 30 segundos, depois de 50% ou 4 minutos tocados — a mesma regra
do Last.fm. O tempo conta só enquanto toca de verdade (seek não conta). O que falhar por
rede fica numa fila em disco e é reenviado com espera crescente.

O app **não** anuncia "ouvindo agora": o Last.fm não tem chamada para encerrar esse estado,
e ele ficava pendurado no perfil quando a reprodução parava antes do mínimo. Só o scrobble
completo é enviado.

### Adicionar pastas

Arrastar (ou escolher) uma pasta varre o conteúdo recursivamente e adiciona só os arquivos
de áudio, em ordem natural — `CD1/`, `CD2/`, `CD10/` saem na ordem certa. O que não é áudio
(arte de capa, `.txt`, `.cue`) é ignorado e contado na barra de status.

### Capa no controle de mídia

Se o arquivo tem capa embutida, ela aparece no applet do KDE (extraída para o cache do app, sem
tocar no arquivo original). Trocar a capa pelo **File Info** atualiza na hora.

### Aparência

**Opções ▸ Aparência…** troca a paleta inteira — janela, lista, display, espectro e
a curva do equalizador — sem reiniciar. Seis esquemas: **Grafite** (padrão), **Clássico**, **Verde LCD**,
**Âmbar**, **Azul aço** e **Papel** (claro). A escolha vai para `appearance/theme` na INI.

Além dos presets, a própria janela monta um esquema próprio: quatro cores
(fundo, destaque, secundária e botões) e um contraste global. A janela repinta enquanto você
mexe, e o resultado vira o item **Personalizado** do menu.

**Salvar tema atual como…** dá nome à paleta; ela passa a aparecer no menu junto dos presets.
**Importar/Exportar temas (CSV)** move vários de uma vez — o formato é
`id,nome,fundo,destaque,secundaria,botoes,contraste`, até 30 por arquivo, e **Salvar modelo
CSV…** grava um arquivo de exemplo com uma linha comentada.

Não é skin: o Amplia continua com o mesmo layout e a mesma forma de janela. Renderizar
`.wsz` é a F7.

### Bandeja (agente)

Ao abrir, o Amplia põe um ícone na bandeja com os comandos do agente do Winamp: Mostrar/Ocultar
janela, Reproduzir/Pausar, Anterior, Próxima, Aleatório e Repetir. O tooltip mostra a faixa
tocando. Clique esquerdo alterna a janela; clique do meio alterna play/pause.

Em **Preferências ▸ Reprodução** há a caixa **Fechar para a bandeja (agente)**: com ela ligada,
o X esconde a janela e a música continua — sair de verdade é "Sair" no menu do ícone. Na
primeira vez aparece um balão avisando.

Abrir o Amplia de novo não cria uma segunda janela: os arquivos vão para a instância que já
está tocando e ela vem para a frente.

### Capa e colunas da lista

A capa do controle de mídia sai da própria tag (ID3v2, FLAC, MP4). Se o arquivo não tem
capa embutida, o Amplia procura `cover.jpg`, `folder.jpg`, `front.jpg` (e `.png`) na pasta —
sem diferenciar maiúsculas, como as discografias vindas do Windows.

A lista mostra número, título e duração em colunas. Em **Playlist ▸ Mostrar caminho
completo** a coluna do título passa a mostrar o caminho do arquivo, útil quando duas pastas
têm faixas com o mesmo nome.

### Arquivos que não tocam

Se você abrir um vídeo (ou qualquer coisa que não seja áudio) pela linha de comando, pelo
"Abrir…" ou arrastando um arquivo solto, o Amplia explica o motivo e oferece **Abrir no player
do sistema** — em vez de tentar decodificar e falhar com erro técnico. O player é de áudio
por decisão de projeto: sem pipeline de vídeo, sem sincronia de imagem, sem peso extra.

Arquivos num contêiner fora da lista, mas de áudio puro (`.mka`, `.m4b`), tocam normalmente:
quem responde é o FFmpeg, não a extensão. E ao arrastar uma pasta inteira nada disso vira
aviso — os arquivos de áudio entram, o resto é contado como ignorado na barra de status.

### Equalizador (Alt+G)

Dez bandas de 70 Hz a 16 kHz, de −12 a +12 dB, com pré-amplificação e dezessete curvas de uso
comum (Rock, Pop, Full Bass, Vocal e outras). No topo, a curva é a resposta real das dez
bandas, calculada com os mesmos coeficientes do áudio: arraste um ponto para ajustar a banda.
A janela não bloqueia: arrastar um slider muda o som na hora, e o que vale fica na INI
(`eq/*`). Os valores são em dB, e bandas com ganho zero são puladas no processamento. Um
limitador de pico (teto em −0,6 dB) segura o reforço para não estourar: empurrar o pré-amp
comprime o som em vez de distorcer.

### Presets do equalizador

**Importar .eqf/CSV…** lê os dois sabores de `.eqf` (o de texto, que circula por aí, e o
binário — o tamanho do registro varia entre programas e versões, e é detectado na leitura) e também o CSV do Amplia. **Exportar CSV…** grava a curva atual no
formato aberto:

```
nome,preamp_db,70,180,320,600,1000,3000,6000,12000,14000,16000
Rock,0,8,5,-2,-3,-1,3,6,7,8,8
```

**Salvar curva…** guarda a curva atual na sua lista (aparece no combo depois das
embutidas, com o nome que você escolher) — é um `.csv` em
`~/.var/app/io.github.cassiofiorin.Amplia/data/cassiofiorin/Amplia/presets/`, então dá para
copiar o arquivo para outra máquina. **Excluir** tira a curva da lista sem mexer no ajuste.

Serve para trocar curva com alguém sem mandar binário.

### ReplayGain

`Opções ▸ ReplayGain` escolhe **desligado**, **por faixa** ou **por álbum**. O ganho vem das
tags (`REPLAYGAIN_TRACK_GAIN`, `REPLAYGAIN_ALBUM_GAIN`) e é aplicado antes do equalizador —
o mesmo ponto por onde o gapless passa, então cada faixa entra com o fator dela.

Se o arquivo declara o pico (`REPLAYGAIN_*_PEAK`), o ganho é limitado por ele para não
estourar. Sem as tags, nada muda.

### Playlists

`.m3u`/`.m3u8`, `.pls` (rádio), `.b4s` (Winamp 5) e `.wpl` (Windows Media Player) abrem pelo
**Arquivo ▸ Abrir playlist**, pela linha de comando ou arrastando o arquivo. Caminhos
relativos são resolvidos contra a pasta da playlist, então uma pasta de álbum com `lista.m3u`
funciona em qualquer lugar. Salvar usa o formato da extensão escolhida.

### Sessão e arquivos ausentes

A lista volta na hora: a abertura lê a INI e as tags já guardadas em cache, **sem varrer os
arquivos**. Tags novas aparecem quando a faixa é tocada (o decoder entrega) ou quando você
salva pelo editor de tags.

Se um arquivo não existe mais, a linha ganha um `⚠` na hora em que você tenta tocá-la — e o
player pula para a próxima. Nada é removido da lista sozinho: um share de rede desmontado
volta ao normal quando voltar.

### Sessão

Ao abrir sem arquivos, o Amplia devolve a playlist da última vez e deixa a faixa que estava
tocando carregada **em pausa** (é só apertar Play). Se você abrir um arquivo pela linha de
comando ou pelo "Abrir com…", a sessão anterior não é restaurada: o pedido de fora manda.

### File Info (Alt+3)

As abas **Basic Info** e **Artwork** mostram e editam as tags com o TagLib: título,
artista, álbum, artista do álbum, ano, gênero, comentário, compositor, gravadora,
faixa, disco e BPM, mais a informação de áudio (codec, bitrate, taxa, canais, duração)
e o Replay Gain quando existir. As setas `<<`/`>>` trocam de faixa sem fechar a janela.

A imagem embutida aparece na aba Artwork, com **Trocar…**, **Salvar como…** e
**Remover**. Hoje a escrita de imagem cobre MP3 (ID3v2), FLAC e MP4/M4A.

Se o arquivo estiver numa pasta somente leitura — um NAS montado `ro`, por exemplo — o
diálogo mostra **"Somente leitura — <motivo>"** em âmbar acima do rodapé e desabilita o
`OK`. A checagem usa `statvfs`, então também pega montagem `ro` cujo modo de permissão
parece normal, e não escreve nada para descobrir.

**Gravação segura** (ligada por padrão) copia o arquivo para `<arquivo>.amplia-tmp`,
grava na cópia, confere relendo os campos e só então troca o original — se algo der
errado no meio, o original continua intacto. É por isso que o rodapé avisa: desligar
da tomada no meio de uma gravação sem gravação segura pode deixar as tags num estado
inconsistente.

### Jump to File (`,`)

Abre a busca sobre a playlist já carregada: digite pedaços do título, do artista, do
álbum ou do caminho (sem acento e sem caixa), em qualquer combinação de palavras.
As **setas** `↑`/`↓` andam pela lista sem tirar o foco do campo, então dá para digitar e
escolher na mesma mão. `Enter` toca a faixa e fecha a janela; `Ctrl+Enter` enfileira e
deixa aberta, para enfileirar várias; `Esc` fecha sem tocar. O contador mostra quantas
casaram. **Alt+clique esquerdo** numa linha enfileira sem fechar a janela, igual à lista
principal — dá para montar a fila inteira ali dentro.

### Painel do player

No topo da janela fica o display: relógio de sete segmentos, indicador de tocando/pausado,
`KBPS`, `KHZ`, `ESTÉREO`/`MONO`, e os indicadores `EQ`, `REP`, `RAND`. Abaixo do relógio, o
nome da faixa com a duração entre parênteses.

- **Clique na barra** (ou arraste) para pular para aquele ponto da faixa.
- **Clique no relógio** para alternar entre tempo decorrido e tempo restante.
- **Clique nas pílulas** `EQ`, `REP` e `RAND` para ligar/desligar cada função direto do
  painel.

Na linha de transporte, os botões são redondos: anterior, reproduzir, pausar, parar,
próxima e **silenciar**, com a barra de volume ao lado. Sorteio, repetir e `PL` ficam
empilhados à direita do display, e os LEDs `EQ`, repetir e sorteio dentro do painel também
respondem ao clique.

O botão de repetição tem três estados: desligado, repetir a lista e repetir a faixa (a
tecla `R` percorre os três). Com **Reproduzir com a faixa tocando volta ao início** ligado
(padrão), clicar em reproduzir durante a música recomeça a faixa.

### Visualizador

Com a música tocando, o bloco à direita do display mostra o **espectro** em 20 bandas, os
medidores de pico dos canais esquerdo e direito e um ponto que pisca na batida. Ele roda a
15 quadros por segundo, para sozinho quando a janela é escondida e **compensa o atraso do
buffer de decodificação** (o que aparece na tela é o que está saindo na caixa, não o que o
decoder acabou de preparar). **Preferências ▸ Visualização**
liga e desliga o visualizador, fixa os quadros por segundo (10–60) e tem o botão **Calibrar…**,
que mede 10 s o custo de desenhar o painel e escolhe a maior taxa que ainda sobra folga. Parado, o bloco mostra a malha de pontos no lugar das barras.

### Título da janela

Com uma faixa tocando (ou pausada), o título da janela vira `Artista - Título — Amplia`. O
painel de tarefas do KDE e o applet de mídia mostram esse título, então dá para ver o que
está tocando sem abrir o player. Parado, volta a `Amplia`.

### Controle de mídia

O Amplia aparece no barramento como `org.mpris.MediaPlayer2.amplia`, então teclas de mídia,
applets e `playerctl` controlam o player:

```bash
playerctl --player=amplia play-pause
playerctl --player=amplia metadata
```

## Teste de fumaça (sem GUI)

O app instala também `amplia-smoke`, que exercita a máquina de estados do engine — play,
pausa com posição congelada, retomada, stop e seek — e sai com código 0 quando tudo passa:

```bash
flatpak run --command=amplia-smoke io.github.cassiofiorin.Amplia ~/Música/alguma-faixa.mp3
```

O mesmo binário cobre a playlist (fila, `next`/`previous` com volta e ida-e-volta `.m3u`),
então ele é o primeiro lugar a olhar quando algo da F1 quebrar.

Build nativo fora do sandbox (para iterar mais rápido — precisa de Qt 6, FFmpeg e
PipeWire *dev* no host, o que hoje ele não tem; nesse caso use o shell do SDK):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/src/ui/wamp
```

## Estrutura

```
wamp/
├── docs/              arquitetura, decisões e roadmap
├── packaging/         manifest Flatpak, .desktop e ícone
├── scripts/           build-flatpak.sh
└── src/
    ├── core/          libwamp: engine, decoder, saída de áudio, DSP, biblioteca
    └── ui/            aplicação Qt 6
```

O fonte do Winamp fica fora deste repositório e é **material de referência apenas** —
consulta de comportamento, nunca de código.

## Decisões que moldaram o projeto

Algumas escolhas explicam o resto do código — e estão registradas com o motivo, não só com
o resultado, em [`docs/DECISIONS.md`](docs/DECISIONS.md):

- **Qt 6 Widgets, não QML.** A janela é compacta, sempre à mão, e o desenho é `QPainter`. QML
  fica reservado para skins (F7), onde o layout dinâmico é o ponto.
- **Áudio nativo em PipeWire**, sem PulseAudio como intermediário, com a thread de tempo real
  sem alocação. O buffer é um anel de 2 s e o decode corre à frente.
- **FFmpeg como único decodificador.** Um caminho de código para todos os formatos, e a
  detecção de conteúdo (áudio × vídeo) vem do próprio FFmpeg, não da extensão.
- **O equalizador roda entre o decode e a saída de áudio**, no mesmo ponto em que o gapless emenda as
  faixas — assim os dois não brigam.
- **MPRIS2 por `sd-bus`**, depois de o QtDBus recusar exportar as interfaces. O caminho mais
  chato deu o controle mais fino sobre o barramento.
- **Scrobbling com fila em disco** e `QNetworkAccessManager`, sem `libcurl`: falha de rede não
  perde escuta.
- **Sem cópia do fonte original.** O comportamento é especificação; o código é novo.

## Limites conhecidos

Coisas que hoje **não** funcionam, para não gerar expectativa errada:

- **Wayland:** o app não conecta janelas (como a playlist acoplada ao player). O tamanho é
  salvo, a posição é do compositor. Existe escape por X11 (`QT_QPA_PLATFORM=xcb`, o manifest
  habilita `--socket=x11`), e o empacotamento traz os dois sockets.
- **Vídeo:** fora de escopo. O player é de áudio; abrir um vídeo mostra a explicação e oferece
  abrir no player do sistema.
- **Skins `.wsz`:** ainda não (F7). Os esquemas de cor atuais são paletas, não skins.
- **Plugins de terceiros:** o modelo de plugins do Winamp é DLL do Windows; a F4 planeja uma
  API própria em `.so`.
- **Rede:** arrastar de um compartilhamento aberto pelo Dolphin entrega `smb://…`, que o app
  não lê — monte a pasta (cifs) para o caminho ser local. URL por `Ctrl+L` toca direto.
- **Português e inglês** são os únicos idiomas embutidos hoje (mais detalhes em [Traduções](#traduções)).

## Traduções

A interface está disponível em **português (idioma-fonte)** e **inglês**. A escolha fica em
**Preferências ▸ Geral ▸ Idioma**, com a opção "Sistema" seguindo o locale do desktop; a troca
pede para reiniciar a interface.

Traduzir não exige recompilar: um arquivo `.qm` avulso em
`~/.var/app/io.github.cassiofiorin.Amplia/data/cassiofiorin/Amplia/i18n/` é carregado na
abertura. O passo a passo está em
[`CONTRIBUTING-TRANSLATIONS.md`](CONTRIBUTING-TRANSLATIONS.md).

Os termos do display (`KBPS`, `KHZ`, `STEREO`, `MONO`, `EQ`, `L`, `R`, `dB`) não são
traduzidos de propósito: são a língua do "hardware de áudio", não da interface.

## Licença e créditos

**GPL-3.0-or-later**, com **licença comercial sob consulta** para quem precisa de um
derivado fechado. O texto completo está em [`COPYING`](COPYING); os créditos e as licenças
das dependências, em [`NOTICE`](NOTICE).

Resumindo o que isso significa:

- você pode usar, estudar, modificar e redistribuir, inclusive comercialmente — sob GPL, sem pedir autorização;
- se distribuir uma versão modificada, ela continua sob GPL (com o código disponível);
- o que exige licença comercial é incorporar o Amplia num **produto fechado**: nesse caso,
  é só conversar.

O ícone e a interface foram desenhados para este projeto. As bibliotecas usadas (Qt, FFmpeg,
TagLib, SQLite, libsystemd) entram por linkagem dinâmica, cada uma sob a sua licença.

Feito por **Cassio Fiorin** — [github.com/cassiofiorin](https://github.com/cassiofiorin).
