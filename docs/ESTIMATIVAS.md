# Estimativas e tempo real

Registro pedido pelo autor em 27/11: o que foi prometido em tempo, quando, e o que aconteceu de
fato. A fonte do tempo real é o histórico de commits do repositório (data do commit, não do
relato) — quem quiser conferir roda `git log --date=format:'%d/%m %H:%M' --pretty='%ad %h %s'`.

## O que foi dito em números

| Prometido | Quando | Prazo dito | Real |
|---|---|---|---|
| Porte para Windows — paridade completa | 26/11 15:47 | 4–6 semanas em tempo parcial | não iniciado (F8) |
| Porte para Windows — caminho sem SMTC nem assinatura | 26/11 15:47 | ~2 semanas | não iniciado (F8) |
| Spike do porte (provar WASAPI + build MSVC) | 26/11 15:47 | 3–5 dias | não iniciado |
| Conversor de formatos com saída MP3 | 27/11 17:47 | 1,5–2 semanas; caminho curto de 3–4 dias | não iniciado (F4B) |

Fora desses quatro, **não houve prazo em data**: as fases foram descritas por escopo e ordem, sem
calendário. Isso é uma escolha (escopo muda mais que código), mas tem custo: sem data, o autor não
consegue planejar o que vem depois, e eu não sou cobrado por desvio.

## O que o relógio diz (trabalho concluído)

Janelas medidas de commit a commit, incluindo compilação e teste — não é só tempo de digitação.

| Entrega | Janela | Tempo |
|---|---|---|
| Fila persistida, com retomada em pausa (D-118) | 24/11 14:23 → 14:38 | **15 min** |
| Semente da Rádio local como mistura (D-119) | 14:38 → 14:50 | **12 min** |
| Alt+3 com BPM e tom da tag (D-120) | 14:50 → 15:05 | **15 min** |
| Clima como ponto num plano, com presets (D-121) | 15:05 → 15:34 | **29 min** |
| Nó do Humor, sliders e rascunho (D-122) | 15:34 → 15:49 | **15 min** |
| Ajuste de espaçamento da Biblioteca + socket órfão | 15:49 → 16:39 | **50 min** (5 builds, 3 idas e voltas de gosto) |
| Ajuda bilíngue + Sobre com créditos (D-125) | 16:39 → 17:16 | **37 min** |
| Slogan, idiomas, versão 0.9.0-rc2 (D-126) | 17:16 → 17:34 | **18 min** |
| 0.9.1: menu de contexto e arrasto na Biblioteca (D-127) | 17:34 → 17:36 | **2 min** de commit; a fatia inteira, ~20 min |
| Ver tag, atualizar tag e arrasto para a fila | 17:36 → 17:53 | **17 min** |
| 0.9.3-rc3: reordenar no ponto do drop, fila vazia (D-128) | 17:53 → 18:44 | **51 min** (a duplicação do arrasto custou 4 iterações) |
| F3 fechada: varredura incremental e filtros (D-129) | 18:44 → 19:32 | **48 min** |
| Inglês completo: 178 textos + árvore + humores (D-131) | 19:32 → 20:10 | **38 min** |
| Auditoria: rádio relativo × paralelo e I/O fora do mutex (D-132) | 25/11 15:43 → 15:51 | **8 min** |
| 0.9.7-rc7: esquema versionado e os dois ajustes visuais (D-133/134) | 15:51 → 16:28 | **37 min** |
| Pacote pronto para o Flathub: metainfo, desktop, licença (D-135) | 16:28 → 16:58 | **30 min** |
| Backlog dos 22 itens, só anotação (24 commits) | 25/11 17:44 → 18:39 | **55 min** |
| Os 22 itens implementados, com build e testes (D-136) | 26/11 13:39 → 15:25 | **1 h 46 min** |
| 1.0.0: versão estável, notas e release | 15:25 → 15:35 | **10 min** |
| Reordenação das fases + documento de compartilhamento | 15:35 → 16:07 | **32 min** |
| CI vermelho: lrelease, catálogo e 20 traduções | 16:12 → 16:30 | **18 min** |
| Capa de álbum: cache, miniaturas, painel e sorteio (D-138) | 17:23 → 18:15 | **52 min** |
| Autotag por texto: módulo, cliente e Alt+3 (D-139) | 19:24 → 20:07 | **43 min** |
| Diário do autotag com desfazer (D-140) | 20:07 → 20:13 | **6 min** |
| Busca sob demanda, 503 como espera, traduções (D-141) | 27/11 12:13 → 12:45 | **32 min** |
| Lote de 250 com fila persistida (D-142) | 12:45 → 13:24 | **39 min** |
| Revisão do lote, consenso e desfazer (D-143..146) | 13:38 → 15:26 | **1 h 48 min** |
| Escrita por remuxagem: m4a e limpeza de campo (D-147) | 15:26 → 17:39 | **2 h 13 min** (inclui 3h de tentativa pelo TagLib antes de achar a causa) |

## O que os números ensinam

1. **Trabalho especificado fecha rápido.** Quase todo item de escopo fechado levou entre 10 e
   50 minutos, com build e teste. A mediana está perto de **20 minutos**, não de "um dia".
2. **O custo não está no código, está na decisão.** As três janelas mais longas (espectro e capa,
   50 min + 52 min; revisão, 1 h 48; remuxagem, 2 h 13) foram longas por motivos diferentes:
   gosto visual (três idas e voltas de lado do espectro), desenho de interação (o que marcar, o
   que gravar) e diagnóstico de causa raiz (TagLib que mente no `save()`).
3. **Minhas estimativas de semanas são as menos testadas.** As duas únicas que dei — Windows
   (4–6 semanas) e conversor (1,5–2 semanas) — nunca rodaram. Pelos números acima, ambas parecem
   **conservadoras**: a F4 inteira, do `norm/v1` ao remux, custou **5 h 26 min** de relógio, e
   isso incluindo três defeitos difíceis. Um conversor de MP3 com fila tem menos incógnita que a
   F4 tinha (o caminho de áudio já existe), então "1,5–2 semanas" provavelmente é teto, não piso.
4. **O que envelhece a estimativa é a dependência externa.** O único item em que errei por baixo
   foi o roteiro de testes: eu esperava validar por clique e passei a maior parte do tempo com o
   ponteiro do X travado, testando por gancho. Nada disso apareceu na conta de ninguém.

## Regra que fica

Para as próximas fases: **estimar em horas de relógio, com faixa**, e não em "dias" nem em
"semanas". Uma fase como a F4B (conversão) deve entrar com "8–16 horas de relógio" em vez de
1,5–2 semanas — mais fácil de conferir e mais honesto com a velocidade real.
