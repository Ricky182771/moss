# Guía: 11. Compilación y verificación
> Las reglas resumidas están en [AGENTS.md](../AGENTS.md).
## 11. Compilación y verificación

Dependencias en Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build libcurl-devel json-devel libasan libubsan libtsan
```

Ubuntu/Debian:

```bash
sudo apt install g++ cmake ninja-build libcurl4-openssl-dev nlohmann-json3-dev catch2
```

La interfaz usa [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v7.0.3, que CMake siempre descarga al configurar y le aplica un arreglo de upstream que todavía no sale en ninguna versión publicada ([#1351](https://github.com/ArthurSonzogni/FTXUI/pull/1351), en `cmake/patches/`): sin él, un pegado de 200 KB tarda 26 s en entrar. Por eso no usa la FTXUI que tenga instalada el sistema. Para el markdown usa [md4c](https://github.com/mity/md4c) v0.6.0, que CMake siempre descarga (los paquetes de las distribuciones son versiones anteriores, sin notas al pie). Por eso la primera vez necesitas conexión a internet y `git`, con el que CMake descarga las dos y aplica el parche ([04-stack.md](04-stack.md)).

Las pruebas necesitan Catch2 v3:

- Ubuntu 24.04 (trae la 3.4.0) y Debian 13 "trixie" (trae la 3.7.1): el paquete `catch2` de `apt` basta.
- Debian 12 "bookworm": su paquete `catch2` es la v2 (2.13.10), así que CMake descarga la v3 por FetchContent al configurar. Otra opción es instalar la 3.7.1 desde `bookworm-backports`.

La interfaz gráfica (Flutter 3.47.9) tiene sus propios requisitos y comandos en [gui/README.md](../gui/README.md).

Flags de warnings en todos los presets: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.

Presets (con su preset de build y de test del mismo nombre):

- `dev`: Debug con warnings.
- `asan`: Debug con AddressSanitizer + UndefinedBehaviorSanitizer.
- `tsan`: Debug con ThreadSanitizer (desde el hito 3: la interfaz usa un hilo de trabajo).
- `release`: Release (`-O3 -DNDEBUG`, el valor de CMake) con los mismos warnings y `-Werror`. Es la build para usar Moss a diario (`./build/release/cli/moss`): con conversaciones largas o al cambiar el tamaño de la terminal responde varias veces más rápido que la de desarrollo. Sus pruebas también deben pasar. Con `NDEBUG` no hay `assert`: ninguna prueba debe depender de ellos.
- Cada preset compila en `build/<preset>/` y se usa igual que `dev`, cambiando el nombre.
- En Linux, los cuatro compilan también el addon de Telegram (`CHATBOT_BUILD_TELEGRAM` encendida por defecto, sección 14), también `asan` y `tsan` en `build/asan` y `build/tsan`. Sin addons: `cmake --preset dev -B build/dev-off -DCHATBOT_BUILD_TELEGRAM=OFF`. Los presets `dev-telegram` y `release-telegram` ya no existen. `option(... ON)` no cambia una caché existente: una carpeta configurada con el addon apagado se borra (`rm -rf build/<preset>`) o se reconfigura una vez con `-DCHATBOT_BUILD_TELEGRAM=ON`; `build/dev-telegram/` y `build/release-telegram/` se pueden borrar.
- **Moss Code** (`CHATBOT_BUILD_CODE`, `ON` por defecto, también en Windows): compila `code/` (`chatbot_code`) y sus pruebas (`chatbot_code_tests`). Con `-DCHATBOT_BUILD_CODE=OFF`, `code/` no se compila y `moss code` sale con 69 ("Moss Code no está incluido en esta compilación."); lo demás sigue igual. `chatbot_process` se compila siempre. Las pruebas de `grep` corren sus dos implementaciones (`rg` y la búsqueda propia) si hay `rg` en el `PATH` (la CI de Linux instala ripgrep) y la de `rg` se salta con `SKIP` si no. La CI también compila `dev sin addons` con `CHATBOT_BUILD_CODE=OFF`.
- **Con poca memoria** (6 GB): `asan` compila con `cmake --build --preset asan -j2` y prueba con `ctest --preset asan -j1`; con más hilos el equipo se queda sin memoria.

`asan` y `tsan` ponen los sanitizadores también en `CMAKE_C_FLAGS`, para que md4c (C) quede instrumentado. Los flags de warnings solo se aplican a nuestros targets (`chatbot_set_warnings`), no a md4c ni a FTXUI.

Comandos de verificación:

```bash
cmake --preset dev  && cmake --build --preset dev  && ctest --preset dev  --output-on-failure
cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan --output-on-failure
cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
cmake --preset dev -B build/dev-off -DCHATBOT_BUILD_TELEGRAM=OFF && cmake --build build/dev-off && ctest --test-dir build/dev-off --output-on-failure
```

Las pruebas de `RequestRunner` y de `CancelToken` crean hilos (deben pasar con `tsan`). Aun así, corre también `./build/tsan/cli/moss` con una conversación real, cancelando un par de veces, y revisa que no aparezca `WARNING: ThreadSanitizer`: el loop de FTXUI no está en las pruebas.

La CI (`.github/workflows/ci.yml`, en cada PR y en cada push a `main`) corre en Ubuntu 24.04 estos mismos comandos con GCC en `dev`, `asan`, `tsan` y `release`, y con Clang en `dev` y `release`, todos con el addon (y revisa que `moss telegram` desactivado salga con 0 y sin token con 78); el trabajo `gcc / dev sin addons` compila y prueba `dev` con `-DCHATBOT_BUILD_TELEGRAM=OFF` y revisa que `moss telegram` salga con 69. Además, comprueba que el ejecutable sin `CHAT_API_KEY` salga con código 1 y que `moss-rpc` con `initialize`, `shutdown` y `exit` por stdin salga con 0 y diga `"ready":false`. El trabajo `paquetes de Linux` (`package-linux.yml`) arma los paquetes en Ubuntu 22.04, los instala en contenedores de Fedora, Debian 12 y Ubuntu 22.04 (y el `.tar.gz` en Debian 12) y los sube como `moss-linux-x86_64` ([17-empaquetado.md](17-empaquetado.md#ci)). En todos, los warnings cuentan como errores. **La CI nunca usa API keys ni llama a una API real.** En el trabajo `tsan` pone `vm.mmap_rnd_bits=28`, porque con 32 TSan falla al arrancar.

**Windows** (MSYS2 UCRT64, en la terminal "MSYS2 UCRT64"). Funciona en Windows 10 (versión 1903 o posterior; se prueba en Windows 10 IoT LTSC 21H2) y en Windows 11. Se compila con [MSYS2](https://www.msys2.org) y GCC (entorno UCRT64), no con Visual Studio:

1. Instala MSYS2 desde [msys2.org](https://www.msys2.org) (queda en `C:\msys64`).
2. Abre la terminal **MSYS2 UCRT64** (desde el menú Inicio) e instala los paquetes:

   ```bash
   pacman -S --needed git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,nlohmann-json,catch}
   ```

3. En esa misma terminal, clona el repositorio y compila con los mismos presets que en Linux (`asan` y `tsan` son solo de Linux):

   ```bash
   git clone https://github.com/Ricky182771/moss.git
   cd moss
   cmake --preset dev     && cmake --build --preset dev     && ctest --preset dev     --output-on-failure
   cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
   ```

   También puedes clonar con Git for Windows: `.gitattributes` fija los saltos de línea, así que no hace falta cambiar `core.autocrlf`.

Cómo correrlo en Windows (Windows Terminal, CMD o PowerShell; no en mintty) y qué cambia ahí: [uso.md](uso.md#en-windows). La interfaz gráfica se compila aparte, con Visual Studio ([gui/README.md](../gui/README.md)); el ZIP, con [`tools/package-windows.sh`](17-empaquetado.md#windows).

curl no viene de MSYS2: CMake la baja y la compila estática (Schannel, solo HTTP y HTTPS) y los `.exe` se enlazan con `-static`, así que no piden DLL de MSYS2 ([04-stack.md](04-stack.md)).

El trabajo `windows` de la CI (`windows-2025`, `msys2/setup-msys2` con `UCRT64`) corre esos dos presets, sin `CHATBOT_STRICT_PERF` (los runners de Windows varían; la latencia de cancelación < 2 s se exige igual), no toca `core.autocrlf` (prueba lo que obtiene quien clona con Git for Windows; los saltos de línea los fija `.gitattributes`: LF en todo, `.ans` sin conversión porque llevan secuencias de escape, `.cmd`/`.bat` en CRLF para CMD), revisa el arranque sin key con stdin en `NUL` y `timeout`, prueba `moss-rpc.exe` con pipes de verdad (`tools/check-rpc-pipes.sh` sobre una copia con `moss.portable`, para no tocar el AppData del runner: ciclo de vida, ningún `\r` en la salida, entrada con `\r\n`, UTF-8 de ida y vuelta con `settings.gui.*`, EOF en menos de 3 s y stdout solo del protocolo; también corre en Linux) y una conversación por HTTPS (`tools/check-rpc-https.ps1`: un certificado de `localhost` en las raíces de la máquina y `tools/fake-llm.py --tls`, que acepta hasta TLS 1.2 como Schannel en Windows 10; `initialize`, `chat.send` y `chat.finished` con `outcome: ok`, sin red ni keys). Las pruebas de permisos POSIX (0600, 0700, umask, "chmod 600") y de enlaces simbólicos no se compilan en Windows; ahí corren las de Windows (credenciales sin `chmod`, rutas con `\`).

El trabajo `gui-windows` (`windows-latest`, `subosito/flutter-action` con Flutter 3.47.9) corre `flutter analyze` y `flutter test` con MSVC (el formato lo revisa el trabajo `gui`). El release de Windows lo compila el trabajo `ZIP de Windows` (`package-windows.yml`, [17-empaquetado.md](17-empaquetado.md)): arma el ZIP oficial en `windows-2025` y lo prueba en otro runner sin MSYS2 con `tools/check-windows-zip.ps1`, en una ruta con espacios y acentos y solo Windows en el `PATH`: los recursos de versión de los tres `.exe`, `--version`, `moss-rpc.exe` (`initialize`, `shutdown` y `exit`) y `moss.exe` sin key, con y sin `moss.portable`; `moss-gui.exe` abre su ventana, arranca el `moss-rpc.exe` de su carpeta y crea `datos\`; la ventana se encoge hasta su mínimo (720×520 a la escala del runner); al cerrarla no queda ningún `moss-rpc.exe`. La escala de 125 % y 150 % y el cambio de monitor no se pueden probar en el runner: son a mano.

`ctest` corre `chatbot_code_tests` (`chatbot::code`: `ProjectRoot`, herramientas, instrucciones de sistema y el ciclo del agente, con transporte y procesos falsos; con `CHATBOT_BUILD_CODE`), `chatbot_addon_settings_tests` (`chatbot::addon_settings`; con `invitados.json` bajo `flock` con escrituras a la vez), `chatbot_tests` (núcleo y `chatbot_cli_lib`, sin FTXUI; los procesos de `service_control` con un `ProcessRunner` falso, salvo los de `run_process`, que lanzan `/bin/sh`, `sleep` y coreutils), `chatbot_markdown_tests` (solo `chatbot::markdown`, sin núcleo, `cli/` ni FTXUI) y `chatbot_ui_tests` (vista de markdown e historial y temas, dibujando con `ftxui::Screen`, sin terminal; F2 → Addons con un `ProcessRunner` falso y archivos en una carpeta temporal, también a 80×24); `chatbot_include_rules` (`tests/include_rules.cmake` con `cmake -P`, también con los addons apagados: fuera de `addons/` nadie incluye `chatbot/bot/` ni `chatbot/telegram/`, solo `cli/`, `rpc/`, `tools/` y `tests/` incluyen `chatbot/addon_settings/`, `addons/settings/` no incluye `chatbot/bot/` ni `chatbot/telegram/`, `markdown/` y `addons/` no incluyen FTXUI ni headers de `cli/src`, y `rpc/` no incluye FTXUI) y `chatbot_include_rules_check` (que falle con un `#include` prohibido); `chatbot_addon_launcher` (`tests/addon_launcher.cmake`: `moss <addon>` con el ejecutable real y un addon falso); `chatbot_rpc_tests` (puras, el `Dispatcher` con `session_fakes.hpp` y un reloj falso, y en POSIX `moss-rpc` real con pipes y un entorno limpio); con un addon encendido, `chatbot_bot_tests` y, con `CHATBOT_BUILD_TELEGRAM`, `chatbot_telegram_tests` (etiqueta `addons`; sin red: un Telegram falso y el modelo falso de `bot_harness.hpp`); y `tests/portable_startup.cmake` con el `moss` real (`chatbot_portable_writable` y, solo POSIX, `chatbot_portable_readonly`, que imprime `SKIP:` como root). En POSIX, `chatbot_ui_tests` también trae `test_paste_pty.cpp`: abre una pty (`posix_openpt`), arranca `chatbot_paste_probe` con la esclava como terminal de control, espera su primer cuadro (ya en modo crudo) y le escribe 200 KB entre corchetes mientras vacía la salida; exige que lleguen completos y en un solo pegado (`\r` → `\n`) y, con `CHATBOT_STRICT_PERF=1`, en menos de 2 s (sin el parche de FTXUI tarda 26 s, sea cual sea el equipo: 128 bytes por vuelta a 60 vueltas por segundo). Otra prueba abre un pegado sin `201~`, espera 3 × `kPasteIdleLimit` y manda una tecla: el pegado debe cerrarse (falla si la app deja de dibujar cuadros con el pegado abierto). `tests/data/markdown_muestra_terminal_w40.ans` y `_w80.ans` son `Screen::ToString()` del render con el tema "De la terminal" (24 bits); si cambia el render a propósito, se regeneran con la salida de `md_preview --theme terminal` en una terminal de 24 bits, quitando el `\r` extra que agrega la pty y el salto de línea final. Todas las pruebas de tiempo miden y reportan siempre con `WARN`. Las de latencia de cancelación (`CancelToken`, `RealSleeper`, `RequestRunner`, incluido su destructor, el apagado y el "Detener" de `chatbot::bot::Bot`, y en Telegram el corte del long polling, "Detener" desde un update y el apagado con varios chats; `report_latency` en `tests/performance.hpp`) exigen **< 2 s en todos los presets**, sin depender de `CHATBOT_STRICT_PERF`: miden la respuesta a la cancelación, no la velocidad del equipo. Los límites de las demás (parseo, reparto de tablas, historial) solo fallan con `CHATBOT_STRICT_PERF=1` (por ejemplo, `CHATBOT_STRICT_PERF=1 ctest --preset release --verbose`), que la CI activa únicamente en release (GCC y Clang). Las esperas máximas entre hilos y la comprobación de no terminar una espera antes de tiempo son verificaciones funcionales y siguen siendo obligatorias. Para revisar el render a ojo: `./build/dev/tools/md_preview --width 40 tests/data/markdown_muestra.md`.

`tools/fake-llm.py` acepta `--script` (guion de respuestas, con llamadas a herramientas), `--log-requests` (guarda las peticiones) y `--require-reasoning-replay` (exige el pensamiento de regreso); formato en `tests/data/fake-llm/README.md`.

`moss-tools-probe` (`tools/tools_probe.cpp`) es la sonda de tool calling de Moss Code ([18-moss-code.md](18-moss-code.md), 3.6): una herramienta a mano para elegir modelos, **no** una prueba de `ctest`, ni se instala ni va en los paquetes. Usa la configuración de la app (proveedor vigente, key guardada o `CHAT_API_KEY`) sin escribir nada y nunca imprime la key. `moss-tools-probe --modelos` lista los modelos del proveedor; `moss-tools-probe <modelo> [<modelo>...]` corre, por modelo, los cuatro escenarios con `read` y `glob` falsos en memoria (llamada simple, dos pasos, varias llamadas, recuperación de un error) y el 2 otra vez sin mandar el pensamiento de regreso, y escribe una tabla: resultado, pasos, tiempo, si mandó pensamiento (y con qué llave) y si lo exige de regreso. Sale con 0 si corrió (aunque los modelos fallen), 1 por error de configuración y 2 por uso. Sin API real, contra `fake-llm.py` y con el entorno de `tools/run-fake.sh` (sin tus `CHAT_*`; carpetas bajo `/tmp`):

```bash
python3 tools/fake-llm.py --port 8765 --script tests/data/fake-llm/sonda-con-index.json &   # o sonda-sin-index / sonda-un-pedazo / sonda-texto
env $(compgen -e | grep '^CHAT_' | sed 's/^/-u /') CHAT_BASE_URL=http://127.0.0.1:8765/v1 CHAT_API_KEY=prueba \
    CHAT_DATA_DIR=/tmp/moss-sonda XDG_CONFIG_HOME=/tmp/moss-sonda/config \
    ./build/dev/tools/moss-tools-probe fake        # el guion es para un solo modelo; reinicia el servidor entre corridas
```

`tools/check-tools-probe.sh <moss-tools-probe>` hace esa corrida de punta a punta (con cada guion, `--log-requests y `--require-reasoning-replay`) y falla si algún escenario no sale "bien", si no detecta que se exige el pensamiento de regreso o si las peticiones no traen lo que dice la guía (`tools`, `tool_choice`, `tool_call_id`, `reasoning_content`, sin `parallel_tool_calls` ni key); es un paso de la CI en Linux. No hay paso en Windows: el script es de bash y corre en el shell de MSYS2, que se instala sin Python (el Python del runner solo se ve desde pwsh, como en `tools/check-rpc-https.ps1`). Por defecto corre los tres guiones (`sonda-con-index`, `sonda-sin-index`, `sonda-un-pedazo`); `CHECK_PROBE_SCRIPT` corre solo uno.

`tools/check-code-e2e.py <moss>` es la punta a punta de `moss code -p` (Python estándar, `python3 -I` en Linux y `python -I` desde pwsh en Windows): por cada guion `tests/data/fake-llm/code-*.json` (`tres-pasos`, `grep` y `limite`, este con `code.max_steps` 2) arranca `fake-llm.py` en un puerto libre, corre `moss code tests/data/code-project -p` con un entorno aislado (sin tus `CHAT_*`, una copia de `moss` en modo portable con su `datos/config.json`, sin proxy) y comprueba el código de salida, que stdout sea solo la respuesta final, las líneas `●` de stderr y las peticiones recibidas (las cuatro herramientas, el `AGENTS.md` en el `system`, los resultados de las herramientas, `reasoning_content` y ni cabecera `Authorization` ni key). Es un paso de la CI en Linux (después de las pruebas) y en Windows (`shell: pwsh`), no en "dev sin addons".

Con la key real, a mano y con tu permiso: `CHAT_API_KEY=... ./build/dev/tools/moss-tools-probe <modelo>` (cada modelo gasta unas 13 peticiones).

