#!/usr/bin/env bash
# La sonda de tool calling (moss-tools-probe) de punta a punta contra
# tools/fake-llm.py, sin red ni keys: los cuatro escenarios deben salir "bien",
# el modelo falso debe "exigir el pensamiento de regreso" y las peticiones que
# recibió el servidor deben ser las de la guía (guias/18-moss-code.md, 3.1-3.4).
#
# Uso: tools/check-tools-probe.sh <moss-tools-probe>
#
# CHECK_PROBE_SCRIPT=<guion.json> cambia el guion (solo para ver que el script
# falla cuando debe; por defecto, tests/data/fake-llm/sonda-con-index.json).
set -euo pipefail
export LC_ALL=C.UTF-8

probe=${1:?Uso: tools/check-tools-probe.sh <moss-tools-probe>}
repo=$(cd "$(dirname "$0")/.." && pwd)
script=${CHECK_PROBE_SCRIPT:-$repo/tests/data/fake-llm/sonda-con-index.json}
test_key=ci-sin-key-real

fail() {
    echo "FALLÓ: $*"
    exit 1
}

# Nada del entorno de quien lo corre: ni sus CHAT_*, ni su configuración.
while read -r v; do unset "$v"; done < <(compgen -e | grep '^CHAT_' || true)
unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY
export NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost

work=$(mktemp -d)
server_pid=
cleanup() {
    [ -z "$server_pid" ] || kill "$server_pid" 2> /dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

export HOME=$work/home XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data \
    XDG_STATE_HOME=$work/state XDG_CACHE_HOME=$work/cache
mkdir -p "$HOME"

port=$(python3 -I -c 'import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])')
python3 -I "$repo/tools/fake-llm.py" --port "$port" --script "$script" \
    --log-requests "$work/req.jsonl" --require-reasoning-replay 2> "$work/fake.err" &
server_pid=$!

up=false
for _ in $(seq 100); do
    if (exec 3<> "/dev/tcp/127.0.0.1/$port") 2> /dev/null; then
        up=true
        break
    fi
    kill -0 "$server_pid" 2> /dev/null || { cat "$work/fake.err"; fail "fake-llm.py no arrancó"; }
    sleep 0.1
done
$up || fail "fake-llm.py no escucha en el puerto $port"

export CHAT_BASE_URL=http://127.0.0.1:$port/v1 CHAT_API_KEY=$test_key CHAT_DATA_DIR=$work/chats
status=0
timeout 120 "$probe" fake > "$work/tabla.txt" || status=$?
cat "$work/tabla.txt"
[ "$status" -eq 0 ] || fail "la sonda salió con $status (se esperaba 0)"

# Escenarios 1-4: "bien" (la fila 5 también empieza con "2 ", se salta).
for n in 1 2 3 4; do
    row=$(grep -E "^  $n [a-z]" "$work/tabla.txt" | grep -v 'sin pensamiento' || true)
    [ -n "$row" ] || fail "no hay fila del escenario $n en la tabla"
    grep -qE '  bien( |$)' <<< "$row" || fail "el escenario $n no dice \"bien\": $row"
done
grep -q 'Exige el pensamiento de regreso: sí' "$work/tabla.txt" ||
    fail "la sonda no dice que el modelo exige el pensamiento de regreso"

# Lo que recibió el servidor.
python3 -I - "$work/req.jsonl" "$test_key" << 'PY' || fail "las peticiones registradas no cumplen (arriba, el motivo)"
import json, sys

path, key = sys.argv[1:]
raw = open(path, encoding="utf-8").read().splitlines()
bad = []
for i, line in enumerate(raw):
    for what in ("authorization", "parallel_tool_calls", key.lower()):
        if what in line.lower():
            bad.append("petición %d: contiene %r" % (i, what))
reqs = [json.loads(line) for line in raw]
if not reqs:
    bad.append("el servidor no recibió peticiones")
second_step = 0
for i, r in enumerate(reqs):
    tools = r.get("tools")
    if not isinstance(tools, list) or sorted(t.get("function", {}).get("name") for t in tools) != ["glob", "read"]:
        bad.append("petición %d: tools no son read y glob" % i)
    if r.get("tool_choice") != "auto":
        bad.append("petición %d: tool_choice no es \"auto\"" % i)
    msgs = r.get("messages", [])
    for j, m in enumerate(msgs):
        if m.get("role") != "assistant" or not m.get("tool_calls"):
            continue
        second_step += 1
        if m.get("content") is not None and not isinstance(m["content"], str):
            bad.append("petición %d: content del asistente no es nulo ni cadena" % i)
        # Todas menos la última petición son de escenarios con pensamiento de regreso.
        has = bool(m.get("reasoning_content"))
        if has != (i < len(reqs) - 1):
            bad.append("petición %d: reasoning_content %s" % (i, "falta" if not has else "no debía ir"))
        for c in m["tool_calls"]:
            answers = [x for x in msgs[j + 1:] if x.get("role") == "tool" and x.get("tool_call_id") == c.get("id")]
            if not c.get("id") or len(answers) != 1 or not isinstance(answers[0].get("content"), str):
                bad.append("petición %d: la llamada %r no tiene exactamente un mensaje tool con su id" % (i, c.get("id")))
if second_step == 0:
    bad.append("ninguna petición de segundo paso con un asistente con tool_calls")
for b in bad:
    print(b)
sys.exit(1 if bad else 0)
PY

echo "OK"
