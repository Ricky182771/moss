# 16. Interfaz gráfica (Flutter)

La GUI (`gui/`, proyecto `moss_gui`, ejecutable `moss-gui`) es una app de Flutter para Linux y Windows. No contiene lógica del núcleo: arranca `moss-rpc` como proceso hijo y le habla por JSON-RPC (`rpc/PROTOCOL.md`). El diseño manda: `gui/diseno/DISENO.md` y luego las maquetas de `gui/diseno/maquetas/`.

Requisitos, comandos y el servidor falso: `gui/README.md`.

## Carpetas de `lib/`

| Carpeta | Qué tiene |
|---|---|
| `main.dart` | `MossApp` (tema según el sistema) y `Boot`: busca y arranca `moss-rpc`, llama `initialize`, y muestra el chat o la pantalla de error. Cierra con `AppLifecycleListener.onExitRequested`. |
| `rpc/` | `client.dart`: `locateRpc` (`--rpc=` o junto al ejecutable, nunca el `PATH`), `RpcClient` (json_rpc_2 sobre las líneas del proceso; `initialize` con tiempo límite de 10 s; `close`: `shutdown`, `exit` y `kill` a los 3 s), `StderrBuffer` (últimas 200 líneas). `errors.dart`: `RpcError` con `code`, `message`, `data`; se muestra `data.message`. |
| `models/` | Copias de los tipos del protocolo: `conversation.dart` (`Entry`, `ConversationSnapshot`, `Phase`), `summary.dart` (fila de `conversations.list`), `events.dart` (`RpcEvent.parse` y un tipo por evento). Solo `fromJson`, sin lógica. |
| `state/` | `apply.dart`: `applyEvent`, función pura que aplica un evento al snapshot o devuelve `null` si hay hueco de `rev`. `app_state.dart`: `AppState` (`ChangeNotifier`), la lista de chats, la conversación abierta, el aviso corto, el error junto a la caja, `restoreText` y las acciones (`submit`, `cancel`, `newChat`, `open`, `export`, `retry`). |
| `screens/` | Pantallas completas: `chat_screen.dart` (barra lateral, encabezado, mensajes, caja, atajos) y `error_screen.dart` (`moss-rpc` no está, no arrancó, no respondió o es de otra versión). |
| `widgets/` | Piezas: `sidebar`, `message_list` (pegado abajo solo si ya estaba abajo), `entry_view` (cada tipo de entrada y el Pensamiento), `markdown_view` (Markdown, bloque de código con Copiar, sin imágenes), `input_box`, `toast`, `indicators` (cursor y "Pensando…"), `controls` (`Pressable`, `IconAction` con etiqueta y `Semantics`), `logo`. |
| `theme/` | `colors.dart` (`MossColors`, todos los tokens en oscuro y claro), `theme.dart` (`MossText` con los estilos de texto y `MossTheme`), `motion.dart` (todas las duraciones y curvas; 0 ms con `disableAnimations`). |
| `strings.dart` | Todos los textos propios de la GUI. Los que vienen de `moss-rpc` se muestran tal cual. |

## Del evento a la pantalla

1. `moss-rpc` escribe una línea en stdout; `RpcClient` la pasa a `json_rpc_2`.
2. Si es una notificación conocida (`RpcEvent.methods`), `AppState` la recibe ya convertida por `RpcEvent.parse`. Las desconocidas caen en el `registerFallback` y se ignoran.
3. `AppState._onEvent`:
   - `conversations.changed`: vuelve a pedir `conversations.list`.
   - Evento de conversación: `applyEvent(snapshot, evento)`. Si devuelve `null` (falta un `rev` o el índice no cuadra), pide `conversation.get` y aplica después los eventos que lleguen mientras.
   - `chat.finished` con `restore_text`: lo pone en `restoreText` y la caja lo recupera si está vacía.
4. `notifyListeners()`: el `ListenableBuilder` de `ChatScreen` reconstruye la barra y los mensajes. Los avisos y errores de la caja son `ValueNotifier` aparte, para no redibujar todo.
5. Si el proceso se cae, `RpcClient.done` marca `disconnected` y sale el aviso con Reintentar: arranca otro `moss-rpc` y reabre la conversación si tenía `id`.

Las peticiones van al revés: el widget llama a un método de `AppState`, que llama a `RpcClient.request`. Un `RpcError` se muestra junto a la caja (Busy, sin configurar, validación) o como aviso corto (lo demás).

## Cómo agregar una pantalla

1. Revisa la pantalla en `DISENO.md` y su maqueta. Si falta algo del servidor, se agrega al protocolo en su propio PR (documentado en `rpc/PROTOCOL.md`); no se copia la regla del núcleo a Dart.
2. Crea `lib/screens/<nombre>_screen.dart` con un widget que reciba `AppState`.
   - Colores con `context.colors`, estilos con `context.styles`, duraciones con `Motion.of(context)`. Nada de colores, tamaños de letra ni duraciones escritos a mano.
   - Textos nuevos en `Strings`.
   - Botones de solo ícono con `IconAction` (etiqueta al pasar el mouse y `Semantics`).
   - Lo que no cabe se desplaza en su propia zona; el esqueleto (barra, encabezado, caja) no se mueve.
3. Las llamadas al servidor van en métodos de `AppState` (o de un `ChangeNotifier` propio de la pantalla si su estado es solo de ella), nunca dentro del widget.
4. Para cambiar de pantalla, el que la contiene elige qué mostrar con un fundido de `Motion.of(context).screenFade` (`DISENO.md`: Chat ↔ Configuración, sin deslizar).
5. Pruebas en `test/` con `startApp` de `test/harness.dart` y el `FakeServer` en memoria (sin `moss-rpc` ni red). Si la pantalla tiene maqueta, agrega su captura a `test/capturas_test.dart`.
6. Verifica con la receta "gui" de `test-runner` (`dart format`, `flutter analyze`, `flutter test`).

## Deuda conocida

- `thoughtSummary` (`state/apply.dart`) copia la cuenta de palabras de la TUI (`reasoning_summary`). Lo correcto es que el servidor mande el resumen; necesita un campo nuevo en el protocolo.
- Resaltado de sintaxis en los bloques de código: la maqueta pinta las palabras clave con el token `code`, pero `DISENO.md` no lo pide; queda para después.
