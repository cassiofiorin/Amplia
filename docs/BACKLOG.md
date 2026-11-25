# Backlog — usabilidade anotada no uso

Só correções de usabilidade: coisas que atrapalham ou faltam no uso do dia a dia. O que é
fase nova, empacotamento ou dívida técnica fica no `ROADMAP.md`; release fica nas notas de
versão. Uma linha por item, com a data e onde acontece — a ordem é a de anotação, não a de
prioridade.

| # | Data | Item | Onde | Estado |
|---|---|---|---|---|
| 1 | 25/11 | **Coluna "Ano"**, como última coluna da tabela (depois de "Tocadas"). O dado já existe em `LibraryEntry.info.year`; a tabela mostra 8 colunas e o ano não está entre elas. Ordenar como número, igual a Duração e a Nota | Biblioteca, tabela de faixas | a fazer |
| 2 | 25/11 | **Ordem padrão da tabela: Ano, Álbum, Faixa** (nessa prioridade). Na hora, decidir se vale para todas as visões ou só para "Todas as faixas" — "Adicionadas", "Modificadas", "Tocadas" e "Mais tocadas" hoje ordenam por data ou contagem, e perderiam o sentido | Biblioteca, tabela de faixas | a fazer |
| 3 | 25/11 | **Nome que só muda de caixa aparece duas vezes nas listas de escolha** — exemplo real na biblioteca: `Dogstar` e `DOGSTAR`, 79 faixas no total. As caixas de Artista e Álbum vêm de `distinctValues()`, que é `SELECT DISTINCT` e compara byte a byte, então a mesma gravadora vira duas entradas. O filtro em si já compara sem caixa (`= ? COLLATE NOCASE`), logo escolher qualquer uma das duas funciona. Proposta: agrupar por `COLLATE NOCASE` e mostrar uma entrada por grupo, preferindo a grafia mais frequente (empate: a primeira em ordem alfabética). Vale para Artista, Álbum e Gênero. Também há álbuns assim: "Uma Outra Estação" × "Uma outra Estação", "Working On A Dream" × "Working on a Dream" | Biblioteca, caixas de filtro | a fazer |
