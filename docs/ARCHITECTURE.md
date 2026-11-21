# Arquitetura do Amplia

## 1. Objetivo

Reproduzir o **comportamento** de um player Winamp no Linux, de forma nativa: sem Wine,
sem carregar DLLs PE, sem tradução de chamadas. O alvo funcional das fases iniciais é:

- tocar MP3, FLAC, OGG/Vorbis, Opus, WAV, AAC/M4A;
- playlist com os formatos clássicos (`.m3u`, `.m3u8`, `.pls`, `.b4s`, `.wpl`);
- fila de reprodução no estilo "jump to file";
- equalizador de 10 bandas com presets `.eqf`;
- biblioteca local com metadados, playcount e busca;
- scrobbling (Last.fm e ListenBrainz) e MPRIS2;
- plugins de DSP/visualização próprios, em `.so`.

**Fora de escopo até a F5:** skins `.wsz`, visualizações Milkdrop/AVS, plugins Win32 de
terceiros, CD ripping, queima, dispositivos portáteis.

## 2. Fronteira com o código do Winamp

O repositório onde este projeto vive é um fork do fonte do Winamp, sob a *Winamp
Collaborative License v1.0*, que permite modificação **para uso privado** e proíbe
distribuir versões modificadas ou forks (cláusula 5).

Consequências de projeto, não de estilo:

1. `wamp/` contém somente código escrito para este projeto. Nenhum arquivo de `../Src`
   é copiado, compilado, linkado ou redistribuído.
2. O fonte do Winamp é usado como **especificação**: ler `eq10dsp.cpp` para entender a
   resposta do filtro e reimplementar a matemática é legítimo; copiar o arquivo, não.
3. Nada é publicado. Portfólio mostra vídeo, screenshots e diagramas — nunca o código.

## 3. Camadas

```
┌─────────────────────────────────────────────────────┐
│  UI (Qt 6 Widgets)                                  │
│  janela principal · playlist · EQ · biblioteca      │
└───────────────────────┬─────────────────────────────┘
                        │ API do core (sem Qt)
┌───────────────────────▼─────────────────────────────┐
│  libwamp (C++20)                                    │
│  PlayerEngine · Playlist · Queue · DSP · Library    │
│  ScrobbleSink · Config                              │
└──┬──────────┬──────────┬──────────┬─────────────────┘
   │          │          │          │
 FFmpeg    PipeWire   SQLite     libcurl + sd-bus
 decode    saída      biblioteca Last.fm/LB · MPRIS2
```

Regra de ouro: **o core não conhece Qt**. Ele expõe uma API em C++ puro e emite eventos
por `std::function`. Isso mantém a possibilidade de um front-end alternativo (ou um CLI)
e deixa a UI substituível — requisito para o dia em que as skins entrarem.

## 4. Modelo de threads

| Thread | Responsabilidade | Regras |
|---|---|---|
| UI (Qt) | desenhar, receber input | nunca bloqueia; nada de I/O de disco |
| Controle (engine) | decodificar, preencher ring buffer, seek, estado | pode alocar e travar |
| PipeWire RT | callback de áudio | **sem** alocação, mutex, log ou syscall |
| Metadados | lê tags da playlist, uma faixa por vez | não decodifica nem abre codec |
| Biblioteca | varredura e escrita no SQLite | fila serializada, um escritor |
| Scrobble | HTTP, retry | backoff, fila persistida |
| MPRIS2 | `sd_event` + sd-bus | thread própria |

> A fila de metadados é separada da biblioteca de propósito: ela só lê cabeçalho
> (`avformat_open_input` + `find_stream_info`, sem `avcodec_open2`), então uma playlist
> de mil faixas fica legível em segundos e a UI recebe as tags uma a uma, como no Winamp
> original.

O único ponto de contato entre a thread de controle e a RT é um **ring buffer SPSC
lock-free** de `float` intercalado. Capacidade de 2 s (96.000 frames estéreo a 48 kHz);
o preenchimento inicial é de 200 ms antes de liberar o `play`, o que absorve jitter de
disco sem som de início atrasado.

O volume é aplicado **no callback RT** (ganho suavizado por amostra), não na thread de
decode, para que mover o slider não introduza latência de buffer.

## 5. Fluxo de áudio

```
arquivo → FFmpeg (avformat/avcodec)
        → swresample → F32 intercalado 48 kHz estéreo
        → [ganho] → [EQ 10 bandas] → [ReplayGain] → ring buffer
        → callback PipeWire → dispositivo
```

O formato interno é **sempre** F32 estéreo 48 kHz. Isso elimina ramificações de formato
em todo o pipeline: o resample acontece uma vez, na entrada, e o EQ e os plugins veem
sempre o mesmo layout. Arquivos de 44,1 kHz ganham uma conversão, mas é a conversão do
swresample — não a do servidor de áudio, que é onde o Wine engasga.

### 5.1 Gapless

A UI decide a próxima faixa **uma vez por troca** (`updateNextUp()`) e entrega o caminho ao
engine (`setNextTrack`). Com 2 s restantes o engine abre o decoder seguinte na thread de
controle; no `EOF` do atual ele apenas troca o ponteiro do decoder — o ring buffer, que
continua cheio, nunca é limpo e o PipeWire não para.

Os metadados não trocam no `EOF` (o áudio da faixa nova ainda está 2 s à frente no ring):
o `pull()` decrementa um contador com os frames que faltam tocar da faixa antiga e, quando
zera, a thread de controle publica a faixa nova e chama `onTrackChanged` — a UI move a
seleção e o título exatamente quando o som vira. Seek durante essa janela reabre a faixa
que o usuário está vendo, para que o reposicionamento não caia na faixa errada.

O padding do encoder já vem descartado pelo FFmpeg (`AV_PKT_DATA_SKIP_SAMPLES`), então
MP3 com tag LAME emenda no sample certo; FLAC/OGG emendam por construção.

## 6. Estado e eventos

```
        load()                play()
 Idle ─────────► Loading ─────────────► Playing
                   │                      │  ▲
                   │ erro                 │  │ pause()/resume()
                   ▼                      ▼  │
                 Error ◄── stop() ────  Paused
                                          │
                        fim do arquivo    ▼
                        ────────────► Idle (onEnded)
```

O engine emite um stream único de eventos (`onTrackChanged`, `onStateChanged`,
`onPosition`, `onEnded`, `onError`). Biblioteca, MPRIS2 e scrobbling **consomem esse mesmo
stream** — não há lógica de "tocou" duplicada em três lugares. É isso que garante que
playcount, "now playing" e scrobble concordem sobre o que aconteceu.

## 7. Persistência

| Dado | Formato | Caminho (XDG) |
|---|---|---|
| Configuração, sessão e scrobble | INI (hábito do Winamp) | `~/.config/amplia/amplia.ini`, permissão 600 |
| Cache de tags | SQLite (WAL) | `~/.local/share/amplia/library.db` |
| Fila de scrobble | JSON | `~/.local/share/amplia/scrobble-queue.json` |
| Janela/geometria | mesma INI | seção `[Window]` |

Esquema do cache de tags (`wamp/src/library/src/tag_cache.cpp`):

```sql
CREATE TABLE tracks (
  path TEXT PRIMARY KEY,
  size INTEGER NOT NULL,      -- tamanho e data do arquivo no momento da leitura
  mtime INTEGER NOT NULL,     -- se qualquer um mudar, o registro não vale mais
  title TEXT, artist TEXT, album TEXT, album_artist TEXT, genre TEXT,
  track_no INTEGER, year INTEGER,
  duration_ms INTEGER, sample_rate INTEGER, channels INTEGER, bitrate INTEGER
);
```

O cache é consultado ao montar a lista (sessão restaurada, `.m3u`, pasta arrastada):
o que bate em `path` + `size` + `mtime` entra direto em memória e **não** vai para a fila
de leitura. Só o que falta (arquivo novo ou alterado) é lido pelo FFmpeg; quando a leitura
termina, o registro é gravado dentro de uma transação confirmada a cada 32 faixas e no
fechamento (`closeEvent`). Sem isso, toda abertura relia o cabeçalho de cada arquivo.

Dentro do Flatpak esses caminhos viram `~/.var/app/io.github.cassiofiorin.Amplia/...` — o
código usa `XDG_CONFIG_HOME`/`XDG_DATA_HOME`, nunca caminho absoluto no home.

A INI é escrita com `QSettings` num caminho explícito (`QStandardPaths::ConfigLocation`
+ `/amplia/amplia.ini`), não pelo esquema organização/app do Qt, para o arquivo ficar onde a
tabela acima promete. Chaves já em uso: `play/volume`, `play/shuffle`, `play/shuffleLevel`
(percentual), `playlist/items`, `playlist/current`, `playlist/visible`, `window/width`,
`window/collapsedHeight`, `scrobble/lastfm*`, `scrobble/listenbrainz*`,
`scrobble/nowPlaying`.

## 7.1 Janelas

Existe **uma** janela: display, barra de posição, transporte e, embaixo, a lista. O
botão `PL` mostra a lista e faz a janela crescer `kPlaylistHeight` (300 px) para baixo;
fechar devolve a altura compacta.

O motivo é o Wayland: o cliente não posiciona nem move janelas — a geometria é do
compositor. Sem isso, o "acoplamento" do Winamp (arrastar uma janela e a outra ir junto)
não existe, e duas janelas aparecem como duas entradas na barra de tarefas. Com uma
janela só, "acoplado" é o estado natural.

Limite conhecido: se a janela estiver colada no rodapé da tela, os 300 px crescem para
fora da área visível — o app não pode reposicionar a janela para cima. No X11
(`QT_QPA_PLATFORM=xcb`) isso poderia ser corrigido com `move()`; no Wayland, não.

O tamanho é salvo como **largura + altura compacta** (`window/width` e
`window/collapsedHeight`), não como retângulo: assim, se a lista estava aberta ao fechar,
a próxima abertura não herda a altura grande com a lista escondida. A posição não é
guardada — no Wayland é do compositor e no X11 também seria restaurada pelo gerenciador
de janelas.

## 8. Controle de mídia (MPRIS2)

O player publica `org.mpris.MediaPlayer2.amplia` em `/org/mpris/MediaPlayer2`, com as duas
interfaces que o protocolo exige:

| Interface | O que expõe |
| --- | --- |
| `org.mpris.MediaPlayer2` | `Identity`, `DesktopEntry`, `CanRaise`, `CanQuit`, `Raise`, `Quit` |
| `org.mpris.MediaPlayer2.Player` | `PlaybackStatus`, `Metadata`, `Position`, `Volume`, `Shuffle`, `Can*`, `Play`, `Pause`, `PlayPause`, `Stop`, `Next`, `Previous`, `Seek`, `SetPosition`, `OpenUri` |

- Implementado com **sd-bus** (`libsystemd`), não com QtDBus: ver D-022.
- As funções da vtable chamam os mesmos ganchos da janela (`MprisHooks`), então não há
  estado duplicado: volume, sorteio e fila continuam sendo os da UI.
- O bombeamento do barramento entra no laço do Qt por `QSocketNotifier` sobre
  `sd_bus_get_fd()`; `sd_bus_process()` é chamado quantas vezes precisar até esvaziar.
  A notificação de escrita só liga quando `sd_bus_get_events()` pede `EPOLLOUT`; a de
  leitura fica sempre ligada (desligá-la deixaria o processo surdo à próxima mensagem).
- `Refresh()` também cuida das propriedades que mudam com a faixa: `CanGoNext` e
  `CanGoPrevious` (senão o applet do desktop acha que "avançar" está sempre indisponível
  e nem envia os cliques), além de `Shuffle` e `Volume`.
- `PlaybackStatus` e `Metadata` só emitem `PropertiesChanged` quando mudam de fato:
  `refreshStatus()` compara a string e `refreshMetadata()` compara
  `caminho|título|duração`, avançando o `mpris:trackid` a cada faixa nova.
- Cada método chama `sd_bus_reply_method_return()`; devolver sem responder derrubava a
  conexão (o chamador ficava pendurado e o barramento derrubava o nome).

## 9. Scrobbling

Interface única, implementações independentes:

```
PlayerEngine ──eventos──► ScrobbleController ──► LastFmSink
                                            └──► ListenBrainzSink
```

Implementação: `wamp/src/scrobble/` (`wamp::Scrobbler`, QObject + `QNetworkAccessManager`),
sem libcurl — o Qt Network já resolve HTTPS e o runtime do Flatpak o traz.

- **Last.fm**: desktop auth (`auth.getToken` → navegador → `auth.getSession`), session key
  persistida na INI (permissão 600); `track.updateNowPlaying` na troca de faixa e
  `track.scrobble` na metade/4 min; assinatura `md5(params ordenados + secret)`; POST
  form-urlencoded. Envio de um scrobble por requisição (o lote de 50 fica para quando
  houver fila acumulada).
- **ListenBrainz**: token no header, `POST /1/submit-listens` com JSON (`playing_now` e
  `single`), `GET /1/validate-token` para o botão de teste nas preferências.
- Fila persistida em `~/.var/app/io.github.cassiofiorin.Amplia/data/<app>/scrobble-queue.json`:
  o que falha por rede fica lá com contador de tentativas e *backoff* (30 s × tentativa, até
  10 min) e é reenviado no próximo flush.
- Regra: `qualifiesForScrobble()` — duração > 30 s **e** (50% da faixa ou 4 min). O tempo
  tocado é somado a cada tique de 200 ms (saltos > 5 s são tratados como seek e não contam);
  ao atingir o limiar o scrobble sai na hora, e uma reprodução fica marcada como enviada.
- Sessão começada só quando a faixa realmente toca (carregar em pausa não conta tempo).
- `updateNowPlaying` é opcional (`ScrobbleConfig::announceNowPlaying`, desligado por padrão,
  caixa em Preferências ▸ Scrobbling). O Last.fm não tem chamada para encerrar o anúncio,
  então ele fica no perfil até outro scrobble chegar; por isso o padrão é não anunciar, e
  quem liga só vê o anúncio depois de 5 s tocando.
- Sem tags de artista/título não há envio: melhor não registrar do que registrar lixo.

## 10. Empacotamento

Flatpak local, runtime `org.kde.Platform//6.10` (traz Qt 6). Permissões mínimas:

| Permissão | Motivo |
|---|---|
| `--socket=wayland`, `--socket=fallback-x11` | janela |
| `--socket=pulseaudio` | saída via PipeWire-pulse (fallback) |
| `--filesystem=xdg-run/pipewire-0` | PipeWire nativo |
| `--share=network` | Last.fm / ListenBrainz |
| `--filesystem=home` | ler a biblioteca e gravar playlists `.m3u` junto da música |
| `--talk-name=org.mpris.MediaPlayer2.*` | publicar MPRIS |

`home` é leitura **e** escrita: o Winamp grava `.m3u` ao lado da música, e a
biblioteca da F3 escreve em `$XDG_DATA_HOME` (que o Flatpak redireciona para
`~/.var/app/io.github.cassiofiorin.Amplia/`). Se a escrita no home incomodar, dá para
voltar a `:ro` e mover os `.m3u` para o diretório do app.

## 11. Riscos conhecidos

1. **FFmpeg no SDK**: presença dos headers é a primeira coisa a verificar na F0.
2. **PipeWire no sandbox**: `pw_stream` funciona, mas exige a permissão de socket; o
   fallback é ALSA nativo dentro do sandbox.
3. **Biblioteca**: o modelo de dados do Winamp (`ml_local`) é COM/Wasabi e não tem SQLite;
   reimplementar é mais barato do que portar, mas perde-se casamento com plugins antigos.
4. **Skins (F5)**: decidir entre escrever um leitor `.wsz` próprio e ressuscitar o `bfc`.
   Nada do que foi feito até a F4 impede qualquer dos dois caminhos.
