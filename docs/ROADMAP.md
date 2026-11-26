# Roadmap

Cada fase tem um critério de pronto verificável. Uma fase só começa quando a anterior
toca de ponta a ponta — nada de três frentes abertas ao mesmo tempo.

Cada fase concluída publica uma versão com **todos os formatos de empacotamento** disponíveis
naquele momento (D-130). Hoje só existe Flatpak; a F8 cria os outros.

**Fila:** F6c (fechado) → **F8 (empacotamento)** → F3.1 (capa de álbum) → F4 (plugins) →
F7 (skins). A busca de tags (seção "F6 — Busca de tags") segue em pausa.

## F0 — Primeiro som nativo

Objetivo: provar o pipeline inteiro (decode → ring buffer → PipeWire) dentro do sandbox.

- [x] Manifest Flatpak compila (Qt 6 + FFmpeg + PipeWire disponíveis no SDK)
- [x] `PlayerEngine` toca um MP3, com play/pause
- [x] Seek funciona (flush do ring + `av_seek_frame`)
- [x] Janela Qt mínima com play/pause, slider e tempo
- [x] Position reportada corretamente após seek
- [x] Arquivo aberto pela linha de comando entra em reprodução automática

**Pronto quando:** um MP3 toca dentro do Flatpak instalado, com seek, sem clique audível
na troca de posição.

Verificado com `flatpak run --command=wamp-smoke io.github.cassiofiorin.Amplia <arquivo>`
(play, pausa com posição congelada, retomada, stop e seek).

**O que ficou de lição:** o SDK do KDE 6.10 tem tudo (`libavformat` 61.7.100,
`libpipewire-0.3` 1.4.9, Qt 6.10.3) — o plano B de empacotar FFmpeg não foi preciso. O
risco real foi outro: `org.flatpak.Builder` não enxerga SDK instalado só na instalação de
usuário do Flatpak (ver D-013).

---

## F1a — Player e playlist (concluída)

Objetivo: transformar o F0 num player utilizável no dia a dia, sem depender de nada
externo. Concluída em 2026-11-19, com build, `wamp-smoke` e uso real.

- [x] Playlist com `.m3u`/`.m3u8` (ler e gravar)
- [x] Arrastar arquivos para a janela (pastas ainda não)
- [x] Fila separada da playlist: enfileirar, consumir na próxima, contagem na barra
- [x] Próxima/anterior, teclas Z/X/C/V/B, fim de faixa avança a playlist
- [x] Fila de leitura de metadados em thread própria, tags na lista conforme chegam
- [x] Número da faixa na lista e posição na fila à direita (como o JTFE)
- [x] Preferências (Ctrl+P) e sorteio com nível, botão `⇄` na barra
- [x] Sorteio sem repetir a faixa atual nem as últimas tocadas (janela por nível)
- [x] Enfileirar com `Alt+clique esquerdo` e menu de contexto na lista
- [x] `Enter` toca a selecionada, `Delete` remove; rodapé com menu **Gerenciar ▾**
- [x] Clique no slider de posição pula para o ponto clicado (`SeekSlider`)
- [x] Playlist dentro da janela principal, crescendo para baixo (uma entrada na barra)
- [x] INI inicial: volume, sorteio, nível, geometria e estado da lista (`~/.config/amplia/amplia.ini`)

**Pronto quando:** dá para ouvir um álbum inteiro, montar fila e salvar/abrir `.m3u`
sem trocar de app. ✅

---

## F1b — Integração (concluída)

Concluída em 2026-11-19, validada no uso real: gapless audível sem gap, teclas de mídia,
applet do KDE, Last.fm e ListenBrainz recebendo escutas.

Objetivo: o Amplia passar a ser *o* player do sistema — teclado de mídia, applet, scrobble
e troca de faixa sem silêncio. Ordem pensada para não refazer trabalho de áudio.

1. **Gapless** (core) — ✅ feito em 2026-11-19: o decoder da próxima faixa abre ~2 s antes
   do fim e o ring buffer atravessa a troca sem parar o PipeWire; os metadados só trocam
   quando a faixa nova começa a soar. Veio primeiro porque mexe na alimentação do ring
   buffer — o mesmo trecho que o equalizador vai ocupar depois.
2. **MPRIS2** (integração) — **feito**: player, metadata, volume, sorteio, seek, next/previous: `org.mpris.MediaPlayer2.amplia` com `Play/Pause/Next/Previous`,
   `Metadata` (`xesam:title/artist/album`, `mpris:length`) e
   `Position`. Sem D-Bus de entrada (controle remoto) nesta rodada.
3. **Scrobbling** — **implementado em 2026-11-19**: módulo `wamp/src/scrobble/` com
   `wamp::Scrobbler` (Qt Network, sem libcurl), regra >30 s e 50%/4 min, fila em JSON com
   retry em backoff, `track.updateNowPlaying`/`track.scrobble` (Last.fm) e
   `/1/submit-listens` (ListenBrainz). Configuração em `Preferências ▸ Scrobbling`, com
   "Conectar com o Last.fm…" (auth.getToken → navegador → auth.getSession) e teste de token
   do ListenBrainz. A session key fica na INI com permissão 600. Validado no uso real: as
   duas plataformas recebendo escutas. O anúncio de "ouvindo agora" existe, mas nasce
   desligado (`scrobble/nowPlaying`) — ver D-027.
4. **Extras da rodada**: título da janela com a faixa tocando, caixa de "ouvindo agora"
   opcional, correção da altura compacta da janela.

**Pronto quando:** tocar um álbum inteiro sem gap, com scrobble correto no Last.fm e
controles funcionando pelo MPRIS (teclado de mídia / applet do desktop). ✅

---

## F1c — Formatos e última milha (concluída)

Objetivo: fechar o que falta para o player não pedir outro programa em nenhuma tarefa do
dia a dia. Não entra áudio novo aqui — o equalizador é a F2.

- [x] `.pls` (ler e gravar) — o formato de rádio, com `File1=`/`Title1=`/`Length1=`
- [x] `.b4s` (ler e gravar) — playlist XML do Winamp 5
- [x] `.wpl` (ler e gravar) — Windows Media Player Playlist, o que o Windows usa hoje
- [x] Cache de tags em SQLite: reabrir a playlist não relê os arquivos
- [x] Arrastar **pastas** para a janela (varre recursivamente os arquivos de áudio)
- [x] Última playlist ao abrir: restaurar a lista e a faixa atual da sessão anterior
- [x] Menus **Arquivo / Playlist / Ordenar** no rodapé, com os atalhos do Playlist Editor
- [x] **Jump to File** (tecla `,`): busca sem acento, multi-palavra, Enter toca, Ctrl+Enter enfileira
- [x] **File Info** (Alt+3) com TagLib: leitura e edição dos campos, aba Artwork, gravação segura
- [x] Colunas da lista: duração em coluna própria e opção de caminho completo
- [x] `mpris:artUrl` com a **capa embutida** (ID3v2 APIC, FLAC picture, MP4 covr)
- [x] `mpris:artUrl` com `cover.jpg`/`folder.jpg` da pasta quando não houver capa embutida

**Pronto quando:** abrir um `.pls` de rádio, salvar `.b4s`/`.wpl`, arrastar uma pasta de
álbum e reabrir o app com a playlist de ontem — sem sair do Amplia. ✅ (concluída em
2026-11-19, com build e `wamp-smoke` verdes)

---

## F2 — Equalizador (concluída)

- [x] Filtros de 10 bandas (70 Hz … 16 kHz), F32, no caminho de decode — feito em 2026-11-19
- [x] 17 curvas pré-definidas no diálogo (o Flat saiu, quem zera é o botão)
- [x] Presets `.eqf` (import) e CSV (import/export) — 2026-11-19
- [x] Curvas salvas pelo usuário, listadas no combo — 2026-11-19
- [x] ReplayGain (faixa/álbum), lido das tags, com teto de pico — 2026-11-19
- [x] UI do EQ: dez sliders com valor em dB, pré-amp e predefinições
- [x] Curva desenhada na UI, com arrasto dos pontos — 2026-11-19
- [x] Timbre do EQ no formato do EQ10: `g = 10^(dB/20) − 1`, Q 1.41 com reforço dobrado e corte pela metade — 2026-11-19

**Pronto quando:** importar um `.eqf` do Winamp reproduz a mesma curva audivelmente e os
presets sobrevivem ao restart.

---

## F2B — Display do player (concluída)

A moldura onde o visualizador vai morar. O painel substituiu o título e a barra de seek, e a
duração passou a ficar no nome da faixa (`Artista - Título (4:54)`), como no original.

- [x] **Display de sete segmentos** com relógio (casa dos minutos reservada, sem deslocar ao
      cruzar `9:59`), glifo de estado e barra de posição clicável que alterna
      decorrido/restante no clique do relógio — 2026-11-20
- [x] `KBPS`/`KHZ`/`ESTÉREO`/`MONO` a partir das tags — 2026-11-20
- [x] LEDs `EQ`/`REP`/`RAND` clicáveis, com os glifos de laço e 1‑2‑3 acesos/apagados — 2026-11-20
- [x] Transporte circular com mute, ações empilhadas à direita do display, volume alinhado à
      barra de posição — 2026-11-20
- [x] Artista e título em duas linhas com rolagem independente, duração fixa à direita da
      barra — 2026-11-20

**Pronto quando:** o painel mostra estado, tempo e formato sem tirar espaço da lista.
Validado em 2026-11-20 (D-052, D-053, D-054, D-055).

---

## F2C — Visualização (medidor, espectro e beat) — concluída

Depois do equalizador de propósito: o visualizador mede o sinal **depois** do EQ, então fazer
antes obrigaria a mexer duas vezes no mesmo ponto do caminho de áudio.

- [x] Medidor de intensidade **por canal** (L/R): pico + RMS, com ataque rápido e queda
      suavizada (as barras do painel do Winamp moderno).
- [x] **Espectro de bandas** (16–24 bandas logarítmicas, 20 Hz … 16 kHz): FFT de 1024 pontos
      com janela Hann, suavização e "peak hold".
- [x] Indicador de **beat**: energia dos graves contra a média recente, com histerese e
      tempo de espera para não piscar em todo transiente.
- [x] **Calibração de ~10 s**, em Preferências ▸ Visualização ou na primeira abertura do
      visualizador: pinta no tamanho real da área, descarta o primeiro segundo (aquecimento),
      mede o custo por quadro em janelas de 1 s e **nivela por baixo** — o fps escolhido é o
      que sobrevive à janela mais cara (p95 do custo) com 40% de folga, não a média do momento
      bom. Grava o fps, o custo p95 e o consumo de CPU medido.
- [x] Calibração mostra progresso, pode ser **cancelada**, e a pintura continua normal durante
      a medição (as barras não congelam). Roda uma vez; o número fica na INI.
- [x] **Padrão sem calibração: 15 fps.** Baixo o bastante para não pesar em máquina nenhuma e
      ainda suficiente para o medidor parecer vivo; depois da calibração vale o valor medido.
- [x] **Slider manual** de 10 a 60 fps, com prioridade sobre o valor calibrado; "Automático"
      usa o número medido, e a calibração pode ser refeita quando o valor faltar ou envelhecer.
- [x] Análise **pausada por completo** quando a janela está oculta na bandeja.
- [x] Desligável, com a escolha guardada na INI.

**Fora de escopo:** capturar a saída do sistema (PipeWire capture) para visualizar outros
apps — exigiria permissão de captura e mostraria áudio alheio. O visualizador da F2C mede só
o sinal do Amplia.

As animações usam constantes de tempo (ms, dB/s), nunca "por quadro": assim 15 fps e 60 fps
mostram a mesma velocidade de queda, só com passos diferentes.

**Pronto quando:** com uma música tocando, as barras L/R acompanham a intensidade, o espectro
responde às bandas, o beat pisca no ritmo, e a taxa de atualização se ajusta sem comer CPU
com a janela fechada.

## F2D — Aparência (esquema de cores)

Camada "nostálgica" sem renderização de skin: o Amplia não abre `.wsz`, não desenha bitmap e
não muda a forma da janela — ele troca **paleta**. É a F2D, e não a F5, porque o motor de
skin de verdade continua sendo outro projeto.

Menu **Aparência ▸ Esquema de cores**, com itens de rádio e aplicação imediata. Os nomes
descrevem a cor, não a skin de origem (nada de "Bento" ou "Modern" como nome de produto):

- **Verde LCD** — o clássico preto/verde do 2.x
- **Âmbar** — o do "BEAT", quente sobre preto
- **Grafite** — o cinza-azulado escuro das skins modernas (parecido com o atual)
- **Azul aço** — frio, com acento claro
- **Papel** — claro, para quem usa o desktop claro

- [x] Centralizar os tokens de cor num só lugar (`src/ui/theme.h`): moldura, lista e display.
- [x] Gerar o stylesheet a partir dos tokens; `PlaylistDelegate`, `RoundButton`, `EqCurveView`
      e `DisplayPanel` leem a paleta ativa.
- [x] Presets como dados (nome + paleta), aplicáveis sem reiniciar.
- [x] `appearance/theme` na INI e menu **Aparência** com itens de rádio.
- [x] EQ (F2), display (F2B) e visualizador (F2C) leem as cores do tema.
- [x] **Janela Aparência** (Opções ▸ Aparência…, `Ctrl+,`): lista de esquemas + editor de
      quatro bases + contraste, aplicação imediata, Reverter e Excluir.
- [x] Tema **Clássico** (por contraste, sem relevo).
- [ ] **F2D.1 — Acoplamento de janelas (só X11)**, parado por decisão: trilha por delta com
      âncora e seguidoras, encaixe 8 px / desencaixe 40 px. Wayland não permite.
- [x] **Janelas auxiliares**: playlist e equalizador fora da janela principal (F2D).
- [x] **F2D.2 — Dois modos de playlist** (`playlist/mode`): **integrada** (padrão; a lista é
      painel da janela principal e o `PL` cresce/encolhe a janela) e **janela própria**
      (molde do porte de Windows). Troca na hora, geometria por modo, teclas da lista seguem
      o painel (D-083).
- [x] Importar/exportar temas por CSV (teto de 30 por arquivo e na aplicação), "modelo
      CSV" e "Salvar tema atual como…".
- [ ] Aviso de contraste baixo no editor.
- [ ] "Seguir o sistema" (paleta do KDE) como opção — ainda não.
- [ ] Contraste mínimo de 4,5:1 conferido por cálculo em cada preset.

**Fora de escopo:** imagem de fundo, gradiente, janela não-retangular, fonte bitmap e
qualquer leitura de arquivo de skin — isso é a F5.

**Pronto quando:** trocar o esquema repinta janela, lista, EQ e visualizador na hora, e a
escolha sobrevive ao restart. *(Atingido em 20/11, com "seguir o sistema" pendente.)*

---

## F3 — Biblioteca

- [x] Esquema SQLite + migrações (reusa `library.db`; tabela `roots`)
- [x] Janela com lista, duração e o mesmo contador de metadados do app
- [x] Adicionar pasta… + Atualizar + tocar direto da lista
- [x] Varredura incremental de pastas (mtime): só o arquivo novo ou alterado é reaberto
- [x] Árvore de visões do ML (todas, áudio, recentes, nunca tocadas, mais tocadas, melhor avaliadas)
- [x] Busca por texto com `LIKE` (FTS5 fica para quando a coleção passar de ~10 mil)
- [x] Filtros combinados por artista/álbum/gênero (três caixas que se somam à busca)
- [x] Fila de indexação persistida (`scan_queue`) + retomada na abertura
- [x] Playcount e "última reprodução" (regra de 350 ms, como o ML original)
- [x] Nota 0–5 no banco (estrelas desenhadas na tela)
- [ ] Capa de álbum extraída das tags + cache → **mudou de fase: F3.1**, depois do README

**Pronto quando:** 10 mil faixas indexadas em menos de um minuto e busca respondendo em
menos de 100 ms. **Adiado:** a medição fica para quando houver uma máquina e um disco de
referência; a coleção de desenvolvimento tem 7.650 faixas e o critério não bloqueia a fase.

A Biblioteca está **fechada**: a varredura é incremental (compara tamanho e mtime antes de
reabrir) e os filtros de artista/álbum/gênero combinam com a busca e entre si (D-129).

---

## Reordenação de 26/11 — o caminho depois do 1.0

Reordenado a pedido do autor, depois da 1.0.0: o app deixou de ser "um Winamp para Linux" e
passou a ser música local com sensação de comunidade. O que ordena as fases agora é o que
**aumenta o valor do que já existe** — identidade da faixa, dados compartilhados e alcance —
e o que sobra para o fim é o que custa muito e serve a pouca gente.

| Nova | Era | Entrega |
|---|---|---|
| **F4** | F4 (plugins) + F3.1 (capa) | Tags, capa e identidade: autotag por AcoustID/MusicBrainz, gravação de tags, capa embutida e `folder.jpg`, fingerprint como desempate |
| **F5** | — | Análises compartilhadas (BPM, tom e confiança por obra) — [`COMPARTILHAMENTO.md`](COMPARTILHAMENTO.md) |
| **F6** | — | Perfis compartilhados (humor, mistura e léxico com nome e autor) |
| **F7** | F8 | Empacotamento e alcance: AppImage, `.deb`, `.rpm`, portátil, Flathub **e Windows** |
| **F8** | F7 | Skins clássicas `.wsz` — vai ao fim da fila; projeto aberto pode ser assumido por outra pessoa |
| **F9** | F4 | Plugins `.so` — adiado |
| depois | F6 | Busca de tags — reavaliar depois do autotag, que deve encolher o caso de uso |

Por que esta ordem, em uma linha cada:

- **F4 primeiro** porque a identidade (tag normalizada + fingerprint) é a **dependência** das
  duas fases seguintes: sem ela não existe chave de obra para compartilhar.
- **F5 antes de F6** porque dado de análise é alicerce; perfil é empréstimo de gosto e só faz
  sentido quando houver com o que comparar.
- **F7 com Windows dentro** porque a comunidade é o produto agora, e comunidade se mede em
  gente que consegue instalar. É a fase cara; ela vem depois de o dado existir, para que
  quando os pacotes saírem todos já encontrem o dataset pronto.
- **Skins no F8** por custo alto e uso estreito, e porque aceitamos que alguém da comunidade
  pode assumir.
- **A F3.1 foi absorvida pela F4**: capa é leitura de tag e arquivo ao lado; autotag é escrita
  de tag. Mexer no mesmo código duas vezes não se paga.

---

## F4 — Tags, capa e identidade

A base das duas fases seguintes e o que fecha a Biblioteca de verdade: metadado confiável e
uma identidade de faixa que sobrevive a mudar de máquina.

- [x] **Identidade** `norm/v1` como função pura, com testes: mesma obra vira a mesma chave em
      rip, remaster e caminho diferente (regras em [`COMPARTILHAMENTO.md`](COMPARTILHAMENTO.md) §2)
      — em `src/library/.../work_key.h`, testada em `tests/work_key_test.cpp` e rodando no CI
- [ ] **Fingerprint** (`libchromaprint` empacotado) e consulta ao AcoustID → MusicBrainz
- [x] **Busca por texto** no MusicBrainz como primeiro passo do autotag: `autotagScore` /
      `rankCandidates` / `classifyTagMatch` em `src/library/.../autotag.cpp` (testados em
      `tests/autotag_test.cpp`) e cliente com fila de 1,1 s e user agent próprio em
      `src/ui/musicbrainz.cpp` (analisador de resposta testado com JSON gravado). A primeira
      consulta real devolveu 15 candidatos para Foo Fighters — "All My Life", com margem 0,00,
      ou seja: caso de revisão, não de gravação automática.
- [ ] **Diálogo "Buscar tags…" (refino)**: hoje ele já abre no Alt+3, consulta, lista os
      candidatos com pontuação e motivo, e devolve a escolha preenchendo só os campos vazios.
      Falta a comparação campo a campo com caixa por campo e a busca pelo disco inteiro
      (`searchReleases` existe, sem botão).
- [x] **Capa de álbum**: embutida (`APIC`) e `folder.jpg`/`cover.jpg` da pasta, cache por álbum
      com limite de tamanho, mostrada no painel do player, no Alt+3 e na lista — `CoverCache`
      por álbum (chave `norm/v1`, extração fora da thread da interface, nunca amplia arte
      pequena), miniatura de 18 px na lista com tooltip de 160 px, e capa no painel por
      revelação progressiva (entra quando sobra a largura do quadrado; a altura acompanha a
      faixa de barras). O Alt+3 já tinha a aba *Artwork* desde antes. Revisado com a biblioteca
      real (Bowie, Foo Fighters).
- [x] **Diário do lote e desfazer**: `<CacheLocation>/autotag/<carimbo>/journal.json` guarda os
      valores anteriores de cada campo, e a capa é copiada ao lado quando o lote mexeu nela.
      Cinco lotes ficam disponíveis; `readLatestJournal` acha o mais recente pelo carimbo e
      `pruneJournals` apaga o resto. Testado em `tests/tag_journal_test.cpp`.
- [ ] **Tela do lote**: as pilhas "prontas / revisar / ignoradas", execução de 250, retomada de
      onde parou e o botão de desfazer amarrado ao diário (o motor existe; falta a janela).
- [ ] **Desempate de gravação** por fingerprint, consumido pela F5 quando o fingerprint existir

**Regras do autotag (o arquivo do autor é o caso de teste).** O acervo oficial não cobre
gravação não oficial: um rip de transmissão de TV (o Lollapalooza do Foo Fighters do autor, que
circulou por P2P e nunca entrou em catálogo) **não** existe no MusicBrainz, e isso é normal, não
é erro. Por isso:

- **Nada é apagado.** Não achar resultado é resposta válida: o diálogo diz "nada encontrado para
  esta gravação" e o arquivo fica exatamente como estava.
- **Nenhum campo é esvaziado**, nem por candidato com campo vazio: campo vazio na fonte nunca
  escreve nada no arquivo.
- **Só o que foi escolhido é gravado**: comparação campo a campo, com caixa de seleção por
  campo, e o padrão é **não** sobrescrever o que já existe.
- **Nunca se grava o bloco inteiro**: o `TagEditor` atualiza só os campos confirmados, mantendo
  comentário, replay gain e o resto.
- **A cópia temporária continua obrigatória** (a "gravação segura" que já existe), e a falha no
  meio volta o original.
- **Gravação não invalida análise**: mudar a tag muda o `mtime`, então a varredura incremental
  relê a faixa — BPM e tom ficam, a capa em cache é regerada.
- **Bootleg sem registro oficial continua sendo obra**: a chave de `norm/v1` vem da tag, não do
  MusicBrainz — então o dataset compartilhado funciona justamente para o que nunca entrou em
  catálogo.

**Desenho do autotag (decidido em 26/11).**

- **Porta de entrada é o texto**; o fingerprint entra como penúltimo item, só para desempatar
  gravação (e é o que a F5 consome).
- **Gravação silenciosa** só quando: score do melhor candidato ≥ 0,90 **e** margem ≥ 0,15 sobre
  o segundo; duração dentro de ±3 s; e **apenas campos em branco** no arquivo (título, artista,
  artista do álbum, álbum, faixa, disco, ano). Nada preenchido é tocado.
- **Capa** entra sozinha quando o arquivo não tem nenhuma embutida e a pasta não tem
  `folder.jpg`/`cover.jpg`, e o candidato tem arte no Cover Art Archive.
- **Unidade de revisão é o álbum**, agrupado pelo `norm/v1` de artista+álbum; faixa solta (álbum
  em branco) cai no grupo do **diretório** e, se nem isso, vira busca por faixa — que é a mais
  cara e por isso fica atrás de uma opção explícita.
- **Janela por execução: 250 faixas** (padrão, ajustável), com progresso e "continuar de onde
  parou" — o MusicBrainz aceita 1 requisição por segundo, então a fila é persistida em vez de
  ser um laço bloqueante. O número não é só sobre a API: um lote pequeno cabe na memória de
  uma máquina modesta (leitura de tag, hash e decodificação de capa por faixa), e cada execução
  termina com um resultado visível — o resumo do que mudou —, o que faz o trabalho acumulado
  aparecer em vez de virar um processo longo e opaco.
- **Desfazer o último lote é obrigatório**: diário em JSON por lote com os valores anteriores
  dos campos tocados (e o marcador de capa adicionada), guardado em
  `<CacheLocation>/autotag/<lote>/journal.json`. Desfazer reescreve os valores anteriores e
  remove a capa que o lote pôs. Guarda os últimos 5 lotes. Não é cópia do arquivo: restaura
  valores, não bytes — o `mtime` muda e a varredura incremental relê a faixa, como qualquer
  gravação de tag.
- **Resumo ao fim de cada lote**: quantas preenchidas, quantas para revisar, quantas sem
  correspondência (não achar é resposta válida).
- **Tag recém-escrita sai da busca.** Toda gravação feita pelo app (Alt+3, autotag, desfazer)
  carimba `tracks.tag_fixed_at` com o `mtime` do arquivo naquele instante. A fila do autotag
  **ignora** a faixa enquanto o `mtime` do disco for igual ao carimbo — ou seja, enquanto o
  arquivo estiver exatamente no estado em que o app o deixou. Se algo mudar a tag por fora, o
  `mtime` muda, a varredura incremental relê e a faixa volta a ser elegível.
- **O aviso é parte do retorno**: ao gravar, a barra de status diz "Tag atualizada — fora da
  busca até o arquivo mudar"; no relatório do lote entra a linha "12 ignoradas (tag corrigida
  pelo app)". O diálogo de busca mostra a pilha *ignoradas* com o motivo e um "incluir mesmo
  assim" para quem quiser forçar.
- **"Buscar por outra referência" dentro do Alt+3.** O diálogo não é um botão que devolve um
  veredito: é uma consulta com campos **editáveis** (artista, título, álbum, pré-preenchidos com
  a tag atual). Quando o casamento automático é baixo — ou não existe —, o usuário escreve ali
  o nome que ele *sabe* que a gravação tem, busca de novo e escolhe da lista. A escolha humana
  **vence a pontuação**: aplica mesmo com score baixo, porque quem confirmou foi ele.
- **A pontuação fica visível, não decisória**: cada candidato mostra quais campos casaram
  (mesmo artista, mesmo álbum, duração próxima, ano próximo) — é o mesmo vocabulário das razões
  da estação, para o usuário julgar com o mesmo olho.
- **O diário registra a origem**: `source: auto` (regra silenciosa) ou `source: user` (escolha
  manual). Desfazer funciona igual nos dois; o relatório distingue os dois números.
- **A referência manual pode ser lembrada**: por padrão o diálogo prefere a tag atual; se o
  usuário buscou por outro texto, o próximo Alt+3 daquela faixa abre com a referência usada da
  última vez (nunca sobrescrevendo a tag com ela).


**Pronto quando:** um arquivo com tag errada é corrigido pelo diálogo e a capa aparece no
Alt+3 sem abrir o arquivo duas vezes.

---

## F5 — Análises compartilhadas

O BPM e o tom medidos aqui servem a quem ainda não tocou a faixa. A chave é a **obra**, nunca o
caminho; o formato e as regras de confiança estão em
[`COMPARTILHAMENTO.md`](COMPARTILHAMENTO.md) §3 e §4.

- [ ] **Exportar** por biblioteca ou por artista, em TSV `norm/v1` (só `analyzed = 1`)
- [ ] **Importar** com prior herdado (`analyzed = 2`, confiança × 0,7), transação única e resumo
- [ ] **Leitura do banco** separando medida e herdada (o rodapé já conta medidas; ganha as herdadas)
- [ ] **Mesmo validador** no app e no CI do repositório de dados

**Pronto quando:** exportar no desktop e importar no notebook deixa as faixas com BPM e tom sem
tocar nenhuma delas — e a primeira que tocar substitui o herdado pela medição local.

---

## F6 — Perfis compartilhados

O gosto cabe num arquivo: agitação, tom, estabilidade, mistura e léxico, com nome e autor. É o
`HumorPreset` que já existe, exportável ([`COMPARTILHAMENTO.md`](COMPARTILHAMENTO.md) §6).

- [ ] **Exportar perfil** a partir de um preset salvo (`amplia-perfil/v1`, JSON)
- [ ] **Importar perfil** como preset salvo, com o autor no nome; presets de fábrica não mudam
- [ ] Nada de histórico sai do banco: nem skip, nem playcount, nem data

**Pronto quando:** um perfil salvo numa máquina abre na outra e toca a mesma estação.

---

## F3.1 — Capa de álbum (absorvida pela F4 em 26/11)

Saiu da F3 a pedido do autor: é semi-cosmético e não muda achar música.

- [ ] Ler a capa embutida nas tags (e a `folder.jpg`/`cover.jpg` da pasta como queda)
- [ ] Cache em disco por álbum, com tamanho limitado
- [ ] Mostrar no painel do player, no Alt+3 e na lista da Biblioteca

**Pronto quando:** a capa aparece no Alt+3 sem abrir o arquivo duas vezes.

---

## F9 — Plugins `.so` (era F4; adiado em 26/11)

- [ ] ABI C versionada (`wamp_plugin_v1`)
- [ ] Pontos de extensão: DSP e visualização
- [ ] Carregamento por diretório de plugins + isolamento de falha (plugin não derruba o app)
- [ ] Plugin de exemplo no repositório

**Pronto quando:** um plugin de DSP externo altera o som sem recompilar o app.

---

## Atalhos do Winamp (a F5 antiga — concluída)

Renumeração de 20/11: a F5 é a paridade de **teclado** com o Winamp 2.x, que é barata e
aparece a cada uso — e fica na frente de funcionalidades grandes.

**Já implementado**

| Tecla | Ação |
|---|---|
| `Espaço` | tocar/pausar |
| `X` / `C` / `V` | tocar / pausar / parar |
| `Z` / `B` | anterior / próxima |
| `L` / `Shift+L` | adicionar arquivos / pasta |
| `Ctrl+O` / `Ctrl+N` / `Ctrl+S` | abrir playlist / nova / salvar |
| `,` | ir para a faixa (Jump to File) |
| `Alt+3` / `Alt+G` / `Ctrl+P` | informações / equalizador / preferências |
| `S` / `R` / `Ctrl+Shift+R` | sorteio / repetir / embaralhar lista |
| `Delete` / `Ctrl+Delete` / `Ctrl+Shift+Delete` | remover / manter só a seleção / limpar |
| `Ctrl+R` / `Ctrl+Shift+1..3` | inverter / ordenar por título, arquivo, caminho |
| `Alt+Delete` | remover inexistentes |

**A implementar (mapa clássico)**

- `←` / `→` → retroceder/avançar 5 s; `↑` / `↓` → volume.
- `1`…`9`, `0` → pular para 10%…100% da faixa.
- `Ctrl+T` → alternar decorrido/restante no relógio.
- `J` → abrir o Jump to File (hoje é `,`; manter os dois).
- `Ctrl+J` → "ir para o tempo…" (diálogo com mm:ss).
- `Ctrl+L` → adicionar URL.
- `Alt+W` / `Alt+E` / `Alt+L` → mostrar janela, editor de playlist, biblioteca.
- `T` (sempre no topo), `D` (double size — depende da F7), `Esc` (fechar diálogos).
- `Ctrl+A` selecionar tudo (hoje `Ctrl+Alt+A`), `Ctrl+Alt+A` como alias.
- `R` já cicla repetir; falta o terceiro estado ser anunciado na barra.

**Fora de escopo:** teclas que só faziam sentido com janelas que não temos (skin browser,
visual plug-in, ML) e macros/plugins de teclado.

**Pronto quando:** o mapa clássico acima funciona sem conflito, e a janela de atalhos
(`Ctrl+?` ou Ajuda ▸ Atalhos) lista o que existe.

## F6B — Busca de tags (reavaliar depois do autotag)

Avaliada em 20/11 e **parada a pedido do usuário** ("chegando lá avaliamos o custo").
Rotas possíveis: busca por texto (MusicBrainz, 1 req/s, User-Agent próprio) ou texto +
fingerprint (AcoustID) — a segunda exige empacotar `libchromaprint`, que não existe no
runtime `org.kde.Platform`. UI pensada: File Info → "Buscar tags…", lista de candidatos com
confiança, comparação campo a campo, nunca sobrescrever sem confirmar, gravação pelo
`TagEditor`, capa do Cover Art Archive como opção separada.

## F8 — Skins clássicas (`.wsz`) (era F7; fim da fila desde 26/11)

Renumerada para **F7** em 20/11 (a F5 ficou reservada para os atalhos e a F6 para a busca
de tags, que está em pausa).

Alinhado em 20/11: **só skins clássicas**, que é o que o acervo público guarda — o
[Winamp Skin Museum](https://skins.webamp.org/) expõe mais de 100 mil `.wsz` vindos da
coleção do Internet Archive. Skins "modern" (`.wal`, Wasabi XML + Maki) ficam de fora: são
um motor de interface inteiro, e o próprio autor do Webamp desistiu delas.

O que uma skin clássica **é**, na prática: um conjunto de bitmaps com coordenadas fixas
(`main.bmp`, `cbuttons.bmp`, `numbers.bmp`, `text.bmp`, `posbar.bmp`, `volume.bmp`,
`shufrep.bmp`, `monoster.bmp`) mais um `viscolor.txt` com os 24 tons do espectro. O visual
é a imagem; o comportamento continua sendo o nosso.

**Consequência do Wayland (medida em 20/11, D-081):** o protocolo não permite posicionar
janela nem esconder uma janela da barra de tarefas — só diálogos **modais** recebem
`xdg_toplevel.set_parent`. Logo, o modelo original de **três janelas acopladas** (main, PL, EQ
se movendo juntas) não é reproduzível no Wayland. O caminho é a skin em **janela única**: a
imagem compõe main + playlist + EQ como painéis internos, arrastáveis *dentro* da janela
(é o que o Webamp faz no navegador). Multi-janela fica como recurso **só de X11**.

**Consequência de arquitetura:** skin é um **modo de UI alternativo**. Não dá para "vestir"
widgets Qt com esses sprites — em modo skin a janela inteira passa a ser composta com
`QPainter` a partir dos recortes, com regiões de clique mapeadas. O layout moderno fica
desativado enquanto uma skin está ativa, como no original.

- **F5a — janela principal**: leitor do `.wsz` (zip, com fallback para BMP/PNG soltos),
  `main.bmp`, transporte com estados, números, posbar, volume, `viscolor.txt` no espectro.
  *2–3 sessões.*
- **F5b — janelas auxiliares**: playlist editor e equalizador em janelas próprias, fontes
  bitmap, `pledit.txt`. *2 sessões.*
- **F5c — fidelidade**: windowshade, regiões/máscaras, animações de sprite, double-size e
  escala. *2–3 sessões.*

**Riscos conhecidos**

- **Wayland**: a skin clássica pressupõe três janelas posicionáveis e "coladas". O compositor
  não deixa o cliente posicionar janela, então o encaixe do original se perde; a mitigação é
  desenhar as três numa janela só (menos fiel) ou aceitar janelas soltas.
- **Licença**: o formato não é protegido, cada skin tem a licença do autor. Carregar arquivos
  locais e nunca redistribuir nos deixa na mesma posição de VLC/Audacious/Qmmp, que já tocam
  `.wsz` no Linux.

**Pré-requisitos baratos (feitos antes de começar):** cores centralizadas num `Theme` (F2D),
desenho separado do layout (o `DisplayPanel` já se pinta sozinho) e nenhuma geometria sagrada
no motor de áudio.

**Pronto quando:** um `.wsz` clássico carrega e a janela principal fica visualmente
equivalente à do Winamp 2.x, com transporte, números, posição e volume respondendo.

## Avaliado e descartado

**Vídeo (F6, recusada).** O Winamp original abria MPG/AVI/WMV via DirectShow. Reproduzir
isso no Amplia custaria 3–5 sessões e traria os riscos que o app hoje não tem: saída de
imagem, sincronia áudio-vídeo, frames perdidos no redimensionamento, decode por hardware.
O decode em si é grátis (o FFmpeg já está linkado) — o trabalho é todo no output. Fica de
fora por decisão (D-043), e a aplicação continua leve. Quem tentar abrir um vídeo recebe
uma explicação com atalho para o player do sistema.

**Captura da saída do sistema** (visualizar outros apps): exigiria permissão de captura e
mostraria áudio alheio. O visualizador da F2C mede só o sinal do Amplia.

## Depois (avaliado, não planejado)

### Porte para Windows — "possível, não planejado"

Objetivo de longo prazo declarado pelo autor: um player multiplataforma, compartilhável com
amigos que usam Windows e querem um Winamp com cara de 2026.

O que o porte exige (nada disso está no escopo atual):

- **Saída de áudio**: implementar `IAudioOutput` sobre **WASAPI** (compartilhado, evento,
  float32). Gapless, EQ, analisador, ReplayGain e scrobble não mudam — trabalham em PCM.
- **Integrações**: MPRIS → SMTC; bandeja e atalhos já são Qt.
- **Instância única**: socket abstrato do `QLocalServer` é Linux-only → named pipe.
- **Permissões de arquivo**: `statvfs`/`::access`/`unistd.h` precisam de versão Windows.
- **Empacotamento**: zip portátil ou MSIX, `windeployqt`, FFmpeg/TagLib empacotados,
  assinatura para não cair no SmartScreen.

O que o porte **não** exige: reescrever interface, temas, playlist, biblioteca, equalizador,
scrobble ou analisador — todos são C++20 + Qt/FFmpeg/TagLib/SQLite.

## Idioma (a F6 antiga — dividida em três etapas)

### F6a — Infraestrutura invisível (concluída)

- `tr()` em toda string de interface; `QStringLiteral` só nos termos universais do display
  (D-073) e em identificadores.
- `i18n/amplia_pt_BR.ts` como fonte; `lupdate`/`lrelease` no CMake; `QTranslator` carregando
  por `QLocale::system()` com fallback no português.
- **Preferências ▸ Geral ▸ Idioma**: `Sistema`, `Português (Brasil)`, `English` (+ qualquer
  `.qm` avulso em `~/.local/share/amplia/i18n/`).
- Testes fixam o locale (senão as asserções em português quebram).
- Critério de pronto: o app abre em português e em inglês **sem nenhuma string traduzida
  ainda** além do que está no `.ts` de exemplo.

### F6a.1 — Reinício da interface (concluída)

- `MainWindow::setRestartHandler()` + `restartForLanguage()`; `main()` guarda a janela num
  `shared_ptr` e recria no mesmo processo; `QMessageBox` com "Reiniciar agora" / "Na próxima".
- Escape `AMPLIA_RESTART_TEST` para testar sem clique.

### F6b — Tradução de verdade (concluída)

- `amplia_en.ts` preenchido; espanhol opcional (é o próximo idioma com mais retorno).
- Revisão visual idioma a idioma: rótulos de largura fixa, menus, diálogos, barra de status.
- `CONTRIBUTING-TRANSLATIONS.md`: como traduzir, como testar com `.qm` avulso, o que não
  traduzir (display), como abrir PR.
- Critério de pronto: app inteiro em inglês sem sobra em português.

### F6c — Documentação pública (concluída)

Decidido em D-079: **um arquivo só**, português no topo, separador e inglês abaixo — com
`[English](#english)` no cabeçalho e `[↑ Português](#amplia)` na metade inglesa.

- [x] `COPYING` (GPL-3.0 verbatim) e `NOTICE` (dependências, licença comercial sob consulta,
      não afiliação, código de terceiros).
- [x] `ESTADO.md` movido para `docs/NOTAS-DE-SESSAO.md` e ignorado no git; README cita as notas
      como registro de desenvolvimento.
- [x] README em português: motivação e histórico, captura, tabela de fases corrigida, decisões
      com link para os ADRs, limites conhecidos, traduções, licença e créditos.
- [x] Metade em inglês (não é tradução literal: mesmas seções, frases mais curtas).
- [x] `NOTICE` revisado na hora de publicar binário (versões de dependência do runtime).
- [x] `## Instalar o pacote pronto` nas duas línguas, com o bundle da release e a remoção.
- [x] Galeria de capturas (biblioteca, humor, ajuda) e a imagem da fila junto do parágrafo dela.
- [x] Crédito a quem traduz: `help/idiomas.txt` alimenta o Sobre, e o crédito é do autor do PR.

## Dívida técnica — o `MainWindow` precisa de controladores

A auditoria externa apontou o tamanho do `src/ui/main_window.cpp`: **5.587 linhas e 163 métodos**
(era 4.338/115 antes da fila e do humor). Ele acumula quatro papéis: janela, player, fila e
biblioteca. Não é urgente, mas cada entrega nova fica mais cara.

- [ ] Extrair `PlaylistController` (carregar/salvar, ordens, enfileirar, arrastar)
- [ ] Extrair `QueueController` (fila, origens, sementes, painel)
- [ ] Extrair `LibraryController` (raízes, varredura, humores, prévia)
- [ ] `MainWindow` fica só com janela, menus, atalhos e o que liga os três

**Pronto quando:** nenhum controlador passar de ~1.200 linhas e o `main_window.cpp` ficar abaixo
de 2.000.

## F7 — Empacotamento e alcance (era F8; inclui Windows desde 26/11)

Publicar deixa de ser só Flatpak: **cada fase concluída sai em todos os formatos disponíveis**
(D-130), e esta fase cria os formatos. A ordem abaixo é por retorno, não por facilidade.

Base de compatibilidade: **glibc 2.35** (Ubuntu 22.04 / Debian 12), que cobre a maior parte
das máquinas em uso. O binário do Flatpak **não serve de referência** para o AppImage — ele
nasce contra o SDK 6.10. AppImage, `.deb` e `.rpm` são compilados dentro de um contêiner com
essa base, não no SDK.

- [ ] **F7a — AppImage**: `Dockerfile` fixo (Ubuntu 22.04) com as dependências de build;
      `cmake --install` num `AppDir/usr`; `linuxdeploy` + `linuxdeploy-plugin-qt` fechando o
      arquivo. Empacotados: Qt 6 (xcb **e** wayland, imageformats, estilos), FFmpeg, TagLib,
      SQLite, `libsystemd` e **`libpipewire`/`libspa`** (o cliente tem de casar com o daemon
      do usuário). `.desktop` e `.svg` saem de `packaging/`; falta o AppStream metainfo.
      `scripts/build-appimage.sh` roda tudo isso num comando.
- [ ] **F7b — `.deb`**: a partir do mesmo `AppDir` (`CPack` ou `nfpm`), mas **sem** embarcar
      Qt/FFmpeg: declara as dependências e usa as do sistema, que é o que um `.deb` espera.
- [ ] **F7c — `.rpm`**: idem, para Fedora/openSUSE (o `nfpm` cobre os dois formatos).
- [ ] **F7d — portátil `.tar.gz`**: o `AppDir` sem o envelope do AppImage, para quem não tem
      FUSE ou prefere rodar de uma pasta.
- [ ] **F7e — AUR (`PKGBUILD`)**: arquivo no repositório, compilando do fonte; a comunidade
      mantém. Sem binário nosso.
- [ ] **F7f — Flathub**: publicar a entrada da loja (metainfo, capturas, revisão do `NOTICE`),
      não só o Flatpak instalado localmente.
- [ ] **F7g — importar dados do Flatpak**: Preferências ▸ "Importar dados do Flatpak" copia
      INI, `library.db` e presets de `~/.var/app/io.github.cassiofiorin.Amplia/` para
      `~/.config/cassiofiorin/` e `~/.local/share/cassiofiorin/Amplia/`. Sem isso a biblioteca
      indexada "some" na primeira abertura fora do Flatpak.
- [ ] **F7h — uma release só**: a tag `vX.Y.Z` gera Flatpak + AppImage + `.deb` + `.rpm` +
      `.tar.gz` no mesmo workflow, com checksums e notas de versão.

- [ ] **F7i — Windows**: o port completo. Saída de áudio **WASAPI** por trás do
      `IAudioOutput` (`src/core/include/wamp/audio_output.h`; hoje só existe
      `makePipeWireOutput()`), build MSVC + CMake + vcpkg (Qt 6, FFmpeg, TagLib, SQLite), sem
      sd-bus (o MPRIS vira **SMTC**, o controle de mídia do sistema), bandeja pelo
      `QSystemTrayIcon`, instalador **Inno Setup** e assinatura de código (decisão de custo a
      parte: o certificado EV é o que evita o aviso do SmartScreen), DPI e fontes conferidos.
      Estimativa honesta: **4 a 6 semanas** em tempo parcial; o caminho sem SMTC e sem
      assinatura sai em ~2 semanas, mas entrega um aviso do Windows na primeira execução.

**Fora por enquanto:** Snap (loja própria e sandbox a manter) e Nix — entram se houver pedido. macOS
fica fora do roadmap: exigiria CoreAudio e outro ciclo de vida de janela, sem público estimado.

**Pronto quando:** uma tag publica todos os formatos de uma vez; o AppImage abre numa máquina
com glibc 2.35 sem instalar nada; o `.deb`/`.rpm` abrem com a biblioteca que veio do Flatpak já
importada; e o `.exe` toca a mesma biblioteca no Windows, com o mesmo banco e os mesmos presets.
