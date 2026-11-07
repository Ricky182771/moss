#!/usr/bin/env bash
# Prueba a mano de la GUI con tools/fake-llm.py, sin tocar tu configuración,
# tus keys ni tus chats: quita todas las CHAT_* de tu entorno y lo lleva todo
# a /tmp/moss-prueba. Las pruebas a mano de la GUI se hacen solo con esto
# (AGENTS.md §16).
#
# Uso: tools/run-fake.sh [--perfil] [--reasoning] [--slow] [--status N]
#                        [--frame-stats]
#   --perfil       el bundle de profile (flutter build linux --profile) en vez
#                  de `flutter run` (debug).
#   --reasoning, --slow, --status N
#                  para fake-llm.py, si este script lo arranca.
#   --frame-stats  para la GUI (gui/README.md).
set -euo pipefail

die() {
	echo "run-fake.sh: $*" >&2
	exit 1
}

raiz=$(cd "$(dirname "$0")/.." && pwd)
prueba=/tmp/moss-prueba
puerto=8765
rpc=$raiz/build/dev/cli/moss-rpc
perfil=false
fake=()
gui=()

while (($#)); do
	case $1 in
	--perfil) perfil=true ;;
	--reasoning | --slow) fake+=("$1") ;;
	--status)
		(($# >= 2)) || die "--status necesita un código"
		fake+=("$1" "$2")
		shift
		;;
	--frame-stats) gui+=("$1") ;;
	-h | --help)
		sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*) die "opción desconocida: $1 (mira --help)" ;;
	esac
	shift
done

# Nada de tu entorno: ni tu key, ni tu modelo, ni tus carpetas.
while read -r v; do unset "$v"; done < <(compgen -e | grep '^CHAT_' || true)
# Sin proxy: lo de la caja va solo a 127.0.0.1.
unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY
export NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost
# HOME y XDG_* también: el addon y su unidad de systemd salen de ahí. Flutter
# sigue usando tu caché de paquetes.
export PUB_CACHE=${PUB_CACHE:-$HOME/.pub-cache}
export HOME=$prueba/home
export XDG_DATA_HOME=$prueba/data
export XDG_STATE_HOME=$prueba/state
export XDG_CACHE_HOME=$prueba/cache
export CHAT_BASE_URL=http://127.0.0.1:$puerto/v1
export CHAT_API_KEY=prueba
export CHAT_MODEL=fake
export CHAT_DATA_DIR=$prueba/chats
export CHAT_DOWNLOAD_DIR=$prueba/descargas
export XDG_CONFIG_HOME=$prueba/config

# Por si alguien edita lo de arriba: rutas reales (sin enlaces) bajo /tmp y el
# servidor solo en esta máquina.
for d in "$CHAT_DATA_DIR" "$CHAT_DOWNLOAD_DIR" "$XDG_CONFIG_HOME" "$HOME" \
	"$XDG_DATA_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"; do
	case $(realpath -m "$d") in
	/tmp/?*) ;;
	*) die "$d no está bajo /tmp; no arranco" ;;
	esac
done
case $CHAT_BASE_URL in
http://127.0.0.1:* | http://localhost:*) ;;
*) die "CHAT_BASE_URL no es 127.0.0.1 ni localhost; no arranco" ;;
esac

[[ -x $rpc ]] || die "falta $rpc (cmake --preset dev && cmake --build --preset dev)"
# Portable: moss.portable junto a moss-rpc ganaría sobre XDG_CONFIG_HOME.
[[ ! -e $(dirname "$rpc")/moss.portable ]] ||
	die "hay un moss.portable junto a $rpc; usaría otra configuración"
(umask 077 && mkdir -p "$prueba" "$HOME")
[[ -O $prueba && ! -L $prueba ]] || die "$prueba no es una carpeta tuya"

escucha() { (exec 3<>"/dev/tcp/127.0.0.1/$puerto") 2>/dev/null; }
es_fake() {
	python3 -I -c 'import sys, urllib.request as u
r = u.urlopen(sys.argv[1], timeout=2)
sys.exit(0 if r.headers.get("Server", "").startswith("fake-llm") else 1)' \
		"$CHAT_BASE_URL/models" 2>/dev/null
}

fake_pid=
if escucha; then
	es_fake || die "algo que no es fake-llm.py ocupa el puerto $puerto"
	((${#fake[@]} == 0)) || echo "run-fake.sh: fake-llm.py ya corre; ignoro ${fake[*]}" >&2
else
	python3 "$raiz/tools/fake-llm.py" --port "$puerto" "${fake[@]}" &
	fake_pid=$!
	trap 'kill "$fake_pid" 2>/dev/null || true' EXIT
	for _ in $(seq 50); do
		escucha && break
		kill -0 "$fake_pid" 2>/dev/null || die "fake-llm.py no arrancó"
		sleep 0.1
	done
	escucha || die "fake-llm.py no escucha en el puerto $puerto"
fi

# Los moss-rpc que ya corrían (de otra app) no cuentan al final.
antes=$(pgrep -u "$(id -u)" -x moss-rpc || true)

estado=0
if $perfil; then
	bundle=$raiz/gui/build/linux/x64/profile/bundle/moss-gui
	[[ -x $bundle ]] || die "falta el bundle de profile (cd gui && flutter build linux --profile)"
	"$bundle" "--rpc=$rpc" "${gui[@]}" || estado=$?
else
	args=(-a "--rpc=$rpc")
	for a in "${gui[@]}"; do args+=(-a "$a"); done
	(cd "$raiz/gui" && flutter run -d linux "${args[@]}") || estado=$?
fi

for pid in $(pgrep -u "$(id -u)" -x moss-rpc || true); do
	grep -qx "$pid" <<<"$antes" && continue
	ps -o pid=,args= -p "$pid" >&2 || true
	echo "run-fake.sh: quedó un moss-rpc vivo (arriba)" >&2
	estado=1
done
exit "$estado"
