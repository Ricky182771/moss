#!/usr/bin/env bash
# Prueba de instalación de un paquete de Linux de tools/package-linux.sh, como
# root dentro de un contenedor desechable de la CI (fedora:latest, debian:12,
# ubuntu:22.04; guias/17-empaquetado.md). Nunca en tu máquina: instala y
# desinstala con dnf/apt.
#
#   tools/check-linux-package.sh rpm|deb|tar <carpeta con los paquetes>
#
# 1. Instala el .rpm (dnf) o el .deb (apt) o, con tar, descomprime el .tar.gz
#    en /opt e instala solo las dependencias que dice su LEEME.md.
# 2. moss --version y moss-rpc --version; ldd sin "not found" en cada
#    ejecutable y .so; permisos 755/644 y dueño root; enlaces de /usr/bin al
#    archivo real de /usr/lib/moss; el .desktop, los íconos y el metainfo.
# 3. Una sesión corta de moss-rpc sin red ni configuración: initialize,
#    shutdown y exit por stdin (rpc/PROTOCOL.md, sección 12) → 0.
# 4. moss-gui en un X virtual (Xvfb), con CHAT_DEBUG_RPC: si el registro de
#    moss-rpc trae initialize, la GUI cargó data/ (su código de Dart corrió) y
#    encontró moss-rpc junto a su ejecutable real.
# 5. Desinstala: no queda nada en /usr/lib/moss ni en /usr/bin.
set -euo pipefail
export LC_ALL=C.UTF-8

fail() {
    echo "check-linux-package.sh: $*" >&2
    exit 1
}
say() { echo "==> $*"; }

[[ $# -eq 2 ]] || fail "uso: $0 rpm|deb|tar <carpeta con los paquetes>"
format=$1
dist=$(cd "$2" && pwd)
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(< "$root/VERSION")
id=io.github.ricky182771.moss
export DEBIAN_FRONTEND=noninteractive
# Los espejos de Ubuntu a veces no responden a la primera.
if [[ -d /etc/apt/apt.conf.d ]]; then
    echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-reintentos
fi

# --- 1. Instalar ---------------------------------------------------------------

case $format in
rpm)
    say "dnf install moss-$version-1.x86_64.rpm"
    dnf install -y "$dist/moss-$version-1.x86_64.rpm"
    prefix=/usr/lib/moss
    bin=/usr/bin
    share=/usr/share
    ;;
deb)
    say "apt install moss_$version-1_amd64.deb"
    apt-get update
    apt-get install -y "$dist/moss_$version-1_amd64.deb"
    prefix=/usr/lib/moss
    bin=/usr/bin
    share=/usr/share
    ;;
tar)
    tarball=$dist/moss-$version-linux-x86_64.tar.gz
    mapfile -t leeme < <(tar -xzOf "$tarball" "moss-$version/LEEME.md" |
        sed -n 's/^sudo apt install //p' | tr ' ' '\n')
    [[ ${#leeme[@]} -gt 0 ]] || fail "LEEME.md no dice qué instalar con apt"
    say "apt install ${leeme[*]} (del LEEME.md)"
    apt-get update
    # Como el "sudo apt install" del LEEME.md: con Recommends (libegl1 trae
    # así libegl-mesa0, el que de verdad abre EGL).
    apt-get install -y "${leeme[@]}"
    tar -xzf "$tarball" -C /opt
    prefix=/opt/moss-$version
    bin=$prefix
    share=$prefix/share
    ;;
*) fail "formato desconocido: $format" ;;
esac

# --- 2. Lo instalado -----------------------------------------------------------

say "--version"
[[ $("$bin/moss" --version) == "moss $version" ]] || fail "moss --version no dice moss $version"
[[ $("$bin/moss-rpc" --version < /dev/null) == "moss-rpc $version" ]] ||
    fail "moss-rpc --version no dice moss-rpc $version"

say "ldd"
mapfile -t elves < <(find "$prefix" -type f \( -name '*.so' -o -perm -u+x \) | sort)
[[ ${#elves[@]} -ge 7 ]] || fail "solo ${#elves[@]} ejecutables y .so en $prefix"
for elf in "${elves[@]}"; do
    deps=$(ldd "$elf") || fail "ldd falló con $elf"
    if grep "not found" <<< "$deps"; then
        fail "a $elf le falta una biblioteca"
    fi
    echo "  ${elf#"$prefix"/}: bien"
done

say "Permisos y dueño"
odd=$(find "$prefix" \( -type d ! -perm 755 \) -o \( -type f ! -perm 644 ! -perm 755 \) -o ! -user root)
[[ -z $odd ]] || fail "permisos o dueño fuera de 755/644 y root:"$'\n'"$odd"
for program in moss moss-gui moss-rpc moss-telegram moss-telegram-probe; do
    [[ $(stat -c %a "$prefix/$program") == 755 ]] || fail "$program no es 755"
done
[[ -f $prefix/AVISOS-TERCEROS.txt && -f $prefix/LICENSE && -d $prefix/data && -d $prefix/lib ]] ||
    fail "faltan AVISOS-TERCEROS.txt, LICENSE, data/ o lib/ en $prefix"

if [[ $format != tar ]]; then
    say "Enlaces de /usr/bin"
    for program in moss moss-gui moss-rpc; do
        [[ -L /usr/bin/$program && $(readlink -f "/usr/bin/$program") == "$prefix/$program" ]] ||
            fail "/usr/bin/$program no es un enlace a $prefix/$program"
    done
fi

say "Escritorio"
for file in "applications/$id.desktop" "metainfo/$id.metainfo.xml" \
    "icons/hicolor/scalable/apps/$id.svg" "icons/hicolor/48x48/apps/$id.png" \
    "icons/hicolor/128x128/apps/$id.png" "icons/hicolor/256x256/apps/$id.png"; do
    [[ -s $share/$file ]] || fail "falta $share/$file"
done
grep -q "version=\"$version\"" "$share/metainfo/$id.metainfo.xml" ||
    fail "el metainfo no dice la versión $version"

# --- 3. moss-rpc sin red ni configuración --------------------------------------

say "Sesión de moss-rpc"
home=$(mktemp -d)
status=0
rpc_out=$(printf '%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocol_version":1,"client":{"name":"prueba","version":"0"}}}' \
    '{"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}}' \
    '{"jsonrpc":"2.0","method":"exit"}' |
    env -i HOME="$home" PATH=/usr/bin:/bin timeout 10 "$bin/moss-rpc") || status=$?
echo "$rpc_out"
[[ $status -eq 0 ]] || fail "moss-rpc salió con $status"
grep '"id":1' <<< "$rpc_out" | grep -q "\"name\":\"moss-rpc\",\"version\":\"$version\"" ||
    fail "initialize no respondió con server moss-rpc $version"
grep -q '"id":2,"result":' <<< "$rpc_out" || fail "shutdown no respondió"

# --- 4. La GUI encuentra data/ y moss-rpc --------------------------------------

say "moss-gui en Xvfb"
if [[ $format == rpm ]]; then
    dnf install -y xorg-x11-server-Xvfb mesa-dri-drivers
else
    apt-get install -y --no-install-recommends xvfb libgl1-mesa-dri
fi
Xvfb :99 -screen 0 1280x800x24 &
xvfb=$!
sleep 2
log=$home/rpc.log
status=0
env -i HOME="$home" PATH=/usr/bin:/bin DISPLAY=:99 CHAT_DEBUG_RPC="$log" \
    timeout 20 "$bin/moss-gui" > "$home/gui.txt" 2>&1 || status=$?
kill "$xvfb" || true
cat "$home/gui.txt"
# 124: seguía abierta a los 20 s (lo normal); otra cosa es que se cayó.
[[ $status -eq 124 ]] || fail "moss-gui terminó con $status antes de 20 s"
grep -q '"method":"initialize"' "$log" 2> /dev/null ||
    fail "moss-rpc no recibió initialize de la GUI (¿no encontró data/ o moss-rpc?)"
echo "la GUI arrancó moss-rpc y mandó initialize"

# --- 5. Desinstalar -------------------------------------------------------------

case $format in
rpm) dnf remove -y moss ;;
deb) apt-get remove -y moss ;;
tar) exit 0 ;;
esac
say "Después de desinstalar"
[[ ! -e /usr/lib/moss ]] || fail "quedó /usr/lib/moss: $(find /usr/lib/moss)"
for program in moss moss-gui moss-rpc; do
    [[ ! -e /usr/bin/$program && ! -L /usr/bin/$program ]] || fail "quedó /usr/bin/$program"
done
[[ ! -e $share/applications/$id.desktop ]] || fail "quedó el .desktop"
echo "listo: $format"
