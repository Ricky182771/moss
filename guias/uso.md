# Usar Moss en la terminal

Cómo se usa `moss`, la interfaz de terminal: teclas, comandos, la barra de conversaciones y qué hace con las respuestas. Para configurarlo (proveedor, key, colores, búsqueda), ver [configurar.md](configurar.md). La interfaz gráfica (`moss-gui`) está descrita en [gui/README.md](../gui/README.md).

## Ejecutar

Instalado con un paquete, es `moss`; con el ZIP de Windows, `moss.exe` (ver [En Windows](#en-windows)); compilado:

```bash
./build/release/cli/moss
```

Teclas:

| Tecla | Acción |
|---|---|
| Enter | Envía el mensaje (no hace nada mientras hay una respuesta en curso) o ejecuta un comando (`/buscar`, `/copiar`, `/guardar`, `/exportar`); con el menú de comandos abierto, elige el comando |
| `/` | Al principio de la caja, abre el menú de comandos (ver [Comandos](#comandos)) |
| `\` + Enter | Nueva línea en la caja (el `\` justo antes del cursor se cambia por el salto) |
| Alt+Enter | Nueva línea en la caja, en las terminales que lo mandan (en Windows no: es pantalla completa) |
| ↑ / ↓ | Mueven el cursor una fila en pantalla de la caja (también dentro de un párrafo que se partió en varias filas), conservando la columna; en la primera fila ↑ va al principio del texto y en la última ↓ al final |
| ← / → | Mueven el cursor un carácter (un emoji o una letra con acento combinado cuentan como uno); Ctrl+← / Ctrl+→, una palabra |
| Retroceso / Supr | Borran el carácter antes o después del cursor |
| Insert | Cambia entre insertar y sobrescribir (el cursor se vuelve un bloque) |
| Esc | Cierra el menú de comandos si está abierto; si no, cancela la respuesta en curso (el texto regresa a la caja) |
| PgUp / PgDn | Sube o baja una pantalla del historial |
| Rueda del ratón | Sube o baja unas 3 líneas (del historial, de la barra si el puntero está sobre ella, o de la caja si el puntero está sobre ella y su texto no cabe) |
| Clic en la caja | Pone el cursor en ese carácter (después del final de una fila, al final de esa fila) |
| Home / End | Con la caja vacía, va al inicio o al final del historial; con texto, lleva el cursor al inicio o al final de la fila en pantalla en que está |
| F2 | Abre o cierra la configuración |
| Ctrl+N | Empieza una conversación nueva |
| Ctrl+B | Muestra u oculta la barra de conversaciones |
| Ctrl+O | Pasa a la barra de conversaciones (y la muestra si estaba oculta) |
| Ctrl+T | Abre el pensamiento de todas las respuestas (si ya están todos abiertos, los cierra); ver [Pensamiento](#pensamiento-del-modelo) |
| Ctrl+C | Sale |

## Barra de conversaciones

A la izquierda está la lista de conversaciones guardadas, agrupadas por fecha: **Hoy**, **Ayer**, **Últimos 7 días** y después un grupo por día (`2 oct`, o `15 dic 2025` si es de otro año). La primera fila, `+ Nueva`, empieza una conversación nueva, y la segunda, `⚙ Configuración` (`* Configuración` con símbolos sencillos), abre la configuración. `●` (`•` con símbolos sencillos) marca la conversación abierta, `⌕` (`@` con símbolos sencillos) al final del título marca las que usaron `/buscar` (en una barra angosta se quita antes de recortar mucho el título), y las ilegibles aparecen tenues, con `(ilegible)`, en el grupo **Sin fecha** al final.

- Se ve al arrancar si la terminal mide 100 columnas o más. Si la terminal se angosta a menos de 100, se oculta sola, y reaparece al volver a ensancharla (salvo que la hayas ocultado tú con Ctrl+B).
- Para cambiar su ancho, arrastra con el ratón su borde derecho (de 18 a 60 columnas, o hasta la mitad de la terminal). El ancho no se guarda al salir.
- Con Ctrl+O la barra toma el foco: ↑/↓, PgUp/PgDn y Home/End para moverte (los encabezados de grupo se saltan); Enter abre; **r** renombra; **e** exporta; Supr borra (pide confirmación en la línea de estado: **s** borra, **n** o **Esc** no; las demás teclas y el ratón no la contestan); Esc vuelve a la caja de entrada. La fila seleccionada solo se resalta mientras la barra tiene el foco.
- **Renombrar** (**r**): el título se vuelve un campo con el cursor al final; Enter guarda y Esc cancela. Los espacios y saltos de más se quitan; hasta 60 caracteres (uno más largo da un error en la línea de estado y el campo sigue abierto, no se recorta). Renombrar no cuenta como actividad: la conversación no sube en la lista. Se puede también con una respuesta en curso.
- **Exportar** (**e**): igual que `/exportar`, pero de la conversación seleccionada, esté abierta o no; exporta lo que está guardado. El aviso sale en la línea de estado.
- Con el ratón: un clic en una conversación la abre, en `+ Nueva` empieza una nueva y en `⚙ Configuración` abre la configuración. Un **clic derecho** en una conversación abre debajo de ella un menú con **Renombrar**, **Exportar** y **Borrar**: se elige con clic, o con ↑/↓ y Enter; Esc o cualquier otra tecla lo cierra, y también se cierra si la barra pierde el foco.
- Mientras hay una respuesta en curso, abrir, borrar o crear una conversación (desde la barra o con Ctrl+N) no hace nada: espera la respuesta o cancélala con Esc. Moverte por la lista, renombrar y exportar sí se puede.

## Caja de entrada y pegar

La caja acepta varias líneas y ajusta el texto al ancho: una línea más larga que la caja se parte en varias filas por palabras (después de un espacio; una palabra más ancha que la caja se parte donde ya no cabe). Ninguna fila empieza con el espacio del corte: si la palabra llena justo la fila, ese espacio se queda al final de la fila, en la columna del cursor. El ajuste es solo de pantalla: lo que se envía es exactamente lo que escribiste, sin saltos ni espacios de más. La última columna queda libre para el cursor. La caja crece con sus filas en pantalla hasta 8 (o un tercio de la terminal, lo que sea menor); después aparece una barra a la derecha y se desplaza siguiendo al cursor (o con la rueda). Enter siempre envía; para un salto de línea usa `\` + Enter o Alt+Enter (Shift+Enter y Ctrl+Enter no se distinguen de Enter en la terminal).

Pega con el atajo de tu terminal (en Linux casi siempre Ctrl+Shift+V). Moss activa el *pegado entre corchetes* (bracketed paste): un texto pegado con varias líneas queda completo en la caja, sin enviarse, y los tabuladores pasan a 4 espacios. Si tu terminal no lo soporta, cada salto de línea pegado llega como Enter y envía el mensaje a la mitad: pega una sola línea o usa otra terminal. En los campos de una línea de la configuración (URL, keys, filtro y modelo), los saltos de línea pegados se quitan. Un pegado de más de 256 KiB se recorta y avisa.

Si subes en el historial, la vista se queda donde está aunque llegue texto nuevo, y la línea de estado muestra `↓ Hay más abajo (End)`, o `(PgDn)` si hay texto en la caja (porque ahí End mueve el cursor). Al enviar un mensaje, la vista regresa abajo.

Moss captura el ratón para la rueda. Para seleccionar texto con el ratón, mantén Shift mientras arrastras, como en la mayoría de terminales con apps que usan el ratón. Para abrir un enlace, Shift+clic sobre él (en Kitty también Ctrl+Shift+clic): un clic simple lo recibe Moss y no abre nada.

## Comandos

Escribe `/` al principio de la caja y arriba de ella aparece el menú de comandos, con sus argumentos y qué hacen (si la línea de estado no tiene avisos, el menú ocupa su lugar). Se filtra mientras escribes el nombre (`/g` deja solo `/guardar`; no distingue mayúsculas) y se cierra en cuanto escribes un espacio.

| Comando | Qué hace |
|---|---|
| `/buscar <consulta>` | Busca en la web y responde con fuentes citadas (ver [Búsqueda web](configurar.md#búsqueda-web)) |
| `/copiar [número de bloque]` | Copia un bloque de código; sin número, el último |
| `/guardar [número de bloque [nombre]]` | Guarda un bloque de código en un archivo |
| `/exportar` | Exporta la conversación a Markdown |
| `/model [proveedor] [modelo]` | Cambia de modelo o de proveedor (ver [Cambiar de modelo con /model](#cambiar-de-modelo-con-model)) |

- ↑/↓ mueven el cursor del menú (dan la vuelta en los extremos). Tab completa el comando: `/buscar ` con un espacio para que escribas la consulta, o `/exportar` solo. Enter hace lo mismo con `/buscar` (no se envía sin consulta) y con los demás los completa y los ejecuta de una vez: `/ex` + Enter exporta.
- Esc cierra el menú sin borrar lo que escribiste; no se vuelve a abrir hasta que cambies el texto. Con una respuesta en curso, el primer Esc cierra el menú y el segundo la cancela.
- Con el ratón, pasar el puntero mueve el cursor y un clic hace lo mismo que Tab.
- Un comando que ahora no sirve se ve atenuado con el motivo (`falta la key de búsqueda (F2)`, `no hay bloques de código`, `la conversación está vacía`, `espera a que termine la respuesta`), pero se puede elegir igual: al enviarlo aparece el aviso de siempre.
- Si nada coincide, el menú dice `Sin comandos que coincidan · se enviará como mensaje`: un texto como `/hola` va al modelo como cualquier mensaje.
- En una terminal angosta la descripción se recorta con `…` y, si no cabe, se oculta antes que el nombre. Con la barra de conversaciones o la configuración con el foco no hay menú.

## Cambiar de modelo con /model

`/model` cambia de modelo o de proveedor sin abrir F2 y sin cerrar la conversación; el cambio se guarda en `config.json` igual que en F2 y en el historial queda `Ahora se usa <modelo> de <proveedor>.`. No pide ni guarda keys: solo deja elegir proveedores con key guardada (o locales, como Ollama).

| Escribes | Hace |
|---|---|
| `/model` | Abre el selector en la lista de proveedores |
| `/model <proveedor>` | Abre el selector en la lista de modelos de ese proveedor |
| `/model <modelo>` | Cambia el modelo en el proveedor actual |
| `/model <proveedor> <modelo>` | Cambia de proveedor y de modelo |

- `<proveedor>` es el id de la tabla de [Configurar](configurar.md#proveedor-y-modelo) (`nvidia`, `openai`, `gemini`, …, `ollama`) o el de uno de tus [endpoints](configurar.md#endpoints-personalizados), solo o con `custom:` (`/model omniroute qwen3` y `/model custom:omniroute qwen3` hacen lo mismo), sin distinguir mayúsculas. Una sola palabra que no es un id se toma como modelo (`/model z-ai/glm5`).
- El selector aparece donde el menú de comandos y se maneja igual (lo que escribes filtra, ↑/↓, Enter, Esc, ratón). Los endpoints van después de la tabla, con su id y su nombre. Cada proveedor dice `key guardada`, `local` o, atenuado y sin poderse elegir, `sin key`. Para agregar un endpoint, usa F2. En la lista de modelos, si el filtro no deja nada o el proveedor no da la lista, la última fila es `Usar «<texto>»` para escribir el id a mano. Esc regresa a los proveedores y, ahí, cierra.
- El modelo no se valida contra la lista: si no existe, el siguiente mensaje da el error de modelo no encontrado.
- No funciona con una respuesta en curso ni con `CHAT_MODEL` definida; con `CHAT_BASE_URL` solo cambia el modelo.

## Markdown en las respuestas

Las respuestas del asistente se muestran con formato de markdown, también mientras van llegando. Tus mensajes, los errores y los avisos se muestran como texto plano.

Se muestra:

- Párrafos, encabezados (`#` a `######`), citas (también anidadas) y líneas horizontales (`---`).
- **Negritas**, *cursivas*, ~~tachado~~, `==resaltado==` y `código en línea`.
- Listas con viñetas y numeradas, anidadas, y listas de tareas (`- [ ]` y `- [x]`).
- Bloques de código con su número y el lenguaje como título (`#3 · cpp`, o `#3` si no dice el lenguaje). Las líneas largas se parten, sin colores de sintaxis. El número sirve para `/copiar` y `/guardar`.
- Tablas con alineación por columna. Si no caben, el texto se ajusta dentro de las celdas; si ni así caben, cada fila se muestra como una tarjeta `Encabezado: valor`.
- Enlaces: el texto subrayado y la dirección al lado. En las terminales que lo soportan, el enlace se abre con Shift+clic.
- Imágenes como `[imagen: descripción]` con su dirección, porque la terminal no muestra imágenes.
- Alertas de GitHub (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, `[!CAUTION]`).
- Notas al pie (`[^1]`), que aparecen al final de la respuesta.
- Fórmulas de LaTeX (`$...$` y `$$...$$`) como texto, sin convertir.

El HTML dentro de la respuesta se muestra tal cual, como texto. Los caracteres de control que podrían alterar la terminal (por ejemplo, las secuencias que empiezan con ESC) se reemplazan por `�`, también en tus mensajes y en los errores.

Para ver cómo se muestra un archivo markdown sin usar la API:

```bash
./build/release/tools/md_preview --width 60 tests/data/markdown_muestra.md
./build/release/tools/md_preview --theme terminal tests/data/markdown_muestra.md
```

`--width` es el ancho en columnas (80 si no lo pones). `--theme` elige el tema (`catppuccin-mocha` si no lo pones, o `terminal`), `--background theme|terminal` el fondo y `--symbols unicode|simple` los símbolos (`unicode` si no lo pones). Si la salida es una terminal, se ve con colores y estilos; si la rediriges a un archivo, sale como texto plano.

## Pensamiento del modelo

Algunos modelos mandan su "pensamiento" aparte de la respuesta (en NIM, Nemotron con el razonamiento en Predeterminado o encendido; en OpenRouter, los que lo devuelven). Moss lo muestra arriba de la respuesta, atenuado, en una línea plegable:

```text
▸ Pensamiento · 1 240 palabras
```

- Mientras llega y todavía no hay texto, se ve abierto con sus **últimas 6 líneas** (la línea de estado sigue en "Pensando…"); con el primer texto de la respuesta se pliega solo.
- **Clic** en la línea lo abre (`▾`, con el texto completo, sangrado) o lo cierra. **Ctrl+T** abre el de todas las respuestas, o los cierra si ya estaban todos abiertos.
- Se abre y se cierra solo en la sesión: al reabrir una conversación todo empieza plegado.
- Se guarda con la conversación (hasta 256 KiB por respuesta; si pasa, se recorta y lo dice al final), pero **nunca se le vuelve a mandar al modelo**. Tampoco cuenta para `/copiar`, `/guardar`, `/exportar` ni los números de los bloques: un bloque de código dentro del pensamiento no tiene botones.
- Una respuesta cancelada (Esc) o cortada por un error conserva el pensamiento que alcanzó a llegar.
- Si el modelo se queda sin salida mientras piensa (termina por `length` sin texto), el error dice `El modelo se quedó sin tokens pensando; sube el máximo de salida en Configuración del modelo.` en vez de `El modelo no devolvió texto.`
- Con modelos que no lo mandan (por ejemplo Gemini por la capa compatible con OpenAI, u OpenAI en Chat Completions) no aparece ninguna línea. En modo de símbolos Sencillos, `▸`/`▾` se ven como `>`/`v`.

## Copiar, guardar y exportar

Los bloques de código de las respuestas se numeran desde 1 en toda la conversación, en el orden en que aparecen; el número va en el título del bloque (`#3 · cpp`).

**Con el ratón.** Cada bloque lleva a la derecha de su título los botones `[Copiar]` y `[Guardar]`. Si el bloque es más alto que la pantalla, al bajar con la rueda los botones se quedan pegados a la primera fila visible del bloque y desaparecen con él cuando ya no queda ninguna fila de código a la vista.

- `[Copiar]` copia el bloque y cambia a `[✓ Copiado]` hasta que mueves el puntero fuera del botón o pasan 2 segundos.
- `[Guardar]` lo guarda con el nombre por defecto (`bloque-3.cpp`). Para otro nombre, usa `/guardar 3 suma.cpp`.
- `[Exportar]`, a la derecha de la barra de título, exporta la conversación. Sin respuestas que exportar se ve atenuado y no hace nada.
- El resultado aparece en la línea de estado, corto y con lo importante primero: `#1 copiado con wl-copy`, `#1 copiado con OSC 52 · si no pega, usa [Guardar]`, `#1 guardado en ~/Descargas/moss/bloque-1.cpp` o `No se pudo copiar #1 · usa [Guardar]`. Si no cabe, termina en `…`; el indicador de la derecha (`Pensando…`) nunca se recorta. Los botones no agregan nada a la conversación ni mueven la vista: puedes copiar un bloque de la mitad de la conversación sin que salte al final.
- Funcionan también mientras llega una respuesta: un bloque que sigue llegando se copia como va, y el aviso empieza con `⚠ #1 incompleto (aún no termina)`. Lo mismo con un bloque de una respuesta cancelada (`se canceló`) o cortada por un error (`se cortó`).
- En una terminal angosta los botones se acortan a `[C]` `[G]`; si tampoco caben, no se muestran (los comandos siguen funcionando). Con el puntero encima van en el color de selección del tema (invertidos con `NO_COLOR`).

**Con comandos**, para cuando no hay ratón (por ejemplo, por SSH desde una terminal que no lo manda) o prefieres el teclado. No se envían al modelo ni entran al historial: el resultado aparece como un aviso en la conversación.

| Comando | Qué hace |
|---|---|
| `/copiar` | Copia el último bloque de código de la conversación |
| `/copiar 3` | Copia el bloque #3 |
| `/guardar` o `/guardar 3` | Guarda el último bloque, o el #3, como `bloque-3.cpp` (la extensión sale del lenguaje) |
| `/guardar 3 suma.cpp` | Lo guarda con ese nombre (si no le pones extensión, se agrega la del lenguaje) |
| `/exportar` | Exporta la conversación a Markdown, como `conversacion-<id>.md` |

- Solo en minúsculas y con los argumentos separados por espacios. Cualquier otro texto que empiece con `/` (por ejemplo `/copiarx`) se envía como un mensaje normal.
- Con una respuesta en curso, la línea de estado dice `Espera a que termine la respuesta`. Si no hay bloques, el número no existe o sobran argumentos, la línea de estado explica cómo se usa y el texto se queda en la caja.
- `/guardar` y `/exportar` nunca sobrescriben: si el archivo ya existe, usan `suma-2.cpp`, `suma-3.cpp`… (hasta `-99`). Los archivos se crean con permisos 0644 (o menos, según tu `umask`), nunca como ejecutables (tampoco los `.sh`).
- El nombre que das es solo el nombre del archivo: no puede llevar `/`, `\`, `..` ni caracteres de control, ni empezar con `.`, y mide como máximo 100 bytes. Para que lo guardado se pueda copiar a Windows, en todos los sistemas tampoco puede llevar `< > : " | ? *`, terminar en punto o espacio, ni llamarse como un dispositivo de Windows (`CON`, `PRN`, `AUX`, `NUL`, `COM1` a `COM9`, `LPT1` a `LPT9`, también con extensión: `con.cpp`).
- El archivo exportado tiene el título, la fecha y el modelo, y cada pregunta (`## Tú`, tal como la escribiste, también el `/buscar …`) con su respuesta (`## Asistente`) y, si hubo búsqueda, sus fuentes (`### Fuentes`). Nunca incluye las instrucciones del sistema, los resultados de búsqueda que recibió el modelo, keys, avisos ni errores.

**Cómo se copia**, en este orden:

1. Si estás conectado por SSH (`SSH_CONNECTION` o `SSH_TTY`), primero con la secuencia OSC 52 de la terminal, para que llegue al portapapeles de tu máquina local.
2. Con un programa del sistema que esté en tu `PATH`: `termux-clipboard-set` en Termux (paquete `termux-api` y la app Termux:API), `wl-copy` en Wayland, o `xclip` o `xsel` en X11. En Fedora: `sudo dnf install wl-clipboard` (Wayland) o `sudo dnf install xclip` (X11).
   En Windows, en lugar de un programa, el portapapeles de Windows.
3. Si nada de eso funciona, con OSC 52, y el aviso dice `si tu terminal no lo soporta, usa /guardar`. Kitty, WezTerm, foot, Alacritty y Ghostty soportan OSC 52; dentro de tmux se envía envuelta para tmux, y puede hacer falta `set -g allow-passthrough on` o `set -g set-clipboard on` en tu `~/.tmux.conf`.

OSC 52 solo manda bloques de hasta 100 000 bytes; para uno más grande, usa `/guardar`.

**Dónde se guarda:** en la carpeta `moss/` (se crea con permisos 0755, o menos según tu `umask`) dentro de la primera de estas que aplique:

1. `CHAT_DOWNLOAD_DIR`, si la defines;
2. tu carpeta de descargas de `~/.config/user-dirs.dirs` (`XDG_DOWNLOAD_DIR`, la que configura tu escritorio), si existe;
3. en Termux, `~/storage/downloads` (después de correr `termux-setup-storage`);
4. `~/Descargas` o `~/Downloads`, la que exista;
5. tu carpeta personal (`~`).

En Windows: `CHAT_DOWNLOAD_DIR`, si la defines; si no, tu carpeta Descargas; si Windows no la da, `%USERPROFILE%`.

Para cambiarla, define la variable antes de abrir Moss:

```bash
CHAT_DOWNLOAD_DIR=~/proyectos/escuela ./build/release/cli/moss
```

## En Windows

**Ejecutar.** El programa es una aplicación de consola: córrelo desde **Windows Terminal** (recomendado), **CMD** o **PowerShell**. En la terminal de MSYS2 (mintty) no corre, porque no es una consola de Windows. No necesita nada en el `PATH`:

```bat
build\release\cli\moss.exe
```

**Qué cambia en Windows:**

- La configuración y la key van en `%APPDATA%\moss\` (`config.json` y `credentials.json`), las conversaciones en `%LOCALAPPDATA%\moss\conversations` y `/guardar` y `/exportar` en tu carpeta Descargas, dentro de `moss\`. Las variables `CHAT_*` siguen teniendo prioridad; `HOME` y `XDG_*` no se usan (MSYS2 define `HOME`). Con el [modo portable](configurar.md#modo-portable), todo menos las descargas va a `datos\`, junto a `moss.exe`.
- Con doble clic en `moss.exe` se abre en conhost, y esa ventana se cierra en cuanto el programa termina. Por eso, si Moss no puede arrancar (por ejemplo, en modo portable con `datos\` sin permiso de escritura), muestra el error y `Presiona Enter para cerrar esta ventana.`. Desde CMD, PowerShell o Windows Terminal (con una shell abierta) no espera nada.
- `credentials.json` no necesita `chmod`: lo protege la carpeta de tu perfil de Windows, que solo tu usuario puede leer. Todavía no va cifrado.
- `/copiar` y `[Copiar]` usan el portapapeles de Windows (con acentos y saltos de línea que el Bloc de notas respeta); por SSH, OSC 52 primero.
- Para pegar, usa Ctrl+V, Ctrl+Shift+V o Shift+Insert: Moss lee el portapapeles de Windows y pega en la caja o en el campo de la configuración que tenga el foco (con el foco en la barra de conversaciones no pega nada; en una lista o un botón avisa `Aquí no se puede pegar.`, y si el portapapeles no tiene texto, `El portapapeles no tiene texto.`). En Windows Terminal esos atajos los atiende la propia terminal, con el mismo resultado. En conhost (CMD o PowerShell sin Windows Terminal) está comprobado con `CHAT_DEBUG_KEYS`: Ctrl+V y Ctrl+Shift+V llegan como el byte `16` y Shift+Insert como `1b 5b 32 3b 32 7e` (`ESC [ 2 ; 2 ~`), y los tres pegan. Shift + clic derecho es el pegado propio de conhost: no pasa por el portapapeles de Moss. Si los atajos no hacen nada, revisa que el `moss.exe` que abres sea del preset que acabas de compilar (`build\release` no se actualiza al compilar `dev`).
- HTTPS usa el TLS de Windows (Schannel) y los certificados de su almacén: no hace falta instalar nada más. En Windows 10 llega hasta TLS 1.2, que es lo que piden NVIDIA NIM, Tavily y Telegram. Si la red bloquea los servidores de revocación de certificados (como algunas escolares), la conexión sigue: la revocación se revisa cuando se puede y la cadena del certificado, siempre.
- Al arrancar, la consola se pone en UTF-8 para que los acentos se vean bien, y al salir vuelve a la página de códigos que tenía (`chcp` muestra lo mismo que antes).
- Ctrl+C sale limpio, también a mitad de una respuesta. Ctrl+Break y cerrar la ventana terminan el programa de golpe: lo que ya estaba guardado sigue ahí, pero la respuesta en curso se pierde y, con Ctrl+Break, `chcp` puede quedar en 65001.
- En CMD y PowerShell sin Windows Terminal (conhost) de Windows 10, la fuente de la consola no tiene algunos símbolos: por eso **Símbolos** en `Automático` usa `Sencillos` ahí (ver [Colores y accesibilidad](configurar.md#colores-y-accesibilidad)). En Windows 11 conhost busca el símbolo en otra fuente y se usa `Unicode`; en Windows Terminal, siempre `Unicode`.
- El ratón en conhost funciona como en las demás terminales: clics, rueda, arrastrar el borde de la barra y pasar el puntero sobre el menú y los botones. Conhost se queda con Shift + clic para su propia selección (*QuickEdit*): **Shift + arrastrar** selecciona texto de la pantalla y **Enter** lo copia (Esc cancela); Shift + clic derecho pega. La selección toma líneas completas de la pantalla, así que con la barra lateral abierta también copia su borde y sus filas: ciérrala con Ctrl+B antes de seleccionar, o usa `[Copiar]` en los bloques de código. Mientras hay una selección, conhost detiene la salida de Moss (la respuesta deja de avanzar en pantalla) hasta que copias o cancelas; después aparece de golpe lo que llegó mientras tanto. Es así en todas las apps de consola, no es una falla de Moss.
