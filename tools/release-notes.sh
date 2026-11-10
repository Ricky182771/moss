#!/usr/bin/env bash
# Las notas de un release: la sección "## <versión>" de CHANGELOG.md, sin el
# título, hasta la siguiente "## ". Falla si no está o está vacía.
#
#   tools/release-notes.sh <versión>
set -euo pipefail
[[ $# -eq 1 ]] || { echo "uso: $0 <versión>" >&2; exit 1; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
notes=$(awk -v v="## $1" '$0 == v { on = 1; next } /^## / { on = 0 } on' "$root/CHANGELOG.md")
if [[ -z ${notes//[[:space:]]/} ]]; then
    echo "release-notes.sh: CHANGELOG.md no tiene notas en \"## $1\"" >&2
    exit 1
fi
printf '%s\n' "$notes"
