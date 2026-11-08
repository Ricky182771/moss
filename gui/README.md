# moss_gui

Interfaz gráfica de Moss en Flutter. Habla con `moss-rpc` como proceso hijo
(`rpc/PROTOCOL.md`); el diseño es `diseno/DISENO.md`.

## Requisitos

- Flutter 3.47.9 (estable), la misma que fija la CI.
- Dependencias de compilación de Linux:
  - Fedora: `sudo dnf install clang cmake ninja-build pkgconf-pkg-config gtk3-devel xz-devel`
  - Ubuntu: `sudo apt install clang cmake ninja-build pkg-config libgtk-3-dev liblzma-dev libstdc++-12-dev`
- `moss-rpc` compilado: `cmake --preset dev && cmake --build --preset dev` en la raíz.

## Correrla en desarrollo

La GUI busca `moss-rpc` junto a su ejecutable (nunca en el `PATH`). En
desarrollo, indícale la ruta con `--rpc`:

```bash
cd gui
flutter run -d linux --dart-entrypoint-args=--rpc=$PWD/../build/dev/cli/moss-rpc
```

Así usa tu configuración y tus keys reales. Para probar, usa
`tools/run-fake.sh` (abajo).

## Servidor falso (sin gastar)

`tools/fake-llm.py` (solo Python estándar) imita la API de OpenAI en
`http://127.0.0.1:8765/v1`: `/v1/models` y `/v1/chat/completions` con
streaming, con una respuesta con Markdown y un bloque de código.

- `--reasoning`: manda pensamiento (`reasoning_content`) antes del texto.
- `--slow`: 250 ms entre trozos (para probar Detener).
- `--status 429`: responde siempre con ese error (con `Retry-After: 5`;
  `moss-rpc` reintenta 3 veces, así que el error tarda unos 15 s en verse).

Para probar a mano, **usa siempre `tools/run-fake.sh`** (en la raíz del
repo). Quita de tu entorno todas las `CHAT_*`, exporta las de prueba y lleva
la configuración, los chats y las descargas a `/tmp/moss-prueba`, para **no
tocar tus chats, tu configuración ni tus keys**. Se niega a arrancar si alguna
de esas rutas no queda bajo `/tmp` o si `CHAT_BASE_URL` no es `127.0.0.1` o
`localhost`. Arranca `fake-llm.py` si no está corriendo (y lo detiene al
salir) y luego la GUI con `--rpc`:

```bash
tools/run-fake.sh --reasoning                  # flutter run (debug)
tools/run-fake.sh --perfil --frame-stats       # el bundle de profile
```

- `--reasoning`, `--slow` y `--status N` van a `fake-llm.py` (si ya corría,
  se ignoran con un aviso).
- `--frame-stats` va a la GUI.
- `--perfil` usa `build/linux/x64/profile/bundle/moss-gui`; compílalo antes
  con `flutter build linux --profile`.

Al cerrar la ventana revisa `pgrep -a moss-rpc`; si quedó alguno, el script
lo muestra y sale con 1.

**Tu bot real no se toca.** `HOME` y `XDG_*` aislados no bastan: `systemctl
--user` habla con tu sesión por D-Bus. Por eso la GUI arranca un envoltorio
(`/tmp/moss-prueba/moss-rpc`) que deja a `moss-rpc` sin
`DBUS_SESSION_BUS_ADDRESS` ni `XDG_RUNTIME_DIR` (solo a él: la GUI los
necesita para Wayland). Sin bus, `service.status` dice "No hay systemd de
usuario en esta sesión." y `service.apply` (al guardar Telegram) responde
`-32006`. `journalctl --user` no usa el bus (lee los archivos del journal) y
`loginctl` usa el del sistema, así que el envoltorio pone primero en el
`PATH` los falsos de `tools/fake-bin/`: Ver registro muestra un registro de
ejemplo (ayer y hoy, con errores, una línea fatal y una sin formato), nunca
el tuyo, y el linger siempre es "no".

## Medir cuadros (`--frame-stats`)

Solo en debug o profile (en release se ignora). Escribe en stderr el
renderizador (Impeller o Skia) y la frecuencia que reporta la pantalla, y
luego una línea por cada segundo con cuadros: cuántos, `build` y `raster`
(promedio y máximo) y el intervalo entre vsyncs (promedio, máximo y cuántos
pasan de 1.5× lo esperado). Un hueco de más de 250 ms es la app quieta y no
cuenta. Mide en `--profile`: debug es mucho más lento.

```bash
flutter run -d linux --profile \
  -a --rpc=$PWD/../build/dev/cli/moss-rpc -a --frame-stats
```

Añade `--no-enable-impeller` para usar Skia y `GDK_BACKEND=x11` delante
para XWayland.

## Qué hace (G2, G3 y G4)

- **Opciones** (engranaje a la izquierda de la caja): modelo y proveedor, y el interruptor de la búsqueda web (solo de esta sesión). `/model` sin modelo también la abre.
- **Buscar chats** (Ctrl+K): por título, sin distinguir mayúsculas ni acentos.
- **Menú `⋯`** de cada chat (también clic derecho y Shift+F10): Renombrar, Exportar y Borrar.
- **Menú de `/`**: al escribir `/` al principio de la caja.
- El texto del modelo aparece por palabra mientras llega.
- **Configuración** (F2 o la fila de abajo de la barra; Esc vuelve al chat):
  Proveedor (con tus endpoints), Colores y accesibilidad, Configuración del
  modelo, Búsqueda web y Addons · Telegram. Cada sección guarda su borrador
  hasta Guardar o Cancelar; salir con cambios pregunta antes de descartarlos.
- **Telegram** (solo Linux; en Windows la sección solo dice que el bot
  funciona en Linux): el estado del servicio al pie del menú; Servicio
  (instalación, estado, Reiniciar, Actualizar estado, Ver registro), Bot,
  Acceso (con Generar código), Límites e Invitados (Quitar, al momento).
  Ver registro abre las últimas 200 líneas del bot (hora y mensaje, por
  día, los errores con barra roja; Copiar copia las líneas completas).
  Guardar no reinicia el bot: después pregunta lo que haga falta (llaves que
  se descartarían, "¿Reiniciar ahora?", reemplazar un servicio hecho a mano)
  y aplica lo guardado al servicio.

### Dónde se guardan las preferencias de la GUI

Lo de Colores y accesibilidad (tema, tamaño del texto, Reducir animaciones y
Más contraste) va en el `config.json` de Moss, bajo la llave `"gui"`, con
`settings.gui.set` (`rpc/PROTOCOL.md` §6.7): sigue el modo portable y las
mismas rutas que lo demás. Es un objeto con `"version": 1`; si falta, es de
otra versión o trae algo que la GUI no conoce, se usan los valores
predeterminados. La GUI lo lee antes de dibujar el chat. Las keys nunca van
ahí: van en `credentials.json`, por `moss-rpc`.

## Pruebas

Ninguna necesita `moss-rpc` ni red (usan un canal en memoria):

```bash
cd gui
dart format --output=none --set-exit-if-changed .
flutter analyze
flutter test
```

### Capturas

La prueba marcada `capturas` no corre con `flutter test` ni en la CI
(`dart_test.yaml` la salta). A mano, con Geist cargada, guarda PNG de
1440×900 en oscuro y claro en `/tmp/moss-capturas/`:

```bash
cd gui
flutter test --tags capturas --run-skipped
```
