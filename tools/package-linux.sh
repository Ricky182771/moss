#!/usr/bin/env bash
# Arma los paquetes de Moss para Linux x86_64 (guias/17-empaquetado.md). Lo
# mismo en tu máquina y en la CI:
#
#   tools/package-linux.sh [--skip-build] [--formats=tar,rpm,deb] [--out=<carpeta>]
#
#   --skip-build  usa lo ya compilado (build/release y el bundle de la GUI)
#   --formats     cuáles armar (por defecto, los tres)
#   --out         dónde dejarlos (por defecto, build/package/out)
#
# 1. Revisa que la versión sea la misma en VERSION, gui/pubspec.yaml,
#    kClientVersion y el --version de los ejecutables.
# 2. Compila el C++ con el preset release y la GUI con flutter build linux.
# 3. Arma en build/package/root el árbol que se instala: /usr/lib/moss (la
#    GUI con moss, moss-rpc y moss-telegram al lado, sin símbolos de
#    depuración, AVISOS-TERCEROS.txt y LICENSE) y /usr/share (.desktop,
#    íconos y metainfo, validados).
# 4. Saca moss-<versión>-linux-x86_64.tar.gz (la carpeta moss-<versión>/ con
#    LEEME.md y share/) y, con nfpm (packaging/nfpm.yaml), el .rpm y el .deb.
# 5. Escribe SHA256SUMS.
# 6. Revisa con ldd cada ejecutable y cada .so: falla si falta alguno.
#
# Fuera de build/ solo escribe en --out.
set -euo pipefail
export LC_ALL=C

die() {
    echo "package-linux.sh: $*" >&2
    exit 1
}
say() { echo "==> $*"; }

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
skip_build=0
formats=tar,rpm,deb
out=
for arg in "$@"; do
    case $arg in
    --skip-build) skip_build=1 ;;
    --formats=*) formats=${arg#*=} ;;
    --out=*) out=${arg#*=} ;;
    -h | --help)
        sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "${BASH_SOURCE[0]}"
        exit 0
        ;;
    *) die "argumento desconocido: $arg (ver --help)" ;;
    esac
done
[[ -n $formats ]] || die "--formats vacío"
for format in ${formats//,/ }; do
    case $format in
    tar | rpm | deb) ;;
    *) die "formato desconocido: $format (tar, rpm o deb)" ;;
    esac
done
want() { [[ ,$formats, == *,$1,* ]]; }

# need <programa> <paquete de Fedora> <paquete de Ubuntu>
need() {
    command -v "$1" > /dev/null ||
        die "falta $1. Fedora: sudo dnf install $2 · Ubuntu: sudo apt install $3"
}
need cmake cmake cmake
need objdump binutils binutils
need strip binutils binutils
need ldd glibc-common libc-bin
need sha256sum coreutils coreutils
need gzip gzip gzip
need python3 python3 python3
need desktop-file-validate desktop-file-utils desktop-file-utils
need appstreamcli appstream appstream
need rsvg-convert librsvg2-tools librsvg2-bin
command -v flutter > /dev/null || die "falta flutter 3.47.9 (gui/README.md)"
if want rpm || want deb; then
    command -v nfpm > /dev/null ||
        die "falta nfpm (no está en los repositorios de Fedora ni de Ubuntu): baja" \
            "nfpm_<versión>_Linux_x86_64.tar.gz de https://github.com/goreleaser/nfpm/releases" \
            "y pon el binario nfpm en ~/.local/bin"
fi
if ((!skip_build)); then
    need ninja ninja-build ninja-build
fi

id=io.github.ricky182771.moss
build=$root/build/release/cli
bundle=$root/gui/build/linux/x64/release/bundle
programs=(moss moss-rpc moss-telegram moss-telegram-probe)

# --- 1. La versión -------------------------------------------------------------

say "Versión"
cmake -DSOURCE_DIR="$root" -P "$root/tests/version.cmake" > /dev/null ||
    die "la versión no coincide (arriba, el detalle)"
version=$(< "$root/VERSION")
grep -q "^const kClientVersion = '$version';" "$root/gui/lib/rpc/client.dart" ||
    die "kClientVersion de gui/lib/rpc/client.dart no dice '$version'"
# La fecha del release (metainfo) es la del último commit que cambió VERSION;
# la de los archivos del .tar.gz, la del último commit.
release_date=$(git -C "$root" log -1 --format=%cs -- VERSION 2> /dev/null || true)
epoch=$(git -C "$root" log -1 --format=%ct 2> /dev/null || true)
[[ -n $release_date && -n $epoch ]] || die "sin la historia de git (en la CI: fetch-depth: 0)"
echo "$version, del $release_date"

# --- 2. Compilar ---------------------------------------------------------------

if ((!skip_build)); then
    say "C++ (preset release)"
    (cd "$root" && cmake --preset release && cmake --build --preset release)
    say "GUI (flutter build linux --release)"
    (cd "$root/gui" && flutter build linux --release)
fi
for program in "${programs[@]}"; do
    [[ -x $build/$program ]] ||
        die "no existe $build/$program: compila sin --skip-build (con Telegram encendido)"
done
[[ -x $bundle/moss-gui ]] || die "no existe $bundle/moss-gui: compila sin --skip-build"
for program in moss moss-rpc moss-telegram; do
    said=$("$build/$program" --version < /dev/null) || die "$program --version falló"
    [[ $said == "$program $version" ]] ||
        die "$program --version dice '$said', no '$program $version': ¿build viejo? (sin --skip-build)"
done

# --- 3. El árbol que se instala ------------------------------------------------

pkg=$root/build/package
out=${out:-$pkg/out}
mkdir -p "$out"
out=$(cd "$out" && pwd)
tar_name=moss-$version-linux-x86_64.tar.gz
rpm_name=moss-$version-1.x86_64.rpm
deb_name=moss_$version-1_amd64.deb
# Sin los de una corrida anterior, para que un error no deje uno viejo.
rm -f "$out/$tar_name" "$out/$rpm_name" "$out/$deb_name" "$out/SHA256SUMS"
rm -rf "$pkg/root" "$pkg/tar"
app=$pkg/root/usr/lib/moss
share=$pkg/root/usr/share
mkdir -p "$app"

say "Carpeta de la app ($app)"
cp -R "$bundle/." "$app/"
for program in "${programs[@]}"; do
    cp "$build/$program" "$app/"
done
cp "$root/LICENSE" "$app/LICENSE"
# libflutter_linux_gtk.so y libapp.so ya vienen sin símbolos; los demás no.
strip --strip-unneeded "$app/moss-gui" "${programs[@]/#/$app/}" "$app"/lib/*_plugin.so

say "AVISOS-TERCEROS.txt"
rule='================================================================================'
# section <título> <cómo va incluido> <archivo>...
section() {
    local title=$1 how=$2 file
    shift 2
    printf '\n%s\n%s\n%s\n%s\n' "$rule" "$title" "$how" "$rule"
    for file in "$@"; do
        [[ -s $file ]] || die "falta el texto de la licencia de $title ($file)"
        printf '\n'
        cat "$file"
    done
}
# Los archivos de licencia del paquete del sistema dueño de <archivo>.
system_license_files() {
    local package
    if command -v rpm > /dev/null && package=$(rpm -qf --qf '%{NAME}\n' "$1" 2> /dev/null); then
        rpm -qL "$package" | grep -v '^(contains no files)$' || true
    elif command -v dpkg-query > /dev/null && package=$(dpkg-query -S "$1" 2> /dev/null); then
        echo "/usr/share/doc/${package%%:*}/copyright"
    fi
}

deps=$root/build/release/_deps
[[ -d $deps/ftxui-src && -d $deps/md4c-src ]] ||
    die "no están las fuentes de FTXUI y md4c en $deps: compila sin --skip-build"
ftxui_version=$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$deps/ftxui-src/CMakeLists.txt" | head -n 1)
json_header=/usr/include/nlohmann/json.hpp
[[ -f $json_header ]] || die "no encontré $json_header (nlohmann/json del sistema)"
mapfile -t json_licenses < <(system_license_files "$json_header")
[[ ${#json_licenses[@]} -gt 0 ]] || die "no encontré la licencia del paquete de $json_header"
notices_z=$bundle/data/flutter_assets/NOTICES.Z
[[ -f $notices_z ]] || die "el bundle no trae data/flutter_assets/NOTICES.Z"
flutter_notices=$pkg/flutter-NOTICES.txt
gzip -dc "$notices_z" > "$flutter_notices"

# Cada paquete de Dart que va en la app (sin los de desarrollo) tiene que
# estar en el NOTICES de Flutter; los del SDK (flutter, sky_engine...) los
# cubre la licencia de Flutter.
(cd "$root/gui" && flutter pub deps --json 2> /dev/null) > "$pkg/pub-deps.json" ||
    die "flutter pub deps falló (corre flutter pub get en gui/)"
python3 -I - "$pkg/pub-deps.json" "$flutter_notices" << 'EOF' || die "faltan licencias de Dart (arriba)"
import json, re, sys

deps = json.load(open(sys.argv[1]))
packages = {p["name"]: p for p in deps["packages"]}
seen, pending = set(), list(packages[deps["root"]]["directDependencies"])
while pending:
    name = pending.pop()
    if name not in seen:
        seen.add(name)
        pending += packages[name].get("dependencies", [])
# NOTICES: bloques separados por una línea de 80 guiones; cada uno empieza
# con los nombres de los paquetes, uno por línea, y una línea vacía.
names = set()
for block in re.split(r"\n-{80}\n", "\n" + open(sys.argv[2]).read()):
    for line in block.strip("\n").split("\n"):
        if not line.strip():
            break
        names.add(line.strip())
missing = sorted(n for n in seen if packages[n]["source"] != "sdk" and n not in names)
if missing:
    print("sin licencia en NOTICES:", ", ".join(missing), file=sys.stderr)
    sys.exit(1)
print(f"{len(seen)} paquetes de Dart, todos en NOTICES")
EOF

{
    cat << EOF
AVISOS DE TERCEROS - Moss $version para Linux (x86_64)

moss, moss-rpc, moss-telegram y moss-gui son software libre bajo la
Licencia Pública General de GNU, versión 3 o, a tu elección, cualquier
versión posterior (ver LICENSE). Llevan dentro:

- enlazados estáticos: md4c y nlohmann/json (en moss, moss-rpc y
  moss-telegram) y FTXUI (en moss);
- en la interfaz gráfica (moss-gui, lib/ y data/): Flutter, su motor y los
  paquetes de Dart, y las fuentes Geist y Geist Mono.

Las bibliotecas del sistema (glibc, libstdc++, libcurl, GTK 3...) no van
adentro: las instala el sistema, con sus propias licencias.

Generado por tools/package-linux.sh.
EOF
    section "FTXUI ${ftxui_version:-7.0.3}" \
        "Incluido como: enlazado estático en moss (fuentes de FetchContent), con el
parche del PR #1351 de FTXUI (https://github.com/ArthurSonzogni/FTXUI/pull/1351)." \
        "$deps/ftxui-src/LICENSE"
    section "md4c 0.6.0" \
        "Incluido como: enlazado estático en moss, moss-rpc y moss-telegram
(solo la biblioteca de parseo; fuentes de FetchContent)." \
        "$deps"/md4c-src/LICENSE*
    section "nlohmann/json" \
        "Incluido como: compilado dentro de moss, moss-rpc y moss-telegram
(biblioteca de solo headers, del sistema donde se compiló)." \
        "${json_licenses[@]}"
    section "Geist y Geist Mono" \
        "Incluido como: fuentes de la interfaz gráfica (data/flutter_assets/assets/fonts)." \
        "$root/gui/assets/fonts/OFL.txt"
    section "Flutter, su motor y los paquetes de Dart" \
        "Incluido como: moss-gui, lib/ y data/. El texto es el NOTICES que genera
flutter build (data/flutter_assets/NOTICES.Z), con cada componente y su licencia." \
        "$flutter_notices"
} > "$app/AVISOS-TERCEROS.txt"

say "Escritorio (.desktop, íconos y metainfo)"
install -D -m 644 "$root/packaging/linux/$id.desktop" "$share/applications/$id.desktop"
install -D -m 644 "$root/packaging/linux/$id.svg" "$share/icons/hicolor/scalable/apps/$id.svg"
for size in 48 128 256; do
    mkdir -p "$share/icons/hicolor/${size}x$size/apps"
    rsvg-convert -w "$size" -h "$size" "$root/packaging/linux/$id.svg" \
        -o "$share/icons/hicolor/${size}x$size/apps/$id.png"
done
mkdir -p "$share/metainfo"
sed -e "s/@VERSION@/$version/" -e "s/@DATE@/$release_date/" \
    "$root/packaging/linux/$id.metainfo.xml.in" > "$share/metainfo/$id.metainfo.xml"
desktop-file-validate "$share/applications/$id.desktop" ||
    die "desktop-file-validate encontró errores"
appstreamcli validate --no-net "$share/metainfo/$id.metainfo.xml" ||
    die "appstreamcli validate encontró errores"

# Permisos: carpetas y ejecutables 755, lo demás 644 (los paquetes van con
# dueño root; el .tar.gz, con uid 0 sin nombre).
find "$pkg/root" -type d -exec chmod 755 {} +
find "$pkg/root" -type f -exec chmod 644 {} +
chmod 755 "$app/moss-gui" "${programs[@]/#/$app/}"

# --- 6 (antes de empaquetar). ldd y dependencias -------------------------------

say "ldd y dependencias del sistema"
# Las que packaging/nfpm.yaml declara (GTK 3 trae pango, cairo, atk,
# harfbuzz, gdk-pixbuf y glib). Una nueva: agrégala aquí y en nfpm.yaml.
known_sonames=(
    libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 ld-linux-x86-64.so.2
    libstdc++.so.6 libgcc_s.so.1 libz.so.1 libcurl.so.4 libepoxy.so.0 libfontconfig.so.1
    libgtk-3.so.0 libgdk-3.so.0 libpangocairo-1.0.so.0 libpango-1.0.so.0 libcairo-gobject.so.2
    libcairo.so.2 libgdk_pixbuf-2.0.so.0 libatk-1.0.so.0 libharfbuzz.so.0 libgio-2.0.so.0
    libgobject-2.0.so.0 libglib-2.0.so.0
)
mapfile -t elves < <(cd "$app" && find . -type f \( -name '*.so' -o -perm -u+x \) | sort)
glibc=0
for elf in "${elves[@]}"; do
    file=$app/${elf#./}
    lddout=$(ldd "$file" 2>&1) || die "ldd falló con ${elf#./}:"$'\n'"$lddout"
    if grep -q 'not found' <<< "$lddout"; then
        die "ldd: a ${elf#./} le falta una biblioteca:"$'\n'"$lddout"
    fi
    # RUNPATH: solo rutas relativas al archivo ($ORIGIN...), nunca una de la
    # máquina donde se compiló.
    runpath=$(objdump -p "$file" | awk '$1 == "RUNPATH" || $1 == "RPATH" { print $2 }' | paste -sd:)
    IFS=: read -ra entries <<< "$runpath"
    for entry in "${entries[@]}"; do
        [[ $entry == '$ORIGIN' || $entry == '$ORIGIN/'* ]] ||
            die "${elf#./} tiene RUNPATH '$runpath': solo se vale \$ORIGIN"
    done
    while read -r soname; do
        [[ -e $app/lib/$soname || " ${known_sonames[*]} " == *" $soname "* ]] ||
            die "${elf#./} enlaza $soname, que no está en known_sonames ni en packaging/nfpm.yaml"
    done < <(objdump -p "$file" | awk '$1 == "NEEDED" { print $2 }')
    # La glibc más nueva que pide (la más alta de GLIBC_x.y en sus símbolos).
    need_glibc=$(objdump -T "$file" | { grep -o 'GLIBC_[0-9.]*' || true; } | sed 's/GLIBC_//' |
        sort -V | tail -n 1)
    printf '  %-40s glibc >= %s\n' "${elf#./}" "${need_glibc:--}"
    if [[ -n $need_glibc ]]; then
        glibc=$(printf '%s\n%s\n' "$glibc" "$need_glibc" | sort -V | tail -n 1)
    fi
done
echo "glibc mínima: $glibc"
# La CI promete una glibc (MOSS_MAX_GLIBC, la de Ubuntu 22.04): si algo pide
# una más nueva, el paquete no instalaría donde dice el README.
if [[ -n ${MOSS_MAX_GLIBC:-} &&
    $(printf '%s\n%s\n' "$glibc" "$MOSS_MAX_GLIBC" | sort -V | tail -n 1) != "$MOSS_MAX_GLIBC" ]]; then
    die "se necesita glibc $glibc, más nueva que MOSS_MAX_GLIBC=$MOSS_MAX_GLIBC"
fi

# --- 4. Paquetes ---------------------------------------------------------------

if want tar; then
    say "$tar_name"
    tree=$pkg/tar/moss-$version
    mkdir -p "$tree"
    chmod 755 "$tree"
    cp -a "$app/." "$tree/"
    cp -a "$share" "$tree/share"
    sed "s/@VERSION@/$version/" "$root/packaging/linux/LEEME.md" > "$tree/LEEME.md"
    chmod 644 "$tree/LEEME.md"
    tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
        -C "$pkg/tar" -czf "$out/$tar_name" "moss-$version"
fi
for format in rpm deb; do
    want "$format" || continue
    name=${format}_name
    say "${!name}"
    (cd "$pkg" && MOSS_VERSION=$version MOSS_GLIBC=$glibc \
        nfpm package --config "$root/packaging/nfpm.yaml" --packager "$format" \
        --target "$out/${!name}" > /dev/null)
done

# --- 5. SHA256SUMS -------------------------------------------------------------

say "SHA256SUMS"
(
    cd "$out"
    files=()
    for name in "$tar_name" "$rpm_name" "$deb_name"; do
        [[ -f $name ]] && files+=("$name")
    done
    sha256sum "${files[@]}" > SHA256SUMS
    cat SHA256SUMS
)
ls -l "$out"
