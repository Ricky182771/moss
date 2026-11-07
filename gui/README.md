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
