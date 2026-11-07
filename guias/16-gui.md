# 16. Interfaz gráfica (Flutter)

La GUI (`gui/`, proyecto `moss_gui`, ejecutable `moss-gui`) es una app de Flutter para Linux y Windows. No contiene lógica del núcleo: arranca `moss-rpc` como proceso hijo y le habla por JSON-RPC (`rpc/PROTOCOL.md`). El diseño manda: `gui/diseno/DISENO.md` y luego las maquetas de `gui/diseno/maquetas/`.

Requisitos, comandos y el servidor falso: `gui/README.md`.

## Carpetas de `lib/`

| Carpeta | Qué tiene |
|---|---|
| `main.dart` | `MossApp`, `PrefsApp` (la `MaterialApp` con lo de Colores y accesibilidad: tema, Más contraste, `TextScaler` y Reducir animaciones sumada a `disableAnimations`) y `Boot`: busca y arranca `moss-rpc`, llama `initialize`, lee `settings.gui` antes del primer cuadro del chat, y muestra el chat o la pantalla de error. Cierra con `AppLifecycleListener.onExitRequested`. |
| `rpc/` | `client.dart`: `locateRpc` (`--rpc=` o junto al ejecutable, nunca el `PATH`), `RpcClient` (json_rpc_2 sobre las líneas del proceso; `initialize` con tiempo límite de 10 s; `close`: `shutdown`, `exit` y `kill` a los 3 s), `StderrBuffer` (últimas 200 líneas). `errors.dart`: `RpcError` con `code`, `message`, `data`; se muestra `data.message`. |
| `models/` | `gui_prefs.dart`: las preferencias de la GUI (`version: 1`; lo desconocido usa el predeterminado). Copias de los tipos del protocolo: `conversation.dart` (`Entry`, `ConversationSnapshot`, `Phase`), `summary.dart` (fila de `conversations.list`), `events.dart` (`RpcEvent.parse` y un tipo por evento). Solo `fromJson`, sin lógica. |
| `state/` | `apply.dart`: `applyEvent`, función pura que aplica un evento al snapshot o devuelve `null` si hay hueco de `rev`. `app_state.dart`: `AppState` (`ChangeNotifier`), la lista de chats, la conversación abierta, `ready`/`problem` de `app.status`, el aviso corto, el error junto a la caja, `restoreText`, `searchOn` (interruptor de la búsqueda, solo de la sesión), `openOptions` (`/model` sin modelo) y las acciones (`submit`, `cancel`, `newChat`, `open`, `export`, `rename`, `delete`, `commands`, `retry`). `options_state.dart`: el estado de la ventana de Opciones mientras está abierta. `settings_state.dart`: Configuración (`SettingsState`, `SettingsSection`, `SaveStep`, `SaveOrder`) y una sección por archivo: `provider_section.dart`, `colors_section.dart`, `model_section.dart`, `search_section.dart`. Reglas puras de la interfaz: `title_search.dart` (Buscar chats sin acentos), `slash_menu.dart` (menú de `/`), `arriving.dart` (hasta dónde se muestra el texto que llega). |
| `screens/` | Pantallas completas: `home_screen.dart` (el chat y, encima, Configuración con un fundido), `chat_screen.dart` (barra lateral, encabezado, mensajes, caja, atajos), `settings_screen.dart` (menú de secciones, panel y barra Cancelar / Guardar) y `error_screen.dart` (`moss-rpc` no está, no arrancó, no respondió o es de otra versión). |
| `widgets/` | Piezas: `sidebar` (con la fila de cada chat y su menú `⋯`), `message_list` (pegado abajo solo si ya estaba abajo), `entry_view` (cada tipo de entrada y el Pensamiento), `markdown_view` (Markdown, bloque de código con Copiar, sin imágenes), `arriving_text` (texto que llega), `input_box` (engranaje, pastilla, menú de `/`), `options_panel`, `search_dialog`, `delete_dialog`, `popover` (ventanas flotantes ancladas), `list_keys` (↑ ↓ Enter con el foco en un campo), `select` (reconstruir solo si cambia lo elegido), `settings_form` (tarjeta, campos, `SecretField` para las keys, casillas y opciones de Configuración), un panel por sección (`provider_panel`, `colors_panel`, `model_panel`, `search_panel`), `toast`, `indicators` (cursor y "Pensando…"), `controls` (`Pressable`, `IconAction` con etiqueta y `Semantics`), `logo`. |
| `theme/` | `colors.dart` (`MossColors`, todos los tokens en oscuro y claro, y `darkHigh`/`lightHigh` de Más contraste con el grosor del foco), `theme.dart` (`MossText` con los estilos de texto y `MossTheme`), `motion.dart` (todas las duraciones y curvas; 0 ms con `disableAnimations`). |
| `strings.dart` | Todos los textos propios de la GUI. Los que vienen de `moss-rpc` se muestran tal cual. |

## Del evento a la pantalla

1. `moss-rpc` escribe una línea en stdout; `RpcClient` la pasa a `json_rpc_2`.
2. Si es una notificación conocida (`RpcEvent.methods`), `AppState` la recibe ya convertida por `RpcEvent.parse`. Las desconocidas caen en el `registerFallback` y se ignoran.
3. `AppState._onEvent`:
   - `conversations.changed`: vuelve a pedir `conversations.list`.
   - Evento de conversación: `applyEvent(snapshot, evento)`. Si devuelve `null` (falta un `rev` o el índice no cuadra), pide `conversation.get` y aplica después los eventos que lleguen mientras.
   - `chat.finished` con `restore_text`: lo pone en `restoreText` y la caja lo recupera si está vacía.
4. `notifyListeners()`. Cada parte escucha solo lo suyo con `Select` (`widgets/select.dart`): la barra, la lista y el chat abierto; el encabezado, el título; la caja, la fase; los mensajes, la conversación. `MessageList` reusa el widget de cada entrada que `applyEvent` dejó idéntica, así un `entry_delta` reconstruye solo la última (`test/rebuilds_test.dart` lo cuenta). Los avisos y errores de la caja son `ValueNotifier` aparte.
5. Si el proceso se cae, `RpcClient.done` marca `disconnected` y sale el aviso con Reintentar: arranca otro `moss-rpc` y reabre la conversación si tenía `id`.

Las peticiones van al revés: el widget llama a un método de `AppState`, que llama a `RpcClient.request`. Un `RpcError` se muestra junto a la caja (Busy, sin configurar, validación) o como aviso corto (lo demás).

## Dónde vive cada menú

Las ventanas que flotan ancladas usan `Popover` (`widgets/popover.dart`): `OverlayPortal` con un `LayerLink`, opacidad y escala 0.98 → 1 desde la esquina (140 ms; cierra en 100). Con `onDismiss`, Esc y el clic afuera la cierran y el foco vuelve a `returnFocus` si seguía adentro.

| Menú | Dónde | Notas |
|---|---|---|
| Opciones | `input_box.dart` (engranaje) → `options_panel.dart`, estado en `state/options_state.dart` | Anclada a la caja. El foco empieza en el filtro; ↑ ↓ y Enter con `listKeys`. `models.list` con `RpcClient.start` para poder mandar `$/cancel`. |
| Menú de `/` | `input_box.dart` (`_SlashMenu`) | Otro `Popover` anclado a la caja, sin barrera ni foco propio: el foco se queda en la caja y las teclas pasan por `_onKey`. `commands.list` al abrirse. |
| Menú `⋯` | `sidebar.dart` (`_Row`, `_ChatMenu`) | Anclado al `⋯`. Renombrar convierte la fila en un campo; Borrar abre `delete_dialog.dart`. |
| Buscar chats | `search_dialog.dart` | Una ruta (`showGeneralDialog`): fondo oscurecido y el foco vuelve solo al cerrar. |

## Texto del modelo llegando

`DISENO.md` → Animaciones. Dos piezas:

1. `state/arriving.dart` (`shownLength`): mientras llega, se muestra hasta la última palabra completa; dentro de un bloque de código, hasta la última línea completa. Una palabra cortada por un trozo espera al siguiente.
2. `widgets/arriving_text.dart` (`ArrivingText`): envuelve el Markdown de la respuesta y del Pensamiento. Cada vez que crece el texto que se muestra es un grupo. Después de pintar el Markdown, busca los `RenderParagraph` en orden, ubica el texto nuevo por su lugar en el texto dibujado y pinta encima rectángulos del color de lo que hay detrás (el chat o el bloque de código), cada vez más transparentes durante `Motion.word` (120 ms). No toca el Markdown y no reconstruye por cuadro: solo repinta. Como mucho 40 palabras animándose; una ráfaga más grande aparece directo. `NotArriving` marca lo que no cuenta (la cabecera de un bloque de código).

Al terminar la respuesta (o con Reducir animaciones) `ArrivingText` se apaga sin cambiar el árbol, así el Markdown no se vuelve a montar. Al reabrir un chat o con un snapshot, nada se anima.

`flutter_markdown_plus` vuelve a parsear todo su texto cuando cambia: el mensaje que llega se re-parsea entero en cada trozo (~cada 30 ms). Se decidió medirlo con `--frame-stats` antes de cambiarlo. Si pesa, la propuesta es que `moss-rpc` mande dónde terminan los bloques cerrados (ya los calcula `chatbot::markdown`) para cachearlos, en su propio PR de protocolo.

## Avisos de `/copiar`, `/exportar` y `/model`

Los agrega el servidor al historial (`Session::report`, `PROTOCOL.md` §6.5) y llegan como entrada `notice`, igual que en la TUI. Decidido en G2: se quedan así. Los botones (Exportar del encabezado y del menú `⋯`, Copiar del bloque) usan el aviso corto.

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

## Configuración

`DISENO.md` → Pantallas → Configuración. `AppState.settingsOpen` dice qué sección se ve (null en el chat); `HomeScreen` deja el chat montado debajo (sin foco ni semántica) y funde `SettingsScreen` encima. Al salir, el foco regresa a donde estaba en el chat.

**Borradores.** `SettingsScreen` crea un `SettingsState` al abrirse y lo descarta al salir. Cada sección (`SettingsSection`) lee lo guardado con su `*.get` (`load`) y lleva su borrador (campos, `TextEditingController`); `dirty` lo compara con lo guardado y pone la marca en el menú. Cambiar de sección no pierde nada; Cancelar llama a `discard` en todas. Colores es especial: su borrador es `AppState.prefs`, que `PrefsApp` aplica al momento (vista previa en vivo); Cancelar y Descartar lo regresan a lo guardado antes de desmontar.

**Checks.** Mientras se escribe, cada sección manda su `*.check` con un retraso (`kCheckDelay`, 400 ms después de la última tecla) y pone lo que regresa junto al campo (`field_errors`, `extra_error`, `error`, `hint`, la vista previa, los bytes). Nada de eso se calcula en Dart, salvo convertir lo escrito a número para mandarlo como número (y marcar lo que no se puede convertir; ver la deuda).

**Guardar** (`SettingsState.save`):
1. Los `check` de las secciones con cambios, en paralelo. Un error: no se guarda nada, el panel va a la primera sección que lo tiene y cada error se queda junto a su campo. Un error del propio `check` (Busy, no se pudo leer) va en la barra.
2. Los guardados (`SaveStep`) en `SaveOrder`, el de la F2 de la TUI: ajustes del modelo, proveedor, Colores (`settings.gui.set`), instrucciones, búsqueda. Los ajustes van antes que el proveedor porque `model_params.save` aplica al modelo vigente.
3. A la primera falla se detiene: lo anterior queda guardado (no hay vuelta atrás, como en la TUI), esa sección y las siguientes conservan su borrador y su marca, el panel va a la que falló y el error queda junto al campo (`showSaveError`) o, si no es de un campo (Busy, escritura), en la barra.
4. Al final, `app.status`: como los avisos del historial llegan después de la respuesta del guardado, con esa respuesta ya llegaron; los `notice` nuevos salen como aviso corto.

**Eventos.** `settings.changed` llega a `SettingsState` por `AppState.settingsEvents`. Cada sección dice qué secciones del protocolo la tocan (`events`): sin cambios se vuelve a pedir; con cambios no se pisa y sale el aviso discreto "Esto se cambió fuera de aquí…". Los eventos de nuestros guardados (y de acciones al momento como Borrar endpoint, con `SettingsState.quietly`) se ignoran y al terminar se recargan las secciones sin cambios.

**Keys.** `SecretField`: tapada, con el botón de ver (se apaga al salir del campo y del botón), sin menú contextual ni Ctrl+C / Ctrl+X / Ctrl+Insert / Shift+Supr, sin autocorrector ni sugerencias. Vacía = se conserva la guardada (solo se manda `api_key` si se escribió algo). El controlador se vacía al guardar, al descartar, al cambiar de proveedor y al cerrar. El estado (`guardada: …abcd`) lo da el servidor enmascarado.

**Fijados por `CHAT_*`.** Los campos que fija una variable (`locks` de `settings.provider.get`, `locked` de `settings.search.get`) van deshabilitados con "Fijado por CHAT_…".

### Cómo agregar una sección (G4)

1. Un valor nuevo en `SettingsPage` (en el lugar del menú que diga la maqueta).
2. `lib/state/<nombre>_section.dart`: una clase que extienda `SettingsSection` con `events`, `dirty`, `load`, `discard`, `check` (si el protocolo tiene uno), `steps` (con un `SaveOrder` nuevo después de los que hay, salvo que la TUI diga otro orden) y, si un error de guardar es de un campo, `showSaveError`.
3. `lib/widgets/<nombre>_panel.dart` con las piezas de `settings_form.dart` (`SettingsCard`, `LabeledField`, `SecretField`, `CheckRow`, `Segmented`, `FieldMessage`).
4. En `settings_screen.dart`, la sección en `_sections` y su `SettingsPanel` en `_panel`.
5. Textos en `Strings`; pruebas con `startApp` y los manejadores del `FakeServer` (`test/provider_test.dart` es el ejemplo más completo); capturas en `test/capturas_test.dart`.

Una acción que no espera a Guardar (como Borrar endpoint) va dentro de `owner.quietly(...)` para que su `settings.changed` no cuente como de afuera.

## Deuda conocida

- `thoughtSummary` (`state/apply.dart`) copia la cuenta de palabras de la TUI (`reasoning_summary`). Lo correcto es que el servidor mande el resumen; necesita un campo nuevo en el protocolo.
- Resaltado de sintaxis en los bloques de código: la maqueta pinta las palabras clave con el token `code`, pero `DISENO.md` no lo pide; queda para después.
- `RpcClient` lleva la cuenta de los `id` porque `json_rpc_2` 3.x no deja elegirlos (lo vigila `test/client_test.dart`); la 4.x tiene `idGenerator`.
- El mensaje que llega se re-parsea entero en cada trozo (ver "Texto del modelo llegando").
- La explicación del razonamiento en Configuración del modelo (`Strings.reasoningHelp`) copia los textos de la TUI por `reasoning_style`, sin el caso de OpenRouter. Lo correcto es que `settings.model_params.get` mande el texto.
- `ModelSection._parse` decide qué texto es un número (coma decimal, `−`, enteros según `limits.integer`) con los mensajes de la TUI; `Strings.topKNote` y `Strings.extraHelp` repiten reglas del núcleo. Lo correcto es que `settings.model_params.check` acepte el texto de cada campo y que `get` mande las notas.
- `settings.provider.check` da un solo `error` sin decir de qué campo: va al final de la tarjeta del elegido. `key_status` es solo del proveedor guardado; para los demás, la GUI dice "key guardada" con `key_saved`.
- `PrefsApp` aplana el `TextScaler` del sistema a lineal (en escritorio siempre lo es).
- `title_search.dart` pliega solo los acentos del español y lenguas cercanas; `arriving.dart` detecta los bloques de código solo por las cercas al principio de línea. Las dos están marcadas con `shortcut:`.
