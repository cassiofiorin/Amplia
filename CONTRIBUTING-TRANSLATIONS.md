# Traduções

O Amplia fala português por dentro. Toda string de interface nasce em `tr()` **em português**,
e o inglês é uma tradução como qualquer outra. Isso mantém um idioma-fonte único e evita que
uma tradução "invente" um texto que ninguém revisou.

## Onde ficam

| Arquivo | Papel |
|---|---|
| `i18n/amplia_en.ts` | tradução para inglês (XML do Qt Linguist) |
| `src/ui/translator.cpp` | carrega o `.qm` e resolve `Sistema`/`pt_BR`/`en` |
| `AppDataLocation/i18n/*.qm` | tradução avulsa, carregada **antes** da embutida |

## Traduzir sem recompilar

1. Gere (ou atualize) o `.ts` do seu idioma a partir do inglês ou do português:
   ```
   lupdate -locations none src -ts i18n/amplia_xx.ts
   ```
2. Traduza no **Qt Linguist** (`linguist i18n/amplia_xx.ts`) — ele mostra o contexto, o texto
   original e as marcações `%1`, `%2`.
3. Compile:
   ```
   lrelease i18n/amplia_xx.ts -qm amplia_xx.qm
   ```
4. Copie o `.qm` para `~/.var/app/io.github.cassiofiorin.Amplia/data/cassiofiorin/Amplia/i18n/`
   (no Flatpak) e abra o app com o idioma escolhido em **Opções ▸ Preferências ▸ Geral**.

O app carrega primeiro o `.qm` avulso e só depois o embutido — dá para testar uma tradução
sem tocar no build.

## Regras

- **Não traduza**: `KBPS`, `KHZ`, `STEREO`/`MONO`, `EQ`, `L`/`R`, `dB`. Eles são do display e
  falam a língua do hardware (D-073).
- **Preserve** `%1`, `%2`, `%n`, `&` (atalho de menu), `▸`, `▾`, `•`, `“ ”`.
- **Plurais**: no código-fonte as contagens são neutras (`arquivo(s)`); em idiomas com
  plural real use `%n` no `tr()` quando a string for criada (é trabalho de código, não só de
  tradução).
- **Contextos** vêm do nome da classe (`MainWindow`, `PreferencesDialog`, …). Se você move
  código entre classes, o contexto muda e a tradução precisa ser revisada no `.ts`.
- **Larguras**: o display mede o texto com `QFontMetrics`, mas alguns rótulos fixos (sliders
  do EQ, colunas da playlist) não. Prefira termos curtos quando o original for curto.

## Testando

```
flatpak run --env=AMPLIA_LANG=xx io.github.cassiofiorin.Amplia
```

`AMPLIA_LANG` força o idioma sem mexer na INI. O caminho completo (trocar pelo menu e recriar
a interface) também é exercitado por `AMPLIA_RESTART_TEST=xx`.

## Enviando

1. Rode `lupdate` **só** no seu `.ts` para não reescrever os outros.
2. Confira que não sobrou `<translation type="unfinished">`.
3. Abra o PR com o `.ts` (e o `.qm` só se quiser demonstrar). O build roda `lrelease`.

`i18n/amplia_en.ts` é mantido à mão de propósito: `lupdate` roda quando alguém mexe nas
strings, não a cada build — assim o arquivo não vira fonte de conflito em todo commit.
