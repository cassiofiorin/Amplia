# Ajuda do Amplia

O Amplia é uma reimplementação do Winamp para o desktop Linux: uma janela só, sem
navegador, com a mesma ideia de sempre — a música toca enquanto você mexe na lista.

## A janela principal

- **Display**: tempo decorrido (ou restante, com `Ctrl+T`), nome da faixa, artista, e à
  direita os indicadores de bitrate, frequência e canais. `kHz` e `STEREO` acendem quando
  o arquivo tem o que mostrar.
- **Botões**: anterior, tocar, pausa, parar, próximo, ejetar e o volume. `L R` é o medidor
  de nível; `EQ` e `123` acendem o equalizador e o contador de faixas.
- **Título rolante**: a barra de título mostra a faixa e o artista, rolando quando não cabe.
- **Rodapé**: à esquerda o contador da fila de leitura de metadados e a versão; à direita
  as mensagens passageiras.

## A playlist

A lista tem uma linha por faixa, com duração; a faixa tocando aparece marcada e, se o
painel da fila estiver aberto, a fila vem logo abaixo.

- **Ordenar**: título, artista, álbum, caminho, duração ou data de modificação. `Embaralhar`
  embaralha a ordem; `Repetir` faz a lista girar.
- **Enfileirar** (ou `Alt+clique` numa linha) põe a faixa na frente da ordem, sem perder o
  resto. A fila tem origens: playlist, biblioteca ou rádio.
- **Remover**: `Delete` remove a seleção, `Ctrl+Delete` mantém só ela, `Alt+Delete` tira as
  faixas que sumiram do disco.
- **Manipulação**: arraste linhas para reordenar, `Ctrl+setas` movem a seleção de lugar.
- **Fonte**: `Ctrl+=` e `Ctrl+-` aumentam e diminuem o texto; `Ctrl+Shift+0` volta ao padrão.

## A fila (N)

O botão `Fila (N) ⌄` abre e fecha o painel. Cada linha diz de onde a faixa veio
(`PLAYLIST`, `BIBLIOTECA`, `RÁDIO`) e quanto dura. A fila **sobrevive ao fechamento**: o
app guarda a lista e as origens, e na abertura seguinte ela volta — em pausa, na mesma
faixa, para você decidir quando continuar.

Recusar uma faixa da rádio (tirar da fila) é uma informação: ela alimenta o histórico que
a estação usa para não repetir o que você não quis ouvir.

## A Biblioteca

`Opções ▸ Biblioteca` abre uma janela com a coleção inteira, separada do que está tocando.

- **Visões**: todas as faixas, adicionadas nos últimos 3 dias, modificadas nos últimos 3
  dias, tocadas nas últimas 2 semanas, nunca tocadas, mais tocadas e melhor avaliadas.
- **Busca**: filtra por título, artista ou álbum enquanto você digita.
- **Colunas**: clique no cabeçalho para ordenar; `Nota` aceita clique para dar de 0 a 5
  estrelas, e clique na mesma nota limpa.
- **Rodapé**: Tocar, Enfileirar e (no modo rádio) Tocar estação. `Alt+clique` numa linha
  enfileira em vez de tocar; duplo clique toca.

### Humor

O nó **Humor** é um plano de dois eixos: **Agitação** (lento ↔ rápido, que consome o BPM) e
**Tom** (frio, menor ↔ quente, maior). A **Estabilidade** decide a largura do alvo — de
"esta faixa e as vizinhas" a "a estação inteira pode variar três degraus de BPM". O quarto
slider é a **Mistura**: 0% é só o humor, 100% é só o que você vem ouvindo.

- A lista de humores (de fábrica e os seus) fica no **combo** ao lado do título. Escolher
  um humor carrega os quatro sliders.
- Mexer nos sliders é **rascunho**: a prévia abaixo muda na hora, mas a fila só nasce no
  **Tocar**. O aviso laranja diz isso, e o **Reverter** devolve os sliders ao humor
  escolhido.
- **Salvar como…** guarda o ponto com um nome seu; **Remover** apaga os seus. Os de fábrica
  não mudam: salve uma variação.
- Enquanto a biblioteca não foi medida, quem decide a prévia é o **léxico de gênero** do
  humor. Conforme você ouve, BPM e tom medidos passam a pesar — e a estação melhora. O
  rodapé mostra quantas faixas já foram medidas.

### Rádio local

A **Rádio local** é a irradiação da faixa que toca: a estação parte dela e do que você
vem ouvindo (as últimas cinco), com um slider que decide quanto de cada um.

- O primeiro item da fila é a semente; o resto é a estação.
- Regras de escolha: artista, álbum, gênero, época, tempo e tom; a mesma faixa nunca é
  sugerida duas vezes, artistas repetidos são espaçados nas primeiras escolhas e, de vez
  em quando, entra uma descoberta de quem você ouve pouco.
- O painel mostra a prévia: 20 faixas que a estação usaria agora.

## Alt+3 — informações do arquivo

Mostra e edita as tags (título, artista, álbum, número, ano, gênero, comentário,
compositor, gravadora) e o BPM (`TBPM`). À direita, duas leituras:

- **Análise local**: `BPM da tag` e `Tom da tag` vêm do próprio arquivo (`TBPM` e `TKEY`);
  `BPM percebido` e `Tom percebido` são medidos ao tocar e **não** são gravados no arquivo.
- Gravar é seguro por construção: o app escreve numa cópia temporária e troca de uma vez, e
  avisa que interromper o processo no meio pode corromper as tags.

## Equalizador, aparência e preferências

- `Alt+G` abre o equalizador com curvas prontas e a curva salva por você.
- `Ctrl+,` abre a Aparência: temas prontos, o editor de cores e a fonte da playlist.
- `Ctrl+P` abre as Preferências: dispositivo de saída, ReplayGain, Last.fm, pastas da
  biblioteca, idioma.

## Atalhos

| Tecla | O que faz |
| --- | --- |
| `X` / `Espaço` / `C` | Tocar / pausar / pausar |
| `B` / `Z` | Próxima / anterior |
| `V` | Parar |
| `S` / `R` | Sorteio / repetir |
| `→` / `←` | Avança e volta 5 segundos |
| `↑` / `↓` | Volume |
| `M` | Silencia |
| `Num 1` / `Num 3`, `Ctrl+←` / `Ctrl+→` | Pula 10 faixas |
| `L` / `Shift+L` | Adicionar arquivos / adicionar pasta |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | Nova playlist / abrir / salvar |
| `Delete` / `Ctrl+Delete` / `Ctrl+Shift+Delete` | Remove / mantém só a seleção / limpa |
| `Alt+Delete` | Remove as faixas que sumiram |
| `,` (vírgula) | Ir para a faixa… |
| `Alt+3` | Informações do arquivo |
| `Ctrl+Shift+Q` | Abre e fecha a fila |
| `Ctrl+Shift+R` | Embaralha a lista (playlist) |
| `Ctrl+Alt+A` / `Ctrl+Alt+D` / `Ctrl+I` | Selecionar tudo / nada / inverter |
| `Ctrl+T` | Alterna entre decorrido e restante |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+Shift+0` | Fonte da playlist: maior / menor / padrão |
| `Alt+W` / `Alt+E` / `Alt+G` | Janela principal / playlist / equalizador |
| `Ctrl+Tab` | Alterna entre as janelas abertas |
| `Ctrl+P` / `Ctrl+,` | Preferências / aparência |
| `F1` | Esta ajuda |

## Onde o Amplia guarda as coisas

Tudo vive dentro do Flatpak, em `~/.var/app/io.github.cassiofiorin.Amplia/`:

- `config/amplia/amplia.ini` — preferências, janela, playlist e fila;
- `data/cassiofiorin/Amplia/library.db` — biblioteca, notas, histórico da rádio e humores
  salvos;
- `data/cassiofiorin/Amplia/` — caches (capas, análise).

## Enviar opinião

`Ajuda ▸ Enviar opinião` abre as issues do projeto:
<https://github.com/cassiofiorin/Amplia/issues>. Um relato bom diz o que você fez, o que
esperava e o que aconteceu — e, se for som, o formato do arquivo ajuda.

## Traduzir o Amplia

A ajuda de cada idioma é um arquivo de texto nesta pasta (`ajuda-pt.md`, `ajuda-en.md`), e a
lista de tradutores fica em `tradutores.txt` — ela aparece em `Sobre o Amplia…`. Quem traduz
mexe só no seu arquivo e numa linha de texto: nada de código.
