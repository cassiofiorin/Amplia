# Compartilhamento — análises e perfis

Planejado para as **F5** (dados de análise) e **F6** (perfis). Nada disto está implementado:
este documento fixa o desenho antes do código, como as decisões de arquitetura fazem em
[`DECISIONS.md`](DECISIONS.md).

A ideia em uma frase: **quem já ouviu uma biblioteca pode poupar o trabalho de quem está
começando** — o BPM e o tom que o Amplia mediu ao tocar podem ser herdados, e o gosto que a
pessoa afinou (humor, mistura, léxico) pode virar um perfil que se empresta a um amigo.

## 1. A chave é a obra, não o arquivo

O caminho (`/home/…/Music/a-ha/2005 - Analogue/01 Celice.mp3`) não sobrevive a mudar de
máquina, e o **hash acústico** responde a outra pergunta: ele diz "é a mesma gravação", e um
rip e um remaster da mesma música dão hashes diferentes. Para compartilhar análise o que
interessa é a **obra**: o mesmo artista, álbum e título, com a mesma duração aproximada.

Consequências, em ordem de importância:

- O dataset tem como chave a **tag normalizada** mais a **duração arredondada**. É o que
  sobrevive a rip, remaster, bitrate e mudança de pasta.
- O **fingerprint** (F4, `libchromaprint` + AcoustID) entra só como **desempate**: quando duas
  gravações empatam em obra e duração, ou quando a tag do arquivo está errada e a importação
  precisa saber em qual linha confiar. Sem fingerprint, tudo funciona pelo caminho da obra.
- A normalização é a peça que decide tudo, então ela é **versionada e especificada** — `norm/v1`
  abaixo. Cada regra é uma promessa de que duas linhas são a mesma obra; por isso a lista é
  curta e conservadora.

## 2. `norm/v1` — a identidade da obra

Função pura, sem rede e sem banco: recebe `artist`, `album`, `title` e `durationMs`, devolve a
chave e a duração em segundos. A chave é `sha1(norm(artist) ‖ 0x1f ‖ norm(album) ‖ 0x1f ‖
norm(title))`; a duração fica fora da chave e é usada no casamento.

Regras aplicadas aos três campos:

1. Unicode NFD, remover marcas combinantes (acentos), caixa baixa.
2. Colapsar espaços e aparar as pontas; trocar `\t`, `\n` e `\r` por espaço.
3. Remover número de faixa inicial (`01 `, `1. `, `01-`) do **título**.
4. Remover marcadores de disco (`cd1`, `cd 2`, `disc 1`, `disco 2`) do **álbum**.
5. Remover sufixos entre parênteses, colchetes ou após ` - ` de uma **lista fechada**:
   `remaster`, `remastered`, `remasterizado`, `deluxe`, `deluxe edition`, `bonus`,
   `bonus track`, `mono`, `stereo`, `explicit`, `clean`, `clean version`.
6. `feat.`, `ft.`, `featuring` e o convidado que vem depois saem do **título**; o convidado
   **não** entra no artista da chave.
7. Pontuação final (`.,;:!`) sai; apóstrofos e `&` **ficam** como estão.

O que **não** se remove, de propósito, porque muda a gravação:

`live`, `ao vivo`, `acústico`, `acoustic`, `instrumental`, `demo`, `remix`, `edit`,
`radio edit`, `extended`, `reprise`, `session`.

Duração: segundos cheios. Casamento exige `|Δ| ≤ 3 s` e, na falta de duração em um dos lados,
aceita-se o casamento **só** quando a obra tem uma única linha no dataset.

Mudou uma regra? A versão sobe para `norm/v2` e o arquivo traz a versão no cabeçalho; o
importador só aceita `norm/v1` e avisa nas outras (nunca mistura).

## 3. Formato do arquivo de análises

TSV, uma linha por **valor agregado**, precedido de um cabeçalho obrigatório:

```
# amplia analises norm/v1
# key	artist	album	title	dur_s	bpm	key_pc	key_major	confidence	measurements
6f1c…	 a-ha	analogue	celice	221	115	9	0	0.31	3
6f1c…	 a-ha	analogue	celice	221	112	9	0	0.22	1
```

- `key` — a chave de `norm/v1`, hexadecimal.
- `artist`, `album`, `title` — **como o contribuidor tem**, para leitura humana; a importação
  reconstrói a chave a partir deles e confere com a coluna `key` (divergência = linha ignorada).
- `dur_s` — segundos.
- `bpm`, `key_pc`, `key_major` (`0` menor, `1` maior), `confidence`, `measurements` — a medição
  e quantas vezes ela foi obtida com esse mesmo valor.
- Várias linhas por obra são esperadas e desejadas: é o consenso.

O que o arquivo **não** leva: caminho, tamanho de arquivo, mtime, data de medição, playcount,
rating, skip, apelido de usuário. Nada que descreva hábito.

## 4. Importação — as regras de confiança

1. **A medição local nunca é sobrescrita.** Se a faixa tem `analyzed = 1` no banco, a herança é
   descartada em silêncio.
2. **Sem medição local**, a linha herdada vira um *prior*: grava `bpm`, `key_pc`, `key_major`,
   `bpm_confidence × 0,7` e marca `analyzed = 2` (herdada, não medida aqui).
3. **Quando o arquivo é tocado**, a análise local mede e substitui o prior (`analyzed = 1`).
   A herança é ponto de partida, não resultado.
4. **Consenso**: entre as linhas da obra, vence a que tiver maior soma de `measurements`
   (empate: maior `confidence` média; novo empate: a de menor `bpm`).
5. **Sanidade**: linha com `bpm` fora de 20–400, `key_pc` fora de 0–11, `confidence` fora de
   0–1, `dur_s` fora de 1–3600 ou `measurements` < 1 é ignorada e contada no relatório final.
6. A importação inteira roda em **uma transação**, termina com `flush()` e mostra um resumo
   ("312 herdadas, 41 já medidas aqui, 3 linhas inválidas").

## 5. Exportação

- Escopo: **biblioteca inteira** ou **um artista** (o que o pedido original chamava de banda).
- Só exporta `analyzed = 1` — herança emprestada não se re-exporta, para o dataset não virar
  telefone sem fio.
- Nome sugerido: `amplia-analises-<AAAAMMDD>-<escopo>.tsv` (data no **nome do arquivo**, não
  na linha).

## 6. Perfis (F6)

Um perfil é o que o app já sabe representar: o `HumorPreset` — agitação, tom, estabilidade,
mistura e o léxico de gênero — mais um nome. JSON, um objeto:

```json
{
  "schema": "amplia-perfil/v1",
  "name": "metalhead de academia",
  "author": "cassiofiorin",
  "humor": { "agitation": 88, "tone": 10, "stability": 45 },
  "blend": 0.35,
  "genres": "metal, hardcore, industrial"
}
```

- Exportar: "Salvar como…" já cria o preset no banco; ganha um "Exportar perfil…".
- Importar: entra como preset salvo com o nome do autor ao lado ("metalhead de academia —
  cassiofiorin"), nunca como preset de fábrica.
- Escopo duro: **sem histórico**. Nada do que você pulou ou ouviu sai do banco para o perfil.

## 7. O repositório

- **Repo irmão**, `amplia-dados`, não uma pasta dentro do `Amplia`: o código é GPL-3.0-or-later
  e os dados querem licença própria (CC0-1.0), e PR de dados não deve puxar o CI do app.
- Estrutura: `analises/<A-Z>/<artista-slug>.tsv` (um arquivo por artista evita PR conflitante),
  `perfis/<slug>.json`, `CONTRIBUTING.md` (como exportar e abrir PR), `scripts/valida.py`.
- CI do repo: valida esquema, faixas de valor, chave recomputada, versão `norm/v1`, duplicata
  exata e — quando houver fingerprint na linha — gravação repetida.
- `README` do repo diz o que está sendo compartilhado e o que **não** está, com uma frase sobre
  fingerprint não ser áudio e não permitir reconstruir a música.

## 8. Perguntas ainda abertas

1. Importação **manual** (escolher arquivo) no v1, com download direto do repositório depois?
   A manual combina com "nada de rede obrigatória" e é o que valida o formato.
2. Nome do repositório: `amplia-dados`, `amplia-dataset`, outro?
3. O perfil vai **antes** ou **depois** das análises? (A ordem atual do roadmap diz depois: as
   análises são o alicerce, o perfil é o empréstimo de gosto.)
