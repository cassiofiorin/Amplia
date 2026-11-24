## 0.9.6-rc6 — funcionalidades básicas e dois idiomas

Primeira marca do projeto em que o Amplia faz **o que um player precisa fazer** e faz isso
**em português e em inglês, sem sobra**.

### Funcionalidades básicas cobertas

- **Reprodução** — MP3, FLAC, OGG/Vorbis, AAC/M4A, WAV e Opus pelo FFmpeg, com gapless, seek,
  volume, mudo e ReplayGain (faixa e álbum).
- **Equalizador** de 10 bandas com presets `.eqf`, curva própria e o visualizador reagindo ao
  sinal processado.
- **Playlist** — arrastar para reordenar, ordenar e embaralhar, `.m3u`, `.m3u8`, `.pls`, `.b4s`
  e `.wpl`, sessão restaurada ao abrir.
- **Fila** — enfileirar de qualquer origem (playlist, biblioteca, rádio), reordenar
  arrastando, painel que abre mesmo vazia e retomada em pausa depois de fechar.
- **Biblioteca** — árvore por período/uso, busca, filtros combinados de artista/álbum/gênero,
  varredura incremental por mtime, notas, contagem de execuções e remoção com histórico.
- **Rádio local e Humor** — estação que parte da faixa ou de um ponto no plano
  agitação/tom, com mistura entre o clima e o histórico, prévia ao lado dos sliders e presets
  salvos com nome próprio.
- **Integrações** — MPRIS2, Last.fm, ListenBrainz, bandeja do sistema, instância única,
  abrir por linha de comando ou "Abrir com".
- **Interface** — display de sete segmentos, medidor L/R, espectro, temas de cor, atalhos na
  lógica do Winamp, ajuda (F1) e "Sobre" bilíngues.

### Dois idiomas

- **522 textos** no catálogo inglês, **0 pendentes** (`lrelease` confirma).
- Toda a interface traduzida, inclusive a janela da Biblioteca (que nunca estivera no
  catálogo) e os nomes dos humores de fábrica.
- Português é o idioma-fonte; a troca vale na abertura seguinte, sem reinício forçado.

### Pronto quando

- Interface inteira em PT-BR e EN, sem texto em português numa sessão em inglês.
- Versão publicada com todos os formatos de empacotamento então disponíveis (hoje Flatpak;
  AppImage, `.deb`, `.rpm` e portátil entram na F8).

### Fora desta marca (próximas fases)

Capa de álbum (F3.1), plugins `.so` (F4), skins `.wsz` (F7), empacotamento em todos os
formatos (F8) e a busca de tags (em pausa).
