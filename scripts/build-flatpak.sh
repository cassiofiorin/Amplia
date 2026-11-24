#!/usr/bin/env bash
# Build e instalação local do Amplia como Flatpak.
#
# Os artefatos ficam fora do repositório (~/.cache/amplia-build) para que o
# diretório do projeto continue limpo — e porque o manifest usa `type: dir`,
# que copiaria qualquer lixo de build junto com o código.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$here/packaging/io.github.cassiofiorin.Amplia.yml"
work="${AMPLIA_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/amplia-build}"
vendored="${XDG_DATA_HOME:-$HOME/.local/share}/wamp-tools/flatpak-builder/usr/bin/flatpak-builder"

mkdir -p "$work/state"

# Ordem de preferência:
#  1. FLATPAK_BUILDER_BIN explícito;
#  2. flatpak-builder no PATH (o caso normal, `sudo dnf install flatpak-builder`);
#  3. binário extraído do RPM em ~/.local/share/wamp-tools (sem root — ver README);
#  4. o app org.flatpak.Builder. Atenção: dentro desse sandbox o flatpak-builder
#     não enxerga SDKs instalados só na instalação de usuário e falha com
#     "Unable to find sdk org.kde.Sdk version 6.10".
if [[ -n "${FLATPAK_BUILDER_BIN:-}" ]]; then
  runner=("$FLATPAK_BUILDER_BIN")
elif command -v flatpak-builder >/dev/null 2>&1; then
  runner=(flatpak-builder)
elif [[ -x "$vendored" ]]; then
  runner=("$vendored")
elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
  echo "aviso: usando org.flatpak.Builder; ele não vê o SDK na instalação de usuário." >&2
  runner=(flatpak run --command=flatpak-builder org.flatpak.Builder)
else
  echo "erro: nenhum flatpak-builder disponível. Opções:" >&2
  echo "  sudo dnf install flatpak-builder" >&2
  echo "  ou extraia o RPM em \$XDG_DATA_HOME/wamp-tools (ver wamp/README.md)" >&2
  echo "  ou flatpak install --user flathub org.flatpak.Builder" >&2
  exit 1
fi

echo "flatpak-builder: ${runner[*]}"
# AMPLIA_BUILD_JOBS limita a paralelização: em máquina com pouca memória livre o ninja
# padrão (nproc) estoura e o build morre com SIGKILL (código 137).
jobs=()
if [[ -n "${AMPLIA_BUILD_JOBS:-}" ]]; then jobs=(--jobs="$AMPLIA_BUILD_JOBS"); fi

exec "${runner[@]}" \
  --user --install --force-clean --disable-rofiles-fuse "${jobs[@]}" \
  --state-dir="$work/state" \
  "$work/build" "$manifest"
