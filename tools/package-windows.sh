#!/usr/bin/env bash
# Arma moss-<versión>-windows-x64.zip (guias/17-empaquetado.md). Corre en la
# terminal "MSYS2 UCRT64" de Windows con Visual Studio instalado (de ahí salen
# las DLL de Visual C++); la usa también la CI (package-windows.yml):
#
#   tools/package-windows.sh [--allow-missing-licenses] [--out=<carpeta>]
#
#   --out  dónde dejar el ZIP (por defecto, build/package-windows/out)
#
# Antes, compila: en la raíz, cmake --preset release && cmake --build --preset
# release; en gui/, flutter build windows --release.
#
# 1. Revisa que la versión sea la misma en VERSION, gui/pubspec.yaml,
#    kClientVersion y el --version de moss.exe y moss-rpc.exe.
# 2. Arma build/package-windows/moss-<versión>/: la GUI (lo que deja flutter
#    build en runner/Release: moss-gui.exe, flutter_windows.dll, los plugins y
#    data/), moss.exe y moss-rpc.exe sin símbolos, msvcp140.dll,
#    vcruntime140.dll y vcruntime140_1.dll (Microsoft permite llevarlas junto
#    a la app: en una PC sin el Visual C++ Redistributable no estarían),
#    LEEME.txt y moss.portable.ejemplo (tools/windows-dist/), LICENSE y
#    AVISOS-TERCEROS.txt.
# 3. Revisa con objdump -p qué DLL pide cada .exe y .dll: moss.exe y
#    moss-rpc.exe, solo de Windows (curl y el runtime de GCC van estáticos);
#    los de la GUI, de Windows o de la carpeta. Falla si no.
# 4. Saca el ZIP, con todo dentro de la carpeta moss-<versión>/, y revisa
#    que traiga exactamente lo esperado.
#
# AVISOS-TERCEROS.txt: las licencias de lo que va adentro, de los paquetes de
# MSYS2 (pacman -Qo dice de qué paquete es cada biblioteca), de las fuentes
# que bajó FetchContent (FTXUI, md4c y curl), el OFL de Geist y el NOTICES que
# genera flutter build. Si un paquete de MSYS2 no instala su licencia, los
# textos salen de tools/windows-dist/licenses/<paquete sin prefijo>/ (del
# tarball oficial); si la versión instalada no es la de su README.md, avisa sin
# fallar. Si no encuentra el texto de alguna licencia, lo dice y falla sin
# armar el ZIP; con --allow-missing-licenses lo arma igual y
# AVISOS-TERCEROS.txt dice qué falta. Los .txt van con CRLF, para el Bloc de
# notas.
set -euo pipefail
export LC_ALL=C # La salida de pacman en inglés, para leer sus campos.

die() {
    echo "package-windows.sh: $*" >&2
    exit 1
}
say() { echo "==> $*"; }

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
allow_missing=0
out=$root/build/package-windows/out
for arg in "$@"; do
    case $arg in
    --allow-missing-licenses) allow_missing=1 ;;
    --out=*) out=${arg#*=} ;;
    -h | --help)
        sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "${BASH_SOURCE[0]}"
        exit 0
        ;;
    *) die "argumento desconocido: $arg (ver --help)" ;;
    esac
done

[[ ${MSYSTEM:-} == UCRT64 ]] || die "corre esto en la terminal MSYS2 UCRT64 (MSYSTEM=${MSYSTEM:-})"
prefix=${MINGW_PREFIX:-/ucrt64}
for tool in objdump strip pacman cmake gcc gzip cygpath; do
    command -v "$tool" > /dev/null || die "falta $tool"
done

build=$root/build/release
bundle=$root/gui/build/windows/x64/runner/Release
exes=("$build/cli/moss.exe" "$build/cli/moss-rpc.exe")
for exe in "${exes[@]}"; do
    [[ -f $exe ]] || die "no existe $exe: compila antes con cmake --build --preset release"
done
[[ -f $bundle/moss-gui.exe ]] ||
    die "no existe $bundle/moss-gui.exe: compila antes con flutter build windows --release en gui/"

# --- 1. La versión -------------------------------------------------------------

say "Versión"
cmake -DSOURCE_DIR="$root" -P "$root/tests/version.cmake" > /dev/null ||
    die "la versión no coincide (arriba, el detalle)"
version=$(tr -d '\r' < "$root/VERSION")
grep -q "^const kClientVersion = '$version';" "$root/gui/lib/rpc/client.dart" ||
    die "kClientVersion de gui/lib/rpc/client.dart no dice '$version'"
for exe in "${exes[@]}"; do
    name=${exe##*/}
    said=$("$exe" --version < /dev/null | tr -d '\r') || die "$name --version falló"
    [[ $said == "${name%.exe} $version" ]] ||
        die "$name --version dice '$said', no '${name%.exe} $version': ¿build viejo?"
done
echo "$version"

# --- 2. La carpeta ---------------------------------------------------------------

say "Carpeta moss-$version"
folder=moss-$version
pkg=$root/build/package-windows
stage=$pkg/$folder
rm -rf "$stage"
mkdir -p "$stage" "$out"
out=$(cd "$out" && pwd)
zip=$out/moss-$version-windows-x64.zip
rm -f "$zip" # Que un error no deje uno viejo como si fuera nuevo.

cp -r "$bundle"/. "$stage/"
for exe in "${exes[@]}"; do
    cp "$exe" "$stage/"
    strip --strip-unneeded "$stage/${exe##*/}"
done

# Las DLL de Visual C++ del Visual Studio de esta máquina (vswhere).
vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[[ -x $vswhere ]] || die "no encontré vswhere.exe: instala Visual Studio (gui/README.md)"
vs=$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
    -property installationPath | tr -d '\r')
[[ -n $vs ]] || die "Visual Studio sin las herramientas de C++ (gui/README.md)"
vs=$(cygpath -u "$vs")
redist=$(tr -d '\r\n' < "$vs/VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")
crt=$(find "$vs/VC/Redist/MSVC/$redist/x64" -maxdepth 1 -type d -name 'Microsoft.VC*.CRT' -print -quit)
[[ -n $crt ]] || die "no encontré las DLL de Visual C++ en $vs/VC/Redist/MSVC/$redist/x64"
echo "DLL de Visual C++ de $crt"
visual_cpp=(msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
for dll in "${visual_cpp[@]}"; do
    cp "$crt/$dll" "$stage/"
done

# --- 3. Las DLL que pide cada binario ------------------------------------------

# De Windows: api-ms-win-* (el CRT universal, parte de Windows 10) o las de
# System32 que no son de Visual C++ (esas, en el runner, vienen de Visual
# Studio, no de Windows). moss.exe y moss-rpc.exe no pueden pedir ninguna de la
# carpeta: no llevan DLL de terceros.
say "DLL de cada binario"
system32=/c/Windows/System32
from_windows() {
    [[ $1 == api-ms-win-* ]] && return 0
    [[ $1 =~ ^(msvcp|vcruntime|concrt|vccorlib|vcomp|ucrtbased) ]] && return 1
    [[ -f $system32/$1 ]]
}
for file in "$stage"/*.exe "$stage"/*.dll; do
    name=${file##*/}
    mapfile -t deps < <(objdump -p "$file" | sed -n 's/^\s*DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u)
    [[ ${#deps[@]} -gt 0 ]] || die "objdump no reportó ninguna DLL para $name"
    echo "$name: ${deps[*]}"
    for dll in "${deps[@]}"; do
        if [[ $name == moss.exe || $name == moss-rpc.exe ]]; then
            from_windows "$dll" || die "$name pide $dll, que no es de Windows"
        elif ! from_windows "$dll" && [[ ! -f $stage/$dll ]]; then
            die "$name pide $dll, que no es de Windows ni está en la carpeta"
        fi
    done
done

# --- Licencias (AVISOS-TERCEROS.txt) --------------------------------------------------------

missing=()
notices=$stage/AVISOS-TERCEROS.txt
rule='================================================================================'

# Textos guardados en el repositorio para los paquetes de MSYS2 que no
# instalan su licencia: tools/windows-dist/licenses/<paquete sin prefijo>/,
# con un README.md que dice de qué versión son ("- Versión: X").
repo_licenses=$root/tools/windows-dist/licenses
warnings=()

# Deja en $license_files los archivos de licencia de un paquete instalado
# (uno por línea; no se usa $(...) para que $warnings llegue): los que tiene bajo
# share/licenses/; si no tiene, los COPYING* / LICENSE* / LICENCE* que haya
# instalado en otro lado (por ejemplo, share/doc/); si tampoco, los de
# $repo_licenses (todos menos el README.md). Si la versión instalada no es la
# del README.md, avisa en $warnings (sin fallar), para revisar los textos.
package_license_files() {
    local package=$1 files name dir saved installed
    license_files=
    files=$(pacman -Qlq "$package" | grep -E "^$prefix/share/licenses/.+[^/]$" || true)
    if [[ -z $files ]]; then
        files=$(pacman -Qlq "$package" | grep -Ei '/(copying|licen[cs]e)[^/]*$' |
            grep -v '/$' || true)
    fi
    name=${package#"${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}"-}
    dir=$repo_licenses/$name
    if [[ -z $files && -d $dir ]]; then
        files=$(find "$dir" -maxdepth 1 -type f ! -name README.md | sort)
        saved=$(sed -n 's/^- Versión: *//p' "$dir/README.md" 2> /dev/null | head -n 1)
        installed=$(package_version "$package")
        installed=${installed#*:} # Sin la época (1:2.3.8-4).
        installed=${installed%-*} # Sin el número de compilación de MSYS2.
        if [[ $saved != "$installed" ]]; then
            local text="$package $installed: los textos de tools/windows-dist/licenses/$name"
            text+=" son de la versión ${saved:-(sin \"- Versión\" en su README.md)};"
            warnings+=("$text revísalos con el tarball de la $installed")
        fi
    fi
    license_files=$files
}

# Escribe una sección: título, datos y el texto de cada archivo de licencia.
#   section <título> <detalle> <archivos (uno por línea)>
section() {
    local title=$1 detail=$2 files=$3 file
    {
        printf '\n%s\n%s\n%s\n' "$rule" "$title" "$rule"
        printf '%s\n' "$detail"
        if [[ -z $files ]]; then
            printf '\nNO SE ENCONTRÓ EL TEXTO DE LA LICENCIA.\n'
        fi
    } >> "$notices"
    if [[ -z $files ]]; then
        missing+=("$title")
        return
    fi
    while IFS= read -r file; do
        {
            local shown=${file#"$prefix"/}
            shown=${shown#"$build"/}
            if [[ $file == "$repo_licenses"/* ]]; then
                shown="${file#"$root"/} (de las fuentes oficiales; el paquete no lo instala)"
            fi
            printf '\n--- %s ---\n\n' "$shown"
            cat "$file"
        } >> "$notices"
    done <<< "$files"
}

package_version() { pacman -Q "$1" | awk '{ print $2 }'; }
package_licenses() { pacman -Qi "$1" | sed -n 's/^Licenses *: //p'; }

# Sección de un paquete de MSYS2.
#   package_section <paquete> <cómo va incluido>
package_section() {
    local package=$1 how=$2
    package_license_files "$package"
    section "$package $(package_version "$package")" \
        "Incluido como: $how"$'\n'"Licencia según pacman: $(package_licenses "$package")" \
        "$license_files"
}

cat > "$notices" << EOF
AVISOS DE TERCEROS - Moss $version para Windows (x64)

moss-gui.exe, moss.exe y moss-rpc.exe son software libre bajo la Licencia
Pública General de GNU, versión 3 o, a tu elección, cualquier versión
posterior (ver LICENSE). Llevan dentro:

- moss.exe y moss-rpc.exe, enlazados de forma estática: curl, md4c,
  nlohmann/json, winpthread y partes del runtime de GCC y de mingw-w64;
  moss.exe, además, FTXUI. No llevan DLL de terceros.
- la interfaz gráfica (moss-gui.exe, las .dll de Flutter y data/): Flutter,
  su motor y los paquetes de Dart, y las fuentes Geist y Geist Mono; y las
  DLL de Visual C++ de Microsoft.

Abajo está la licencia de cada uno, en qué archivos va, y su texto tal como
lo instala su paquete o como viene en sus fuentes.

Generado por tools/package-windows.sh.
EOF

# Enlazadas estáticas desde las fuentes de FetchContent. FTXUI siempre sale de
# ahí (cli/CMakeLists.txt ya no usa la de MSYS2): la v7.0.3 con el parche del
# #1351 de upstream, que solo toca la lectura de la terminal en POSIX.
ftxui_src=$build/_deps/ftxui-src
ftxui_version=$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$ftxui_src/CMakeLists.txt" 2> /dev/null |
    head -n 1 || true)
section "FTXUI ${ftxui_version:-(versión desconocida)}" \
    "Incluido como: enlazado estático en moss.exe (fuentes de FetchContent)"$'\n'"Con el parche del PR #1351 de FTXUI (https://github.com/ArthurSonzogni/FTXUI/pull/1351),"$'\n'"que solo cambia la lectura de la terminal en Linux y macOS: el código que se"$'\n'"compila para Windows es el de la versión publicada." \
    "$(ls "$ftxui_src"/LICENSE 2> /dev/null || true)"

md4c_src=$build/_deps/md4c-src
section "md4c 0.6.0" \
    "Incluido como: enlazado estático en moss.exe y moss-rpc.exe"$'\n'"(solo la biblioteca de parseo; fuentes de FetchContent)" \
    "$(ls "$md4c_src"/LICENSE* 2> /dev/null || true)"

# nlohmann/json es solo headers: el paquete dueño del header.
json_package=$(pacman -Qqo "$prefix/include/nlohmann/json.hpp" 2> /dev/null) ||
    die "no se encontró el paquete de nlohmann/json ($prefix/include/nlohmann/json.hpp)"
package_section "$json_package" \
    "compilado dentro de moss.exe y moss-rpc.exe (biblioteca de solo headers)"

# Runtime de GCC enlazado estático (libgcc.a, crtbegin/crtend y el código de
# las plantillas de libstdc++): GPL 3 con la GCC Runtime Library Exception.
libgcc=$(gcc -print-libgcc-file-name)
gcc_package=$(pacman -Qqo "$libgcc") || die "no se encontró el paquete de $libgcc"
# El paquete del compilador puede no traer los textos; los de las DLL de GCC
# (gcc-libs, dueño de libstdc++-6.dll) son del mismo proyecto y con la misma
# licencia. Se usa el primero que traiga la excepción (COPYING.RUNTIME); si
# ninguno, el primero con algún texto, y se avisa que falta la excepción.
stdcxx_package=$(pacman -Qqo "$prefix/bin/libstdc++-6.dll" 2> /dev/null || true)
gcc_files=
gcc_from=$gcc_package
for candidate in "$gcc_package" $stdcxx_package; do
    package_license_files "$candidate"
    files=$license_files
    if grep -q 'COPYING\.RUNTIME' <<< "$files"; then
        gcc_files=$files
        gcc_from=$candidate
        break
    fi
    if [[ -z $gcc_files && -n $files ]]; then
        gcc_files=$files
        gcc_from=$candidate
    fi
done
section "Runtime de GCC ($gcc_package $(package_version "$gcc_package"))" \
    "Incluido como: partes enlazadas estáticas en moss.exe y moss-rpc.exe"$'\n'"Licencia según pacman: $(package_licenses "$gcc_package")"$'\n'"Textos tomados del paquete: $gcc_from" \
    "$gcc_files"
if [[ -n $gcc_files ]] && ! grep -q 'COPYING\.RUNTIME' <<< "$gcc_files"; then
    missing+=("GCC Runtime Library Exception (COPYING.RUNTIME)")
fi

# mingw-w64: el código de arranque y la parte estática del CRT (crt2.o,
# libmingwex).
crt_package=$(pacman -Qqo "$prefix/lib/crt2.o") || die "no se encontró el paquete de crt2.o"
package_section "$crt_package" \
    "partes enlazadas estáticas en moss.exe y moss-rpc.exe (arranque y CRT de"$'\n'"mingw-w64)"

# winpthread (los hilos de C++ de MinGW), enlazada estática con -static.
pthread_package=$(pacman -Qqo "$prefix/lib/libwinpthread.a") ||
    die "no se encontró el paquete de libwinpthread.a"
package_section "$pthread_package" "enlazado estático en moss.exe y moss-rpc.exe"

# curl, estática desde las fuentes de FetchContent (core/CMakeLists.txt).
curl_src=$build/_deps/curl-src
curl_version=$(sed -n 's/^#define LIBCURL_VERSION "\(.*\)"$/\1/p' \
    "$curl_src/include/curl/curlver.h" 2> /dev/null || true)
section "curl ${curl_version:-(versión desconocida)}" \
    "Incluido como: enlazado estático en moss.exe y moss-rpc.exe (fuentes de"$'\n'"FetchContent; solo HTTP y HTTPS, con el TLS de Windows)" \
    "$(ls "$curl_src"/COPYING 2> /dev/null || true)"

# La interfaz gráfica: Geist y el NOTICES de flutter build (motor, framework y
# paquetes de Dart, cada uno con su licencia). Que cada paquete de Dart esté en
# ese NOTICES lo revisa tools/package-linux.sh, con el mismo pubspec.lock.
notices_z=$stage/data/flutter_assets/NOTICES.Z
[[ -f $notices_z ]] || die "el bundle no trae data/flutter_assets/NOTICES.Z"
flutter_notices=$pkg/flutter-NOTICES.txt
gzip -dc "$notices_z" > "$flutter_notices"
section "Geist y Geist Mono" \
    "Incluido como: fuentes de la interfaz gráfica (data/flutter_assets/assets/fonts)." \
    "$root/gui/assets/fonts/OFL.txt"
section "Flutter, su motor y los paquetes de Dart" \
    "Incluido como: moss-gui.exe, flutter_windows.dll, url_launcher_windows_plugin.dll"$'\n'"y data/. El texto es el NOTICES que genera flutter build"$'\n'"(data/flutter_assets/NOTICES.Z), con cada componente y su licencia." \
    "$flutter_notices"
cat >> "$notices" << NOTICE

$rule
Microsoft Visual C++ Redistributable ($(basename "$crt"), $redist)
$rule
Incluido como: ${visual_cpp[*]}, que piden moss-gui.exe,
flutter_windows.dll y url_launcher_windows_plugin.dll.

Son de Microsoft, firmadas por Microsoft, y van junto a la app como
"código distribuible" de Visual Studio, según sus términos de licencia:
https://learn.microsoft.com/visualstudio/releases/2022/redistribution
NOTICE

for warning in "${warnings[@]}"; do
    echo "AVISO: $warning" >&2
    if [[ -n ${GITHUB_ACTIONS:-} ]]; then
        echo "::warning title=Licencia por revisar::$warning"
    fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
    {
        printf '\n%s\nLICENCIAS QUE NO SE ENCONTRARON\n%s\n' "$rule" "$rule"
        printf -- '- %s\n' "${missing[@]}"
    } >> "$notices"
    echo "No se encontró el texto de la licencia de:" >&2
    printf -- '- %s\n' "${missing[@]}" >&2
    if [[ $allow_missing -eq 0 ]]; then
        die "falta el texto de alguna licencia (con --allow-missing-licenses se arma el ZIP igual)"
    fi
fi

# --- 4. Textos y ZIP ------------------------------------------------------------

say "ZIP"
cp "$root/LICENSE" "$stage/LICENSE"
cp "$root/tools/windows-dist/LEEME.txt" "$root/tools/windows-dist/moss.portable.ejemplo" \
    "$stage/"
for text in LICENSE LEEME.txt AVISOS-TERCEROS.txt moss.portable.ejemplo; do
    sed -i 's/\r*$/\r/' "$stage/$text"
done

expected=(moss-gui.exe flutter_windows.dll url_launcher_windows_plugin.dll data
          "${visual_cpp[@]}" moss.exe moss-rpc.exe
          LEEME.txt AVISOS-TERCEROS.txt LICENSE moss.portable.ejemplo)
wanted=$(printf '%s\n' "${expected[@]}" | sort)
got=$(ls -A "$stage" | sort)
if [[ $got != "$wanted" ]]; then
    diff <(echo "$wanted") <(echo "$got") >&2 || true
    die "la carpeta no trae exactamente lo esperado (arriba: < falta, > sobra)"
fi
(cd "$pkg" && cmake -E tar cf "$zip" --format=zip -- "$folder")

# Todo dentro de moss-<versión>/ y, arriba, exactamente lo esperado.
listed=$(cmake -E tar tf "$zip" | sed 's/\r$//')
if grep -qv "^$folder/" <<< "$listed"; then
    die "el ZIP trae algo fuera de $folder/"
fi
top=$(sed -n "s|^$folder/\([^/]*\).*|\1|p" <<< "$listed" | grep -v '^$' | sort -u)
[[ $top == "$wanted" ]] || die "el ZIP no trae exactamente lo esperado"

echo "Listo: $zip ($(du -k "$zip" | cut -f1) KB, $(du -sk "$stage" | cut -f1) KB sin comprimir)"
