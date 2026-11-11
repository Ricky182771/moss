# Guía: 17. Empaquetado de Linux y Windows, y releases
> Las reglas resumidas están en [AGENTS.md](../AGENTS.md).

## 17. Empaquetado de Linux y Windows, y releases

Moss se distribuye en Linux x86_64 en tres formatos, con lo mismo adentro: `moss-<versión>-linux-x86_64.tar.gz`, `moss-<versión>-1.x86_64.rpm` y `moss_<versión>-1_amd64.deb`, más `SHA256SUMS`. Los arma `tools/package-linux.sh`, igual en una máquina de desarrollo y en la CI. En Windows x64, un ZIP: `moss-<versión>-windows-x64.zip` (`tools/package-windows.sh`, [abajo](#windows)). Fuera de alcance: AppImage, Flatpak y Snap (el `ExecStart` de la unidad del bot necesita una ruta fija de `moss` y el sandbox no llega al systemd del usuario), COPR, PPA, AUR, firmas GPG y ARM.

### La versión

- **Una sola fuente: `VERSION`** en la raíz (una línea `X.Y.Z`). CMake la lee para `project()`, y de ahí sale `CHATBOT_VERSION` para `moss`, `moss-rpc` (`initialize` → `server.version`) y `moss-telegram`.
- La GUI la manda en `initialize` (`client.version`) desde `kClientVersion` (`gui/lib/rpc/client.dart`), que `gui/test/apply_test.dart` compara con `gui/pubspec.yaml`. La prueba `chatbot_version` (`tests/version.cmake`) compara `pubspec.yaml` (sin el `+N`) con `VERSION` y corre `--version` de cada ejecutable: `<ejecutable> X.Y.Z` y código 0, sin leer configuración. `tools/package-linux.sh` revisa las tres antes de armar nada, y `release.yml` que la etiqueta sea `v<VERSION>`.
- Subir la versión: `VERSION`, `gui/pubspec.yaml`, `kClientVersion` y una sección `## X.Y.Z` en `CHANGELOG.md`, en el mismo commit.

### Qué va adentro

Una sola carpeta, para que la GUI encuentre `moss-rpc` junto a su ejecutable y `moss telegram` encuentre `moss-telegram` junto a `moss`:

```text
moss-gui                 el bundle de flutter build linux --release
lib/                     libflutter_linux_gtk.so, libapp.so y los plugins
data/                    flutter_assets (con las fuentes Geist), icudtl.dat
moss  moss-rpc  moss-telegram  moss-telegram-probe
AVISOS-TERCEROS.txt      generado por el script
LICENSE                  GPL v3 o posterior (GPL-3.0-or-later)
```

- **`.rpm` y `.deb`:** esa carpeta en `/usr/lib/moss/`, con enlaces en `/usr/bin/` para `moss`, `moss-gui` y `moss-rpc`; además `/usr/share/applications/io.github.ricky182771.moss.desktop`, el ícono en `/usr/share/icons/hicolor/{scalable,48x48,128x128,256x256}/apps/` y `/usr/share/metainfo/io.github.ricky182771.moss.metainfo.xml`. Dueño root; ejecutables y carpetas 755, lo demás 644. `/usr/share` va archivo por archivo (`packaging/nfpm.yaml`), para no hacer al paquete dueño de carpetas del sistema.
- **`.tar.gz`:** la carpeta como `moss-<versión>/`, con `LEEME.md` (cómo correrlo, qué instalar del sistema, qué hacer si se mueve con el bot activo) y `share/` con lo de escritorio, para quien lo quiera copiar a `~/.local/share`.
- **Enlaces simbólicos:** la GUI busca `moss-rpc` con `Platform.resolvedExecutable` y `moss` su propia ruta con `/proc/self/exe` (`executable_path`); los dos dan el archivo real. Abrir `/usr/bin/moss-gui` busca `/usr/lib/moss/moss-rpc`, `lib/` sale de la RPATH `$ORIGIN/lib` y `data/` de `/proc/self/exe`; el `ExecStart` de la unidad del bot queda en `/usr/lib/moss/moss`, nunca en `/usr/bin/moss`. La prueba de instalación lo comprueba arrancando `moss-gui` en Xvfb con `CHAT_DEBUG_RPC`.
- **" (deleted)":** si el binario se reemplaza con el proceso abierto (un `dnf upgrade` con `moss` abierto), Linux le agrega " (deleted)" al destino de `/proc/self/exe`. `executable_path()` lo quita si la ruta sin el sufijo existe (`without_deleted_suffix`): el binario nuevo quedó en el mismo lugar, y F2 escribe un `ExecStart` válido.
- **Sin modo portable:** el paquete no lleva `moss.portable` (con `/usr/lib/moss` de solo lectura, `moss` saldría con el error `Config`). Nada se escribe junto al ejecutable: configuración y chats van a `~/.config/moss` y `~/.local/share/moss`, y desinstalar no los toca.
- **Símbolos:** `strip --strip-unneeded` en `moss-gui`, los cuatro ejecutables de C++ y los plugins; `libflutter_linux_gtk.so` y `libapp.so` ya vienen sin símbolos. Las pruebas, `.dart_tool` y los `build/` no entran.
- **Rutas de la máquina que compila:** `gui/linux/CMakeLists.txt` compila los plugins con su RUNPATH de instalación, `$ORIGIN` (`BUILD_WITH_INSTALL_RPATH`): se copian con `install(FILES)`, que no lo cambia, y se quedaban con la ruta absoluta de `gui/linux/flutter/ephemeral`. El script falla si algún ELF trae un RUNPATH que no empiece con `$ORIGIN` (`moss-gui` lleva `$ORIGIN/lib` y `libflutter_linux_gtk.so` `$ORIGIN`). Lo único que queda es una cadena dentro de `lib/libapp.so` (el AOT de Dart guarda la URI de `.dart_tool/flutter_build/dart_plugin_registrant.dart`): no se usa al correr y no se puede quitar sin ofuscar; en los releases es la ruta de la CI (`/__w/moss/moss/gui/…`), no la de nadie.
- **`moss-telegram-probe`** va en la carpeta, como pide G5a: es la sonda manual del bot ([telegram.md](telegram.md#probar-cómo-se-ven-los-mensajes)) y no corre sola.

### Escritorio

- El `.desktop` se llama como el `APPLICATION_ID` de la GUI (`gui/linux/CMakeLists.txt`), `io.github.ricky182771.moss`: en Wayland el compositor une la ventana con su `.desktop` por ese nombre, y sin eso sale sin ícono. `Exec=moss-gui`, `Categories=Utility;Development;` (dos categorías principales: `desktop-file-validate` da una pista, no un error).
- El ícono (`packaging/linux/io.github.ricky182771.moss.svg`) es el logo de `gui/diseno/DISENO.md`, la "m" con el cursor, en el acento oscuro (`#8DBF5F`) sobre el fondo oscuro (`#0F1411`), en un cuadro redondeado: se lee igual en paneles claros y oscuros. Los PNG de 48, 128 y 256 los genera el script con `rsvg-convert`.
- El metainfo (`packaging/linux/….metainfo.xml.in`) lleva la versión de `VERSION` y la fecha del último commit que la cambió. El script valida los dos con `desktop-file-validate` y `appstreamcli validate --no-net`.

### AVISOS-TERCEROS.txt

Lo genera el script en cada corrida, para que no se quede viejo: FTXUI y md4c de las fuentes de FetchContent (`build/release/_deps`), nlohmann/json del paquete del sistema dueño de `/usr/include/nlohmann/json.hpp` (`rpm -qL` o `/usr/share/doc/<paquete>/copyright`), el OFL de Geist (`gui/assets/fonts/OFL.txt`) y el `NOTICES` que genera `flutter build` (motor, framework y paquetes de Dart, con su licencia cada uno). Antes revisa que cada paquete de Dart que va en la app (`flutter pub deps`, sin los de desarrollo ni los del SDK) esté en ese `NOTICES`; si falta un texto, falla. libcurl, GTK, glibc y libstdc++ no van adentro (son del sistema): no entran.

### Dependencias del paquete

Solo las bibliotecas que los ejecutables enlazan (`NEEDED` de `objdump -p`) y no van en `lib/`, con nombres de Fedora y de Debian en `packaging/nfpm.yaml`: glibc, libstdc++, libgcc, zlib, libcurl, GTK 3 (que trae pango, cairo, atk, harfbuzz, gdk-pixbuf y glib), libepoxy y fontconfig; además EGL y GLES (`libglvnd-egl`/`libglvnd-gles` y `libegl1`/`libgles2`), que Flutter abre con `dlopen` y por eso no salen en `NEEDED` ni en `ldd`: sin ellas `moss-gui` aborta al arrancar, y solo lo atrapa la prueba que arranca la GUI. En Debian, `libcurl4 | libcurl4t64` y `libgtk-3-0 | libgtk-3-0t64` (los nombres después de la transición a `time_t` de 64 bits). La versión mínima de glibc la calcula el script (la más alta de `GLIBC_x.y` en los símbolos) y la pasa a nfpm. Si aparece una biblioteca nueva, el script falla hasta que se agregue a su lista (`known_sonames`) y a `nfpm.yaml`.

### Compatibilidad

Un binario hecho con una glibc nueva no corre con una más vieja. La CI arma en un contenedor `ubuntu:22.04` (glibc 2.35), así los paquetes corren en Debian 12, Ubuntu 22.04 o más nuevos y Fedora reciente. Ahí se usa el GCC del sistema, CMake de `pip` (los presets piden 3.25 y 22.04 trae 3.22) y Flutter 3.47.9.

### El script

`tools/package-linux.sh [--skip-build] [--formats=tar,rpm,deb] [--out=<carpeta>]`: revisa la versión, compila (`cmake --preset release` y `flutter build linux --release`), arma `build/package/root/` (lo que se instala), genera los avisos y lo de escritorio, revisa con `ldd` cada ejecutable y cada `.so` (falla con `not found`) y las dependencias, y saca los paquetes y `SHA256SUMS` en `build/package/out/`. Fuera de `build/` solo escribe en `--out`. Si falta una herramienta, dice cómo instalarla en Fedora y en Ubuntu (`nfpm` no está en sus repositorios: es un binario de sus releases).

```bash
tools/package-linux.sh                       # compila release y la GUI, y arma los tres
tools/package-linux.sh --skip-build          # con lo ya compilado
tools/package-linux.sh --formats=tar,rpm --out=dist
```

Necesita, además de lo de compilar ([11-compilacion.md](11-compilacion.md)) y [Flutter](../gui/README.md), `nfpm` (un solo binario: bájalo de sus [releases](https://github.com/goreleaser/nfpm/releases) y ponlo en `~/.local/bin`), `desktop-file-validate`, `appstreamcli` y `rsvg-convert`:

- Fedora: `sudo dnf install desktop-file-utils appstream librsvg2-tools`
- Ubuntu: `sudo apt install desktop-file-utils appstream librsvg2-bin`

### CI

- **`package-linux.yml`** (reutilizable): arma en `ubuntu:22.04` con `MOSS_MAX_GLIBC=2.35` (el script falla si algo pide una glibc más nueva) y sube `moss-linux-x86_64` (7 días); después, `tools/check-linux-package.sh` instala y prueba en contenedores: el `.rpm` en `fedora:latest`, el `.deb` en `debian:12` y `ubuntu:22.04`, y el `.tar.gz` en `debian:12` con solo lo que dice su `LEEME.md` (en Fedora, antes del checkout, `tar`, `gzip` y `findutils`, que la imagen no trae). En cada uno: `--version`, `ldd` sin `not found`, permisos, enlaces, `.desktop`, ícono y metainfo, una sesión de `moss-rpc` sin red ni configuración (`initialize`, `shutdown`, `exit`), `moss-gui` en Xvfb hasta que manda `initialize` a `moss-rpc`, y desinstalar sin dejar nada en `/usr/lib/moss`.
- **`ci.yml`** lo llama en cada PR (trabajo `paquetes de Linux`).
- **`release.yml`**: con una etiqueta `v*`, revisa que sea `v<VERSION>` y que `CHANGELOG.md` tenga la sección, arma y prueba Linux y Windows (`package-linux.yml` y `package-windows.yml`, 14 días), junta los cuatro paquetes con un solo `SHA256SUMS` (trabajo `files`, artefacto `moss-release`) y crea un GitHub Release **en borrador** con los cinco archivos y las notas de `CHANGELOG.md` (`tools/release-notes.sh`). Con `workflow_dispatch`, lo mismo pero solo deja los artefactos.

### Sacar un release

1. En una rama: subir `VERSION`, `gui/pubspec.yaml` y `kClientVersion`, y escribir la sección `## X.Y.Z` de `CHANGELOG.md`. PR y merge a `main` con la CI en verde.
2. (Opcional) Actions → Release → Run workflow sobre `main`: deja los artefactos para probarlos (`gh run download <id>`).
3. Etiqueta sobre ese commit de `main`: `git tag vX.Y.Z && git push origin vX.Y.Z`.
4. `release.yml` crea el borrador: revisar en GitHub → Releases los cuatro paquetes, `SHA256SUMS` y las notas.
5. Publicarlo a mano.

### Windows

En la terminal **MSYS2 UCRT64**, con Visual Studio y Flutter instalados ([gui/README.md](../gui/README.md)):

```bash
cmake --preset release && cmake --build --preset release
(cd gui && cmd //c flutter build windows --release)
tools/package-windows.sh   # deja build/package-windows/out/moss-<versión>-windows-x64.zip
```

`tools/package-windows.sh [--allow-missing-licenses] [--out=<carpeta>]` corre en la terminal MSYS2 UCRT64 de Windows con Visual Studio, después de `cmake --build --preset release` y `flutter build windows --release` (no compila: el C++ sale de MSYS2 y la GUI de MSVC). Revisa la versión como el de Linux (más el `--version` de `moss.exe` y `moss-rpc.exe`) y deja `moss-<versión>-windows-x64.zip` en `build/package-windows/out/` (o en `--out`), con todo dentro de la carpeta `moss-<versión>\`:

```text
moss-gui.exe  flutter_windows.dll  url_launcher_windows_plugin.dll  data\
msvcp140.dll  vcruntime140.dll  vcruntime140_1.dll
moss.exe  moss-rpc.exe
LEEME.txt  AVISOS-TERCEROS.txt  LICENSE  moss.portable.ejemplo
```

- **Sin `moss-telegram`:** el bot es solo de Linux.
- **DLL de Visual C++:** las tres del `VC\Redist\MSVC\<versión>\x64\Microsoft.VC*.CRT` del Visual Studio de la máquina (`vswhere`), en despliegue local (Microsoft lo permite): una PC de la escuela puede no tener el Visual C++ Redistributable. Vienen firmadas por Microsoft.
- **Revisión de DLL:** `objdump -p` de cada `.exe` y `.dll`. `moss.exe` y `moss-rpc.exe` solo pueden pedir DLL de Windows (`api-ms-win-*` o de `System32` que no sean de Visual C++); los de la GUI, además, las de la carpeta. Si no, falla. Al final revisa que la carpeta y el ZIP traigan exactamente la lista de arriba.
- **Símbolos:** `strip --strip-unneeded` en `moss.exe` y `moss-rpc.exe`; lo de MSVC ya sale sin símbolos de depuración.
- **`LEEME.txt`** (`tools/windows-dist/`): para alumnos, sin tecnicismos: abrirlo (descomprimir primero), SmartScreen, el Control Inteligente de Aplicaciones, el modo portable en la escuela, dónde se guarda todo, desinstalar y requisitos. Los `.txt`, `LICENSE` y `moss.portable.ejemplo` van con CRLF.
- **`AVISOS-TERCEROS.txt`:** generado en cada corrida: FTXUI, md4c y curl de las fuentes de FetchContent; nlohmann/json, el runtime de GCC, el CRT de mingw-w64 y winpthread de sus paquetes de MSYS2 (`pacman -Qo`); el OFL de Geist; el `NOTICES` de `flutter build` (motor, framework y paquetes de Dart; que cada paquete de Dart esté ahí lo revisa `package-linux.sh`, con el mismo `pubspec.lock`); y las DLL de Visual C++ con el enlace a sus términos de redistribución. Si falta un texto, falla (`--allow-missing-licenses` lo arma igual y lo dice; nunca en la CI).

**Enlace estático.** En Windows, curl va estática y mínima por FetchContent (solo HTTP y HTTPS, Schannel, sin OpenSSL, HTTP/2, compresión, IDN, PSL ni SSH) y los `.exe` se enlazan con `-static` (libstdc++, libgcc y winpthread adentro): `moss.exe` y `moss-rpc.exe` no llevan ninguna DLL de terceros: solo piden DLL de Windows ([04-stack.md](04-stack.md)). Schannel usa el almacén de certificados de Windows; en Windows 10, hasta TLS 1.2. HTTP/1.1 en vez de HTTP/2: el streaming SSE no lo necesita. La CI prueba una conversación por HTTPS con TLS 1.2 (`tools/check-rpc-https.ps1`).

**Recursos de versión.** Cada `.exe` lleva nombre de producto y versión, que se ven en Propiedades → Detalles. `moss.exe` y `moss-rpc.exe` los toman de `cmake/windows/version.rc.in` (`chatbot_windows_version` en `CMakeLists.txt`) y `moss-gui.exe` de `gui/windows/runner/Runner.rc`: `ProductName` "Moss", `FileDescription` ("Moss — terminal", "Moss — servidor RPC", "Moss — interfaz gráfica"), `ProductVersion` y `FileVersion` de `VERSION` (en la GUI, por `pubspec.yaml` sin el `+N`: `MOSS_VERSION` en `runner/CMakeLists.txt`), `CompanyName` "Ricky182771" y `LegalCopyright` con la GPL. El ícono (`packaging/windows/moss.ico`, el cuadro redondeado de Linux) va en `moss-gui.exe` y `moss.exe`; se regenera con `python3 -I packaging/windows/make-ico.py` (necesita `rsvg-convert`) cuando cambia el SVG. La prueba del ZIP lee los metadatos de los tres y falla si la versión no coincide.

**Prueba (`package-windows.yml`).** El trabajo `armar` (`windows-2025`: MSYS2 y Flutter) compila y empaqueta; `probar` baja el ZIP en otro runner sin MSYS2 y corre `tools/check-windows-zip.ps1`: lo descomprime en una ruta con espacios y acentos y, con solo Windows en el `PATH`, revisa los recursos de versión, `--version`, `moss-rpc.exe` (`initialize`, `shutdown`, `exit`) y `moss.exe` sin key, primero sin `moss.portable` (sin `datos\`) y luego con él (todo en `datos\`), y abre `moss-gui.exe` como en G6: encuentra el `moss-rpc.exe` de su carpeta, respeta el mínimo de 720×520 y al cerrarla no deja ningún `moss-rpc.exe`. Lo llaman `ci.yml` (cada PR) y `release.yml`; sube `moss-windows-x64`.

### Firmas

Los binarios de Windows no están firmados y no se pedirá una firma: `moss-gui.exe`, `moss.exe`, `moss-rpc.exe` y `url_launcher_windows_plugin.dll` salen de nuestra compilación sin firma, y `flutter_windows.dll` (precompilado de Flutter) tampoco la trae. Solo las DLL de Visual C++ vienen firmadas por Microsoft. Por eso SmartScreen avisa y, con el Control Inteligente de Aplicaciones encendido, Moss no abre en Windows 11; qué hacer está en el `LEEME.txt` y en [instalar.md](instalar.md#windows).
