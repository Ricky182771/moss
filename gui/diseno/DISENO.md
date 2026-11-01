# Diseño de Moss (identidad y GUI) — 1 nov 2026

> **Estado (1 nov 2026): cerrado para empezar la GUI (G1).** Las maquetas ya tienen todo lo que cambió en la base (`claude/diseno-base-pre-gui.md` §8). En el repo, este archivo va como `gui/diseno/DISENO.md` y las maquetas en `gui/diseno/maquetas/`; **si una maqueta y este archivo no coinciden, manda este archivo.**

Canvas: https://claude.ai/artifact/KbdTnY9KBk22pqiG8kYiGY

Fuentes del borrador en el proyecto: `claude/gui-borrador/` (copia vieja; el canvas manda). Mesas: `Main` (paleta y estilo), `Logos`, `Chat-oscuro`, `Chat-claro`, `Chat-vacio` (Chat nuevo), `Buscar` (Buscar chats), `Config-proveedor`, `Config-colores`, `Config-modelo`, `Config-busqueda`, `Config-addons`, `Config-registro` (Telegram · Ver registro). Todas las pantallas miden 1440×900, como una ventana real.

## Identidad
- Nombre: **Moss**. Se escribe `moss_` en el logotipo, en Geist Mono, con el `_` en verde.
- Logo: concepto **C · Cursor**, una "m" de dos arcos con el cursor de bloque de terminal a la derecha (SVG 64×64: `M8 48V30a9 9 0 0 1 18 0v18M26 30a9 9 0 0 1 18 0v18`, trazo 6–7, puntas redondas, más `rect x=49 y=39–40 w=9–10 h=9–10 rx=1.5`).
- Ejecutables: `moss`, `moss-rpc`, `moss-telegram`.

## Tipografía
- Interfaz: **Geist** (400/500/600). Código, terminal, modelos, keys: **Geist Mono** (400/500). De Vercel, licencia SIL Open Font License 1.1.
- **La app de Flutter empaqueta los archivos de las dos fuentes** (no depende del sistema ni de internet; sin ellas Flutter usaría Noto Sans/Cantarell en Fedora y Segoe UI en Windows). Su licencia va en `AVISOS-TERCEROS.txt` del ZIP.

## Colores
Escala moss: 50 #EEF4E6 · 100 #D6E6C3 · 200 #B5D395 · 400 #8DBF5F · 500 #6FA344 · 600 #56853A · 700 #3F6B2A · 800 #2F5221 · 900 #213A18

| Token | Oscuro | Claro |
|---|---|---|
| fondo (chat) | #0F1411 | #FFFFFF |
| barra lateral / tarjeta | #161D18 | #F6F8F3 |
| campo / código | #1E2721 | #EDF1E8 |
| borde | #2A352D | #D5DDD0 |
| texto | #E6EDE7 | #17201A |
| texto tenue | #9AA89E | #56645A |
| acento | #8DBF5F | #3F6B2A |
| texto sobre acento | #0F1411 | #FFFFFF |
| acento suave (globo del usuario, elemento activo, hover) | #22301F | #E3ECD9 |
| código / palabras clave | #B5D395 | #2F5221 |
| error | #E8836B | #B3402A |

Todos los pares de texto pasan 4.5:1.

## Principios (1 nov 2026)
- **No reacomodar toda la interfaz si no hace falta.** La ventana tiene un esqueleto fijo: barra lateral, encabezado, caja del mensaje y barra de Cancelar/Guardar se quedan en su lugar. Cuando el contenido no cabe, **se desplaza solo la zona que lo tiene** (los mensajes, la lista de chats, el panel de una sección de Configuración), con su propia barra de desplazamiento; la ventana nunca se estira ni reparte el contenido en toda el área. Lo mismo para cosas que aparecen o desaparecen (una pastilla, un aviso): ocupan su lugar sin mover lo demás de sitio, salvo cuando lo justifican (la caja crece a dos filas con la búsqueda activa).
- **Minimalista:** el verde lleno se reserva para lo que de verdad hay que hacer (Guardar en Configuración, "Ir a Configuración"). Botones de la barra y de la caja: sin fondo, con el acento suave al pasar el mouse.
- **Botones de solo ícono** llevan `aria-label` y una etiqueta corta que aparece al pasar el mouse o con el foco (Opciones, Detener, Enviar).
- **Avisos cortos** ("Conversación exportada a…", "Copiado"): una notificación chica abajo al centro, encima de la caja del mensaje, que se va sola a los 4 s; no mueve nada. Los errores que piden algo (Busy, un título inválido) van junto a lo que los causó.
- **"Chat/chats" en la GUI**, en lo que es de la propia GUI (barra, encabezado, ventana de búsqueda). Los avisos que vienen del núcleo por `moss-rpc` ("Conversación exportada a…", etc.) se muestran tal cual; la base no cambia (decidido el 1 nov 2026).

## Pantallas
- **Chat:**
  - Barra lateral: logo; **Nuevo chat** (Ctrl+N) y **Buscar chats** (Ctrl+K), filas sin fondo; chats por fecha con un globo si usaron búsqueda (la lista se desplaza sola); Configuración (F2) abajo, fija. Sin el estado de Telegram.
  - **Menú `⋯` por chat:** el botón `⋯` (32 px, solo ícono, "Más opciones de «título»") aparece en la fila al pasar el mouse o con el foco, y siempre en el chat abierto; el mismo menú sale con clic derecho en la fila y con Shift+F10 o la tecla de menú. Opciones: **Renombrar** (la fila se vuelve un campo con el título seleccionado; Enter guarda, Esc cancela, el error del servidor debajo; `conversation.rename`), **Exportar** (`conversation.export` con `id`; el aviso abajo) y, tras un separador, **Borrar…** en el color de error (ventana "¿Borrar «título»? No se puede deshacer." con Cancelar y Borrar; `conversation.delete`; con una respuesta en curso se muestra el mensaje Busy del servidor). En los ilegibles (`readable: false`) no hay menú: la fila muestra su error.
  - Encabezado: ocultar barra (Ctrl+B), título y Exportar. **Sin selector de modelo.**
  - Mensajes en una columna de 760 px como máximo, en su propia zona desplazable: el usuario en globo y el modelo sin globo, con la "m" de avatar. Markdown, bloque de código con Copiar. Mientras llega la respuesta, el texto termina en un cursor verde de bloque.
  - **Pensamiento:** en cada respuesta que lo tenga, arriba del texto, una línea plegable `› Pensamiento · N palabras` (13 px) que se abre y se cierra **por mensaje** (no hay un atajo para todo el chat, a diferencia del Ctrl+T de la TUI). Abierto: el texto en 14 px, con sangría y en el color de texto tenue (#9AA89E / #56645A: menos llamativo que la respuesta, pero pasa 4.5:1). Sin caja ni borde lateral. Plegado por defecto; mientras llega sin texto, abierto con las últimas líneas, y se pliega solo con el primer texto (ver Animaciones).
  - **Caja de mensaje delgada:** una sola fila (engranaje · texto · Enviar/Detener). Crece a dos filas solo cuando hay algo que mostrar junto al engranaje (hoy: la búsqueda web activa); el texto crece hacia arriba con varias líneas.
  - **Opciones** (engranaje, abajo a la izquierda, etiqueta "Opciones" al pasar el mouse): ventana con el **modelo** (proveedor con sus endpoints y los sin key atenuados, filtro y lista con el actual marcado), **Buscar en la web** (interruptor) y un enlace a Configuración del modelo. Usa `settings.provider.get`, `models.list` y `settings.provider.switch`.
  - Con la búsqueda activa, una pastilla **"Búsqueda web ×"** junto al engranaje.
  - **Enviar** (flecha verde) y **Detener** (cuadrado redondeado), solo ícono, sin fondo.
  - Ayuda: `Enter envía · Shift+Enter nueva línea · / comandos`.
- **Chat nuevo:** logo más "¿En qué te ayudo?". Si falta configurar el proveedor, una tarjeta con "Ir a Configuración".
- **Buscar chats** (Ctrl+K): ventana centrada con el campo, resultados por fecha con la coincidencia resaltada y el globo de búsqueda; ↑ ↓, Enter, Esc. **Por título** (lo que da `conversations.list`); buscar dentro del texto necesitaría un método nuevo en el RPC.
- **Configuración:** pantalla aparte (Volver al chat con Esc), menú de secciones a la izquierda, estado del servicio de Telegram al pie del menú, barra Cancelar / Guardar fija abajo; el panel de cada sección se desplaza solo. Por ahora solo en modo oscuro.
  - Proveedor: una lista con dos grupos, "De la lista" (con `key guardada`, `local` o atenuados `sin key`, que aquí sí se pueden elegir para ponerles key) y "Tus endpoints" (el en uso con punto verde) más **+ Agregar endpoint…**; debajo, el elegido: en un endpoint Nombre, URL base, API key (enmascarada) y Borrar endpoint (atenuado si está en uso, con la nota); en uno de la tabla solo la API key. Luego el modelo con filtro, Actualizar y escritura a mano.
  - Colores y accesibilidad: tema Sistema / Moss oscuro / Moss claro, tamaño del texto, Reducir animaciones y Más contraste. Se guarda en `settings.gui`.
  - **Configuración del modelo:** instrucciones del sistema (para todos los modelos: Predeterminadas / Ninguna / Personalizadas, contador, Restaurar); ajustes del modelo en uso (razonamiento de 5 opciones con la explicación según el proveedor, temperatura, Top P, Top K, máx. tokens, penalizaciones, semilla; vacío = predeterminado, errores junto al campo, Restaurar todo); Avanzado plegable con el JSON y la vista previa de lo que se envía.
  - Búsqueda web: el estado, la key y el aviso de `CHAT_SEARCH_API_KEY`.
  - Telegram: Servicio (Reiniciar, Actualizar estado, Ver registro), Bot, Acceso, Límites e Invitados. Guardar no reinicia: después, la GUI pregunta y llama a `service.apply`. Solo Linux: en Windows la sección dice que el bot funciona en Linux (`-32006`).
  - **Ver registro** (mesa `Config-registro`): ventana sobre Configuración (920×720 como máximo, el fondo oscurecido, el formulario no se mueve). Encabezado "Registro del bot" con `moss-telegram.service · últimas 200 líneas`; Actualizar, Copiar (todas las líneas completas, tal como llegan) y Cerrar (Esc). Las líneas en Geist Mono 13 px, en su propia zona desplazable, abajo al abrir. De cada línea `short-iso` (`<fecha> <equipo> <programa>[<pid>]: <mensaje>`) se muestra la hora y el mensaje, con separadores por día (Hoy, Ayer, 31 oct); una línea sin ese formato sale completa. Los errores llevan una barra roja a la izquierda y el texto en color de error (no solo el color). Pie: "La más reciente abajo" y, con `truncated`, "Se recortaron líneas largas". Sin líneas, el `notice` del servidor; mientras carga (si tarda más de 300 ms), "Leyendo el registro…". **Necesita un campo nuevo en `service.log`:** `errors: [índices]` con las líneas que marca `log_line_is_error`, aditivo y sin cambiar `protocol_version`, para no copiar esa regla a Dart (va en G4).
- Ventana angosta: por debajo de 900 px de ancho la barra lateral se oculta sola y Ctrl+B la abre encima del chat. Tamaño mínimo de la ventana: 720×520.

## Animaciones (1 nov 2026)

**Reglas**
1. Cortas: 100–200 ms. Entrar con `Curves.easeOutCubic`; salir un poco más rápido que entrar.
2. Solo opacidad, desplazamientos chicos (4–8 px) o escala leve (0.98 → 1). Sin rebotes, sacudidas, desenfoques ni sombras animadas (también por rendimiento con Impeller en las PCs viejas).
3. **Reducir animaciones** (la opción de Colores y accesibilidad, o `MediaQuery.disableAnimations` del sistema): todo a 0 ms y los indicadores que se repiten quedan fijos.
4. Todas las duraciones en un solo archivo (p. ej. `motion.dart`), para que "Reducir animaciones" las apague desde un lugar.
5. Solo mueven otras cosas de lugar las que responden a una acción o lo justifican (barra lateral, caja de dos filas, bloque Pensamiento), de acuerdo con el principio de no reacomodar.

| Acción | Animación | Duración |
|---|---|---|
| Hover y foco (filas, Opciones, Detener) | color de fondo | 100 ms |
| Etiqueta de un botón de ícono | aparece tras ~400 ms de hover (al instante con teclado): opacidad + sube 4 px | 120 ms |
| Menú de Opciones y menú `⋯` | opacidad + escala 0.98 → 1 desde la esquina del botón | 140 ms (cierra en 100) |
| Buscar chats (Ctrl+K) | el fondo se oscurece; la ventana aparece y sube 8 px | 180 ms |
| Chat ↔ Configuración | fundido, sin deslizar | 160 ms |
| Ocultar barra lateral (Ctrl+B) | el ancho | 200 ms |
| Caja que crece a dos filas | la altura | 140 ms |
| Tu mensaje al enviarlo | opacidad + sube 6 px | 160 ms |
| **Texto del modelo llegando** | **cada palabra nueva aparece con opacidad 0 → 1** (ver abajo) | **120 ms** |
| Cursor verde al final de la respuesta | parpadeo lento (1 s); fijo con Reducir animaciones | — |
| "Pensando…" | opacidad que pulsa suave (1.2 s); fija con Reducir animaciones | — |
| Bloque Pensamiento (plegar/desplegar) | la altura y la flechita gira | 180 ms |
| Pensamiento que se pliega solo con el primer texto | la altura, **sin que salte el scroll** (la respuesta se queda donde estaba a la vista) | 200 ms |
| [Copiar] → "Copiado ✓" | fundido del texto del botón; regresa a los 2 s | 120 ms |
| Error en un campo | el mensaje aparece con opacidad, sin sacudir el campo | 120 ms |
| Aviso corto (abajo al centro) | opacidad + sube 6 px; se va solo a los 4 s | 160 ms |
| "Cargando modelos…" | indicador chico, solo si tarda más de 300 ms | — |
| Cambio de tema | instantáneo | 0 ms |

**Texto del modelo llegando (decidido: 120 ms):**
- **Por palabra**, no por token: `moss-rpc` agrupa los deltas en ventanas de 30 ms, así que cada `entry_delta` trae varias palabras. La GUI parte el texto nuevo en palabras y cada una aparece con su propia animación de 120 ms, empezando al llegar. Las que llegan juntas aparecen juntas (sin escalonar, para no atrasar el texto).
- **Bloques de código: por línea.**
- Solo el texto que está llegando: al terminar la respuesta, al reabrir un chat guardado o al recibir un snapshot, el texto sale completo sin animación.
- Como mucho las últimas ~40 palabras animándose a la vez; una ráfaga más grande (p. ej. tras un "Pensando…" largo) aparece directo.
- El pensamiento mientras llega usa la misma regla.
- Con Reducir animaciones, apagado.

## App de Flutter (decidido 1 nov 2026)
- **Carpeta `gui/`** en el repo: proyecto `moss_gui` (`flutter create --platforms=linux,windows`), ejecutable **`moss-gui`** (`moss-gui.exe`), título de la ventana "Moss". Flutter estable.
- Habla con **`moss-rpc` como proceso hijo** (contrato: `rpc/PROTOCOL.md`; paquete `json_rpc_2`). Lo busca junto a su propio ejecutable; para desarrollo, `--rpc=<ruta>`. Si no está o no arranca, una pantalla de error con la ruta que buscó. Al cerrar: `shutdown`, `exit` y `kill` si no sale en 3 s.
- **Estado** con lo de Flutter (`ChangeNotifier`, `ValueNotifier`), sin paquetes de estado. **Dependencias mínimas** (`json_rpc_2`, `stream_channel`, `flutter_markdown_plus`); cualquier otra se justifica en el PR.
- **Markdown en Dart:** `flutter_markdown_plus` o `gpt_markdown` (el que nombra `PROTOCOL.md`); G1 elige con una comparación corta en su reporte. Lo que tiene que permitir: un widget propio para el bloque de código (Copiar), texto seleccionable y animar las palabras que llegan (G2). Copiar toma el texto del bloque tal como se ve.
- Textos de la GUI en español, juntos en un archivo. Tokens de color en un `ThemeExtension` (los dos temas), duraciones en `motion.dart`, fuentes en `gui/assets/fonts/` con su `OFL.txt`.
- **Pruebas:** `flutter analyze` y `flutter test` con un canal falso (sin `moss-rpc`); trabajo `gui` en la CI de Linux. Windows se compila desde G5.
- **Empaquetado (G5):** en Linux, el bundle de `flutter build linux` con `moss`, `moss-rpc` y `moss-telegram` adentro; en Windows, `moss-gui.exe` y sus archivos dentro de `moss-windows-x64.zip`, junto a `moss.exe` y `moss-rpc.exe`. La licencia de Geist va en `AVISOS-TERCEROS.txt`.

## Plan de PRs de la GUI

| PR | Contenido |
|---|---|
| **G1** | Proyecto `gui/`, tema (tokens, Geist, `motion.dart`), cliente RPC (arranque, `initialize`, cierre, pantalla de error), barra con la lista de chats (abrir, Nuevo chat), encabezado (Ctrl+B, título, Exportar), mensajes (Markdown, código con Copiar, Pensamiento por mensaje, fuentes, avisos, errores, fases), caja delgada con Enviar/Detener, `/comandos`, avisos cortos, chat vacío (sin la tarjeta), atajos (Enter, Shift+Enter, Esc, Ctrl+N, Ctrl+B), tema oscuro y claro según el sistema, `tools/fake-llm.py` (servidor falso para probar sin gastar), pruebas y CI. Prompt: `claude/prompt-g1-gui.md` |
| **G2** | Opciones (modelo, proveedor, búsqueda; también para `/model` sin modelo) y la pastilla, la tarjeta "Ir a Configuración", Buscar chats (Ctrl+K), menú `⋯`, menú de `/` (`commands.list`), texto que llega por palabra (120 ms) y el resto de las animaciones del chat |
| **G3** | Configuración: Proveedor (endpoints), Configuración del modelo, Búsqueda web, Colores y accesibilidad (`settings.gui`: tema, tamaño, Reducir animaciones, Más contraste) |
| **G4** | Telegram (formulario, servicio, invitados, código) y Ver registro, con `errors` en `service.log` (C++) |
| **G5** | Empaquetado Linux y Windows, `AVISOS-TERCEROS.txt`, LEEME, compilación de Windows en la CI; pruebas en Windows |
