# Notas de sessão

Registro de como o Amplia foi construído: o que foi tentado, o que falhou, o que mudou de
ideia e por quê. Não é documentação de uso nem de arquitetura — é o diário de obra, guardado
porque explica decisões que o código sozinho não explica.

Para o estado atual do projeto, veja [`ROADMAP.md`](ROADMAP.md) e o README.

---

# Estado para retomar (ferramentas caíram com "Cannot find the native Koffi module")

Data: 2026-11-19 ~17:52

## Concluído e validado pelo usuário
- Play/pause do fone Bluetooth: o BlueZ entregava **dois** `PlayPause` por toque; o app alternava duas vezes e voltava ao mesmo estado.
  Correção: debounce de 400 ms em `playerPlayPause` (`wamp/src/platform/src/mpris_server.cpp`). Usuário testou: "ficou ótimo".
- Instrumentação temporária de MPRIS ainda no código: `logCall()` em `mpris_server.cpp`, ativa só com `AMPLIA_MPRIS_LOG=1`. Remover (ou manter e documentar) na retomada.
- Build + `wamp-ui-smoke`: "tudo certo".

## Em andamento: aviso de pasta/arquivo sem permissão de escrita no editor de tags
Pedido do usuário: "em algumas pastas não há permissão de escrita, acredito que seria importante dar um aviso quando esse for o caso na edição de tag".

Já gravado:
- `wamp/src/tags/include/wamp/tag_editor.h` + `src/tags/src/tag_editor.cpp`: `static bool TagEditor::canSave(const std::string& path, bool safeSave, std::string* reason)` — usa `::access` (inclui `<unistd.h>`); sem gravação segura exige escrita no arquivo, com gravação segura exige escrita na pasta.
- `wamp/src/ui/file_info_dialog.h/.cpp`: `bool savingAllowed() const`, `void updateSaveability()`, membros `QLabel* readonlyLabel_`, `QPushButton* okButton_`; aviso âmbar acima do rodapé, OK desabilitado quando não pode gravar; guarda no `saveCurrent()` com `QMessageBox::warning`; `updateSaveability()` chamado no construtor, no fim de `loadCurrent()` e no toggle da caixa de gravação segura.
- `wamp/src/ui/main_window.cpp`: regra `QLabel#readonlyWarning { color: #e0b050; }` no stylesheet.
- `wamp/tools/smoke.cpp`: bloco novo de permissões (arquivo 0444 e pasta sem escrita).

Resultado do `wamp-smoke` (rodado no app instalado):
```
ok    arquivo somente leitura bloqueia gravação direta
FALHA  motivo cita a permissão do arquivo
FALHA  gravação segura passa: a pasta aceita escrita
ok    pasta sem escrita bloqueia a gravação segura
ok    motivo cita a pasta
```

Suspeita para investigar: `writeToneWav()` em `wamp/tools/smoke.cpp:47` — conferir o que ele devolve (caminho completo?) e imprimir o `motivo` real. Se o motivo for "não está mais no disco", o problema é o caminho; se for outro texto, ajustar a checagem do teste.

## Pendências de documentação
- `wamp/docs/DECISIONS.md`: **D-034** (debounce de 400 ms no PlayPause) e **D-035** (aviso de somente leitura + `TagEditor::canSave`).
- `wamp/README.md`: nota sobre o aviso de permissão no File Info.

## Itens que restam na F1c (responder ao usuário)
- `.pls`, `.b4s`, `.wpl` (leitura/gravação), colunas da lista, `mpris:artUrl`.

## Regras do usuário (permanentes)
- Sempre passar o comando de execução via console a cada build.
- Nunca gravar a saída de áudio do sistema do usuário; testar com volume 0.
- `flatpak kill io.github.cassiofiorin.Amplia` antes de capturas; nunca `pkill -f "flatpak run io.github"`.
