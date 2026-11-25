# Backlog — anotado no uso

Incômodos e pedidos anotados durante a escuta e o uso do app, para a próxima iteração.
Uma linha por item, com a data e de onde veio. A ordem é a de anotação, não a de prioridade:
a prioridade se decide quando a iteração começa.

| # | Data | Item | Onde | Estado |
|---|---|---|---|---|
| 1 | 25/11 | **Coluna "Ano" na Biblioteca**, como última coluna da tabela (depois de "Tocadas"). A `LibraryEntry` já traz `info.year`; a tabela mostra 8 colunas e o ano não está entre elas. Ordenar como número, igual a Duração/Nota | janela da Biblioteca, tabela de faixas | a fazer |
| 1b | 25/11 | **Ordem padrão da tabela: Ano, Álbum, Faixa** (nessa prioridade). Decidir na hora se isso vale para todas as visões ou só para "Todas as faixas" — as visões "Adicionadas", "Modificadas", "Tocadas" e "Mais tocadas" hoje ordenam pela data/contagem | janela da Biblioteca | a fazer |
| 2 | 25/11 | **Corte da `1.0.0`**: versão estável (sem `rc`), notas de release, README e a promessa de esquema/playlist | release | aguardando o ok |
| 3 | 25/11 | **Empacotamento (F8)**: repositório Flatpak próprio assinado (build-export + build-update-repo, hospedado estático, remote via `.flatpakrepo`) e, depois, AppImage/`.deb`/`.rpm`/tar.gz | `docs/ROADMAP.md`, F8 | a fazer |
| 4 | 25/11 | **Tradução inglesa**: os 522 textos foram gerados por IA. Se um dia houver submissão a loja que exija tradução humana, precisa de passada humana; fora disso, fica como está | `i18n/amplia_en.ts` | anotado |
| 5 | 20/11 | **Capa de álbum (F3.1)**: tags + `folder.jpg`/`cover.jpg`, cache por álbum, aparecer no player, no Alt+3 e na lista | `docs/ROADMAP.md`, F3.1 | planejado |
| 6 | 20/11 | **Dívida técnica**: extrair controladores do `MainWindow` (playlist, fila, biblioteca) | `docs/ROADMAP.md` | planejado |
| 7 | 20/11 | **Busca de tags** (F6) — seção em pausa no ROADMAP | `docs/ROADMAP.md` | em pausa |

## Decidido, fora do backlog

- **Flathub não será caminho** (25/11): a política de IA generativa proíbe manifesto com
  conteúdo de IA, e o autor depende de ferramenta assistiva para escrever em inglês. A
  distribuição segue por repositório Flatpak próprio e pelos formatos da F8.
