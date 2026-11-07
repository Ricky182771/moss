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

## Servidor falso (sin gastar)

`tools/fake-llm.py` (solo Python estándar) imita la API de OpenAI en
`http://127.0.0.1:8765/v1`: `/v1/models` y `/v1/chat/completions` con
streaming, con una respuesta con Markdown y un bloque de código.

- `--reasoning`: manda pensamiento (`reasoning_content`) antes del texto.
- `--slow`: 250 ms entre trozos (para probar Detener).
- `--status 429`: responde siempre con ese error (con `Retry-After: 5`;
  `moss-rpc` reintenta 3 veces, así que el error tarda unos 15 s en verse).

Las variables de abajo llevan la configuración, los chats y las descargas a
`/tmp/moss-prueba`, para **no tocar tus chats ni tu configuración**
(`CHAT_*` gana sobre `config.json`; `XDG_CONFIG_HOME` mueve `config.json` y
`credentials.json`; `CHAT_DATA_DIR` los chats; `CHAT_DOWNLOAD_DIR`
`/exportar` y `/guardar`):

```bash
# Terminal 1
python3 tools/fake-llm.py --reasoning

# Terminal 2, en la raíz del repo
mkdir -p /tmp/moss-prueba
cd gui && \
CHAT_BASE_URL=http://127.0.0.1:8765/v1 CHAT_API_KEY=prueba CHAT_MODEL=fake \
CHAT_DATA_DIR=/tmp/moss-prueba/chats CHAT_DOWNLOAD_DIR=/tmp/moss-prueba/descargas \
XDG_CONFIG_HOME=/tmp/moss-prueba/config \
flutter run -d linux --dart-entrypoint-args=--rpc=$PWD/../build/dev/cli/moss-rpc
```

Al cerrar la ventana, `pgrep -a moss-rpc` no debe mostrar nada.

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
