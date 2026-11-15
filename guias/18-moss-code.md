# Diseño — Moss Code (modo agente de programación)
 
Borrador del 14 de noviembre de 2026, sobre `d475b29` (Moss 1.0.0).
 
Objetivo: **el flujo de trabajo de Claude Code dentro de Moss.** Abres una terminal en la carpeta de un proyecto, escribes `moss code`, le pides algo al modelo, y el modelo lee archivos, busca, edita y corre comandos para hacerlo. Tú ves cada paso y apruebas lo que cambia algo. La TUI de chat no cambia, y `moss` sin argumentos sigue siendo el chat.
 
Moss ya no es parte del servicio social: Moss Code es de uso general, para cualquier programador.
 
---
 
## 0. Decisiones
 
| # | Decisión | Propuesta | Estado |
|---|---|---|---|
| D1 | Dónde vive | **Un modo de la TUI:** `moss code [carpeta]` abre la misma app en modo código. El agente (herramientas, permisos y ciclo) va en una biblioteca sin FTXUI, para que después la use `moss-rpc` (y la GUI). Opción de CMake `CHATBOT_BUILD_CODE` (encendida por defecto) | **Decidido (14 nov 2026)** |
| D2 | Plataformas | Leer, buscar y editar en Linux y Windows desde el principio. **Ejecutar comandos, solo en Linux**, hasta un hito propio para Windows | **Decidido (14 nov 2026)** |
| D3 | Para quién | Uso general, imitando a Claude Code. Sin modo escuela | **Decidido (14 nov 2026)** |
| D4 | Modelos | **NVIDIA NIM.** Los candidatos se eligen en C0 con una sonda contra la lista viva de `/v1/models`, porque el catálogo gratis cambia (GLM-5.2 ya da 410 y Kimi K3 salió del plan gratis) | **Decidido (14 nov 2026)**; los modelos, en C0 |
| D5 | Instrucciones del proyecto | `AGENTS.md` en la raíz del proyecto (el estándar abierto que ya usan Moss y OpenCode); si no hay, `CLAUDE.md`. Más uno personal en `~/.config/moss/AGENTS.md` | Propuesta |
| D6 | Pensamiento en el modo código | **Se reenvía**, con la misma llave en que llegó (`reasoning_content` o `reasoning`), en los mensajes del asistente de una sesión de código. Es una excepción a la regla de M3 ("nunca se reenvía"), y solo en el modo código: el chat no cambia | Propuesta (casi obligada, ver 3.4) |
| D7 | Idioma de las herramientas | Nombres, descripciones e instrucciones de sistema del modo código **en inglés** (los modelos usan mejor las herramientas así). El modelo te contesta en español, y todo lo que ves en la pantalla está en español | Propuesta |
| D8 | Modos de permiso | Como Claude Code, con Shift+Tab: **Preguntar** (por defecto) → **Aceptar ediciones** → **Plan**. **Sin preguntar** solo con la bandera `--sin-preguntar` | Propuesta |
| D9 | Reglas guardadas | `.moss/settings.json` (del proyecto, se puede subir a git) y `.moss/settings.local.json` (tuyo, ignorado por git), con la sintaxis de Claude Code: `bash(cmake --build:*)` | Propuesta |
| D10 | Comandos con shell | La herramienta `bash` corre `bash -c` (es lo que el modelo espera). Es una excepción a "procesos sin shell" de `AGENTS.md`: solo para comandos que pidió el modelo y que tú aprobaste | Propuesta |
| D11 | Dónde se guardan las sesiones | Carpeta aparte `…/moss/code/`, con su propio formato. No salen en la barra del chat, y una versión vieja de Moss no las ve | Propuesta |
| D12 | `grep` | `rg` (ripgrep) si está en el `PATH`; si no, una búsqueda propia con `std::regex` y límites de tamaño | Propuesta |
 
**Por qué D1:** `moss-telegram` es un ejecutable aparte porque corre como servicio, sin pantalla. Moss Code es interactivo, igual que el chat: comparte la configuración, el proveedor, `/model`, F2 y el dibujo de markdown. Un ejecutable aparte repetiría todo eso. Lo que sí se separa es el **motor**, en una biblioteca sin FTXUI, por la misma razón que se separó `Session` en R0.
 
**Por qué D6:** DeepSeek, Kimi, GLM y Qwen en modo de pensamiento exigen que el pensamiento de los pasos con herramientas regrese en las peticiones siguientes; si no, responden 400 ("The reasoning_content in the thinking mode must be passed back to the API"). Cursor tuvo justo ese problema. En el chat no hace falta porque no se mandan herramientas.
 
---
 
## 1. Cómo se ve (el flujo)
 
```text
$ cd ~/proyectos/moss
$ moss code
```
 
```text
 Moss Code — ~/proyectos/moss — deepseek-ai/deepseek-v3.2
 AGENTS.md cargado · Preguntar (Shift+Tab cambia)
 
 > Agrega una prueba para trim_history con un mensaje vacío
 
 Voy a ver cómo están las pruebas de history.
 
 ● Buscar  "trim_history" en tests/
   ⎿ 3 archivos
 ● Leer  tests/test_history.cpp (1–180)
   ⎿ 180 líneas
 
 ● Editar  tests/test_history.cpp
   ┌───────────────────────────────────────────────
   │ 141   }
   │ 142 +
   │ 143 + TEST_CASE("trim_history con un mensaje vacío") {
   │ 144 +     ...
   └───────────────────────────────────────────────
   ¿Aplicar este cambio?  [s] Sí  [t] Sí, y no preguntar más por ediciones  [n] No, y decirle qué hacer
 
 ● Ejecutar  cmake --build --preset dev && ctest --preset dev -R history
   ¿Ejecutar este comando?  [s] Sí  [t] Siempre `cmake --build:*` en este proyecto  [n] No
   ⎿ 100% tests passed, 0 tests failed out of 12   (Ctrl+O: salida completa)
 
 Listo: agregué la prueba y pasa.
 
 ☐ Agregar la prueba  ☑ Compilar y correr
 ─────────────────────────────────────────────────
 > _                                                  Preguntar · 18 % del contexto
```
 
- Cada llamada a una herramienta es una línea `●` con su resumen, y abajo `⎿` con el resultado plegado. Ctrl+O abre la salida completa.
- **Esc** detiene en cualquier punto: la respuesta, una herramienta o un comando (mata el proceso). Lo que ya se aplicó se queda.
- Las preguntas de aprobación usan la línea de estado y las teclas `s` / `t` / `n` / Esc. Con `n`, puedes escribir por qué, y eso le llega al modelo ("El usuario rechazó el cambio: usa Catch2 SECTION").
- La lista de tareas del modelo (`todo_write`) se ve arriba de la caja.
- `moss code -p "pregunta"` corre sin pantalla, imprime la respuesta y sale (como `claude -p`). Sirve para scripts y para las pruebas de punta a punta. Sin pantalla nadie puede aprobar: con `-p`, todo lo que pediría aprobación se rechaza, salvo lo permitido por reglas o con `--sin-preguntar`.
---
 
## 2. Arquitectura
 
```
moss (TUI)                       moss-rpc (C6)
   │  vista de código (FTXUI)        │  métodos code.* y eventos
   ▼                                 ▼
┌────────────────── chatbot_code (sin FTXUI) ───────────────────┐
│ CodeSession ── AgentLoop ── ToolRegistry ── PermissionEngine  │
│      │             │          read · glob · grep · edit ·     │
│      │             │          write · bash · todo_write       │
│ SessionStore    ProjectRoot (rutas)   ProcessGroup (Linux)    │
└────────────┬──────────────────────────────────────────────────┘
             ▼
        chatbot_core: ChatClient con tool calling (C0)
```
 
- **`chatbot_core`** aprende tool calling (sección 3), pero no sabe qué herramientas existen.
- **`chatbot_code`** (carpeta nueva `code/`): herramientas, permisos, ciclo del agente, rutas del proyecto, procesos con grupo, guardado de sesiones y `CodeSession`. Depende de `chatbot_core` y nlohmann/json; **nunca** de FTXUI. Si necesita algo de `chatbot_cli_lib` (por ejemplo, el selector de modelos o la configuración vigente), C1 decide con `explorer` si se mueve a `core` o si `chatbot_code` depende de `chatbot_cli_lib`; no se duplica.
- **`CodeSession`** es al modo código lo que `Session` es al chat: se usa solo desde el hilo de la interfaz, los trabajadores le hablan por `post`, y la TUI y el RPC dibujan con lo que avisa.
- **`AgentLoop`**: una máquina de estados en el hilo de la interfaz. Los trabajos lentos (la petición al modelo, una búsqueda grande, un comando) corren en un hilo de trabajo, como hoy `RequestRunner`.
```
Idle ─send─► Requesting ─(texto, sin herramientas)─► Idle
                 │
                 └─(tool_calls)─► por cada llamada, en orden:
                        Checking ─permitida─► Running ─► (resultado)
                           │
                           └─hay que preguntar─► AwaitingApproval ─s/t─► Running
                                                        └─n/Esc─► resultado "rechazada"
                      todas con resultado ─► Requesting (siguiente paso)
```
 
**Invariante clave:** cada `tool_call` que mandó el modelo recibe **exactamente un** mensaje `tool` con su `tool_call_id`, aunque se rechace, falle, se cancele o se pase del tiempo. Si falta uno, la siguiente petición da 400. Al cancelar con Esc, las llamadas pendientes reciben `"Cancelled by the user."` y el turno termina.
 
**Límites del turno** (configurables en `config.json` → `"code"`):
 
- **Pasos:** 50 peticiones al modelo por turno. Al llegar, se detiene y pregunta "¿Seguir?".
- **Bucles:** la misma herramienta con los mismos argumentos 3 veces seguidas detiene el turno con un aviso. Los modelos gratis caen en esto.
- **Argumentos rotos:** si el modelo manda argumentos que no son JSON, se le regresa el error como resultado para que corrija; 3 seguidos detienen el turno.
- **Cuota:** el plan gratis de NIM limita las peticiones por minuto (se reporta alrededor de 40). El 429 con `Retry-After` ya se reintenta; la línea de estado dice "Esperando a NIM (límite de peticiones)…" en vez de quedarse muda.
---
 
## 3. Tool calling en el núcleo (C0)
 
### 3.1 Mensajes
 
`Message` crece sin romper a nadie:
 
```cpp
enum class Role { System, User, Assistant, Tool };
 
struct ToolCall {
    std::string id;         // "call_abc123"; si el servidor no lo manda, se genera uno
    std::string name;       // "read"
    std::string arguments;  // JSON crudo, tal como llegó (se valida al ejecutar)
};
 
struct Message {
    Role role;
    std::string content;              // puede ir vacío si hay tool_calls
    std::vector<ToolCall> tool_calls; // solo Assistant
    std::string tool_call_id;         // solo Tool
    std::string reasoning;            // solo Assistant; se manda solo si replay_reasoning (D6)
    ReasoningKey reasoning_key;       // reasoning_content | reasoning: la llave en que llegó
};
```
 
- `build_request_body` hoy rechaza un mensaje sin contenido. Con herramientas, un asistente con `tool_calls` y sin texto es válido: va con `"content": null`.
- Un mensaje `tool` va con `role`, `tool_call_id` y `content`.
### 3.2 La petición
 
- `complete_stream` recibe, aparte de los mensajes, un `RequestOptions {tools, replay_reasoning}` opcional, con las herramientas como `ToolSpec` (el esquema es una cadena JSON). Con herramientas manda `tools` y `"tool_choice": "auto"`; sin herramientas, el cuerpo **byte por byte igual que hoy** (prueba con las transcripciones existentes).
- **No** se manda `parallel_tool_calls`: no todos los modelos lo aceptan. Si el modelo pide varias en una respuesta, se ejecutan en orden.
- `tools` y `tool_choice` siguen reservadas en el JSON avanzado de M2 (`kReservedKeys`): nadie las pisa desde Configuración del modelo.
### 3.3 El flujo (SSE)
 
Las llamadas llegan en pedazos dentro de `choices[0].delta.tool_calls`:
 
```jsonc
{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read","arguments":""}}]}}
{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"path\":\"src/ma"}}]}}
{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"in.cpp\"}"}}]}}
{"delta":{},"finish_reason":"tool_calls"}
```
 
- Un acumulador (`ToolCallAccumulator`, función pura y probada sola) junta por `index`: el `id` y el `name` del primer pedazo y los `arguments` concatenados.
- Variantes que hay que aguantar: la llamada completa en un solo pedazo; varias llamadas intercaladas; sin `index` (se toma un `id` nuevo como llamada nueva); `finish_reason` `stop` aunque haya llamadas (manda la presencia de llamadas, no el `finish_reason`); texto y pensamiento en el mismo pedazo que una llamada.
- `CompletionInfo` gana `std::vector<ToolCall> tool_calls`.
- `StreamDelta` gana un tipo `ToolCallStarted` (con índice y nombre), para que la pantalla diga "● Editando tests/…" mientras llegan los argumentos de un `write` largo.
- **Reintentos:** la regla de M3 se extiende: no se reintenta después de entregar cualquier delta, también el de una llamada.
- Un modelo que escribe la llamada como texto (`<tool_call>…` dentro de `content`) **no** se interpreta: la sonda de C0 lo marca como modelo no apto.
### 3.4 El pensamiento (D6)
 
- En el modo código, `decode_openai_chunk` también recuerda la llave en que llegó el pensamiento, y `Message::reasoning` lo guarda con ella.
- Al armar la petición con `replay_reasoning = true`, cada asistente lleva su pensamiento con esa llave. El chat lo deja en `false`: su JSON no cambia.
- Gemini por la capa compatible pide además su `thought_signature` (`extra_content.google`) en las llamadas a herramientas. **Fuera de alcance:** el modo código se prueba con NIM (D4).
### 3.5 Recorte del historial
 
`trim_history` quita **grupos**: un `User` con todo lo que sigue hasta el siguiente `User` (asistente, `tool_calls` y `tool`). Nunca se deja un `tool` sin su llamada ni una llamada sin sus resultados, y el último grupo no se quita ni se parte. En el modo código el recorte es el último recurso; lo normal es compactar (sección 7).
 
### Lo que cambió al implementarlo (C0)

- En vez de `ToolSet`, `RequestOptions {tools, replay_reasoning}` con el esquema como cadena JSON, porque los headers públicos no exponen nlohmann.
- 3.5: el grupo es un `User` con todo lo que sigue hasta el siguiente `User`, no solo "asistente con sus tool", para que tras el sistema siempre siga un `User` y el resultado sin herramientas sea el de antes.
- Sin herramientas en la petición, los `tool_calls` que mande el servidor se ignoran como antes, para que el chat no cambie.
- Ids: uno vacío o repetido (frente al historial o dentro de la respuesta) pasa a `call_moss_<n>` libre, porque hay servidores que repiten ids y la validación rechaza repetidos; `complete_trimmed` los revisa contra el historial completo.
- Con herramientas, el reintento por contexto exige estado HTTP, porque un pedazo de llamada sin nombre no pasa por `on_delta`.
- Una llamada con id vacío en el historial es error `Config`.

### 3.6 La sonda (para elegir modelos)
 
`moss-tools-probe <modelo>` (solo en los presets de desarrollo, como `moss-telegram-probe`) corre cuatro escenarios contra el modelo real con herramientas falsas y escribe una tabla:
 
1. Una llamada simple (`read` de un archivo falso) y la respuesta final.
2. Dos pasos encadenados (`glob` y luego `read` del resultado).
3. Varias llamadas en una respuesta.
4. Recuperación: la herramienta responde un error y el modelo corrige.
Por modelo: si pasó cada escenario, si mandó pensamiento, si exigió el pensamiento de regreso (probando sin él), tiempo y pasos. Con eso eliges 2 o 3 modelos para probar todo lo demás.
 
---
 
## 4. Herramientas
 
Nombres y descripciones en inglés para el modelo (D7); en la pantalla, en español.
 
| Herramienta | En pantalla | Qué hace | Aprobación (modo Preguntar) |
|---|---|---|---|
| `read` | Leer | `{path, offset?, limit?}`. Hasta 2000 líneas, numeradas como `cat -n`; líneas de más de 2000 caracteres se recortan. Un archivo binario (con bytes NUL) da error | No, dentro del proyecto |
| `glob` | Buscar archivos | `{pattern, path?}`. En un repo de git, sobre `git ls-files --cached --others --exclude-standard` (respeta `.gitignore`); si no, recorre la carpeta sin `.git`, `node_modules` ni `build*`. Ordenado por fecha de cambio, máximo 200 | No |
| `grep` | Buscar | `{pattern, path?, glob?, ignore_case?, output_mode: files \| content \| count, context?}`. `rg` si existe (D12), si no la búsqueda propia. Máximo 200 coincidencias | No |
| `edit` | Editar | `{path, old_string, new_string, replace_all?}`. `old_string` tiene que aparecer **exactamente una vez** (o `replace_all`). Respeta los finales de línea del archivo (`\r\n` en Windows) | Sí, con diff |
| `write` | Escribir | `{path, content}`. Crea o reemplaza. Crea las carpetas que falten. Máximo 1 MiB | Sí, con diff (o el archivo nuevo completo) |
| `bash` | Ejecutar | `{command, description, timeout_ms?}` (solo Linux en v1, D2) | Sí, salvo reglas |
| `todo_write` | (lista de tareas) | `{todos: [{content, status, active_form}]}`. Reemplaza la lista completa | No |
| `exit_plan_mode` | (pregunta) | `{plan}`. Solo en el modo Plan: te enseña el plan y pregunta si empezar | Siempre pregunta |
 
**Reglas comunes:**
 
- **Raíz del proyecto:** la carpeta donde arrancó `moss code`. Toda ruta se resuelve contra ella, con los enlaces simbólicos ya resueltos (`weakly_canonical`). Fuera de la raíz, **todo** pregunta, también leer.
- **Leer antes de editar:** `edit` y `write` sobre un archivo existente fallan si no se leyó en esta sesión, o si cambió desde que se leyó (por fecha y tamaño). Es lo que hace Claude Code, y evita que el modelo pise cambios tuyos.
- **Nunca, en ningún modo:** la carpeta de configuración de Moss (`config.json`, `credentials.json`), ni la de datos. `glob` y `grep` se la saltan. Así el modelo no puede leer tus keys.
- **Tope de resultado:** 30 000 caracteres por resultado (la cabeza y la cola, con `[… N líneas omitidas …]` en medio). El modelo puede pedir el resto con `offset` o un `grep` más fino.
- **Sin terminal:** cada herramienta es una función sin FTXUI que recibe `ProjectRoot`, sus argumentos ya validados contra el esquema y un `CancelToken`, y regresa `ToolResult {text, is_error, display}` (`display` es lo que dibuja la pantalla: el diff, el resumen de una línea).
### 4.1 `bash` (C4)
 
- `bash -c "<command>"` con la raíz como carpeta de trabajo, stdin desde `/dev/null` y su propio grupo de procesos (`setpgid`). Esc o el tiempo límite matan **el grupo entero** (SIGTERM, 2 s, SIGKILL): `cmake` y sus compiladores también se mueren.
- Tiempo límite: 2 minutos por defecto, 10 como máximo.
- Salida: stdout y stderr juntos, en el orden en que llegan, con el tope de 30 000 caracteres. La pantalla muestra las últimas líneas mientras corre.
- **Entorno filtrado:** sin `CHAT_API_KEY`, `CHAT_SEARCH_API_KEY` ni ningún `CHAT_*`; con `MOSS_CODE=1`, `PAGER=cat`, `GIT_PAGER=cat`, `GIT_EDITOR=true` y `TERM=dumb` (para que nada se quede esperando una tecla).
- El `cd` no persiste entre llamadas (la descripción para el modelo lo dice).
- Sin procesos en segundo plano en v1.
- Windows: la herramienta no se ofrece al modelo, y las instrucciones de sistema dicen que no hay comandos.
---
 
## 5. Permisos
 
### 5.1 Modos (Shift+Tab, D8)
 
| Modo | Leer y buscar | Editar y escribir | Comandos |
|---|---|---|---|
| **Preguntar** (por defecto) | Libre dentro del proyecto | Pregunta | Pregunta, salvo reglas |
| **Aceptar ediciones** | Libre | Libre dentro del proyecto | Pregunta, salvo reglas |
| **Plan** | Libre | No (el modelo ni siquiera tiene esas herramientas) | No |
| **Sin preguntar** (`--sin-preguntar`) | Libre | Libre | Libre |
 
- En **Plan**, el modelo investiga y termina con `exit_plan_mode`. Tú decides: "Empezar con Aceptar ediciones", "Empezar con Preguntar" o "Seguir planeando".
- **Sin preguntar** sale en rojo en la línea de estado. Las reglas `deny` y la carpeta de Moss siguen bloqueadas.
### 5.2 Reglas (D9)
 
```jsonc
// .moss/settings.json (del proyecto) o .moss/settings.local.json (tuyo)
{"permissions":{
  "allow":["bash(cmake --build:*)","bash(ctest:*)","bash(git status)","edit(tests/**)"],
  "deny": ["bash(git push:*)","read(.env)"]}}
```
 
- `deny` gana sobre `allow`; el local se suma al del proyecto.
- `bash(prefijo:*)` permite cualquier comando que empiece con ese prefijo; `bash(comando)`, exactamente ese.
- **Comandos compuestos:** `a && b`, `a; b`, `a | b` se parten, y cada parte tiene que estar permitida. Si el comando trae algo que el analizador no entiende con seguridad (`$(…)`, comillas invertidas, `eval`, redirecciones fuera del proyecto), **siempre pregunta**.
- La respuesta `[t]` a un comando escribe la regla en `.moss/settings.local.json`, y la primera vez agrega `.moss/settings.local.json` a `.git/info/exclude`.
- `/permisos` muestra las reglas vigentes y deja quitar las locales.
### 5.3 Lo que sabe el modelo
 
Una herramienta rechazada regresa como resultado: `"The user rejected this action."` y, si escribiste un motivo, `" Reason: <motivo>"`. El turno sigue (el modelo puede proponer otra cosa), salvo con Esc, que lo termina.
 
---
 
## 6. Instrucciones y contexto inicial
 
Las instrucciones de sistema del modo código se arman al empezar cada sesión:
 
1. Las fijas de Moss Code (en inglés, D7): cómo usar las herramientas, leer antes de editar, preferir `edit` a `write`, no inventar rutas, ser breve, contestar en el idioma del usuario (español por defecto).
2. El entorno: carpeta, sistema, fecha, si es un repo de git (rama y `git status --short`, recortado), y si hay `bash`.
3. `~/.config/moss/AGENTS.md` (tuyo) y el `AGENTS.md` de la raíz (o `CLAUDE.md`), D5, hasta 40 KiB entre los dos.
Las instrucciones del chat (`system_prompt` de `config.json`) **no** aplican en el modo código.
 
`/init` le pide al modelo que explore el proyecto y escriba un `AGENTS.md` (pasa por la aprobación como cualquier `write`).
 
---
 
## 7. Sesiones y contexto (C5)
 
- **Guardar:** después de **cada paso** (no solo de cada par), atómico, en `…/moss/code/<id>.json` (D11): versión 1, carpeta, modelo, fechas, título, los mensajes completos (con llamadas, resultados recortados y pensamiento con su llave), la lista de tareas y el modo.
- **Reanudar:** `moss code --continuar` abre la última sesión de esa carpeta; `/reanudar` lista las de la carpeta.
- **Uso de tokens:** se pide `stream_options: {include_usage: true}` en el modo código para saber cuánto contexto va. La línea de estado lo muestra en % de la ventana del modelo, que se configura por modelo (`"context_window"` en `"models"`, con un valor por defecto prudente).
- **Compactar:** `/compact` y, al pasar el 80 %, automático: el modelo resume la sesión (qué se pidió, qué se cambió, qué falta, archivos tocados) y la sesión sigue con las instrucciones, el resumen y el último mensaje. Se avisa en pantalla.
- `/clear` empieza una sesión nueva en la misma carpeta.
---
 
## 8. Deshacer
 
- Antes de cada `edit` o `write` se guarda el contenido anterior del archivo (o que no existía).
- `/deshacer` revierte los cambios de archivos del último turno (C3). Rebobinar a cualquier mensaje anterior, como Esc Esc en Claude Code, queda para después.
- Lo que cambió un comando (`bash`) no se puede deshacer; el aviso de `/deshacer` lo dice.
---
 
## 9. La TUI
 
- `parse_command_line` gana `Kind::Code`: `moss code [carpeta] [-p texto] [--continuar] [--sin-preguntar] [--modo plan]`.
- Pantalla de una columna (sin la barra de conversaciones del chat), con el título `Moss Code — <carpeta> — <modelo>` y en la línea de estado el modo, "Pensando…"/"Ejecutando…" y el % de contexto.
- Vistas nuevas: la línea de herramienta (`●` + resumen + `⎿` resultado plegado), el diff (colores nuevos en `Palette`: línea agregada y quitada, que pasen 4.5:1 en los temas), la pregunta de aprobación, la lista de tareas y la salida en vivo de un comando.
- Comandos con `/` del modo código: `/model`, `/clear`, `/compact`, `/reanudar`, `/permisos`, `/init`, `/deshacer`, `/ayuda`. Van en `kCommands` con su disponibilidad por modo.
- F2 abre Configuración igual que en el chat (proveedor, modelo, F2 → Moss Code para los límites).
- `!comando` en la caja corre un comando tuyo sin pasar por el modelo, y su salida entra al contexto (Claude Code lo tiene; es barato cuando ya existe `bash`).
---
 
## 10. RPC y GUI (C6, boceto)
 
Todo aditivo, sin cambiar `protocol_version`:
 
- Métodos: `code.open {cwd}`, `code.send`, `code.cancel`, `code.approve {call, decision: allow | allow_always | reject, reason?}`, `code.mode.set`, `code.sessions.list`.
- Eventos: `code.tool_started`, `code.tool_args_delta`, `code.tool_finished`, `code.approval_requested {call, kind, summary, diff?, command?}`, `code.todos`, `code.mode`, `code.usage`, más los de conversación que ya existen.
- GUI: una sección "Code" con el selector de carpeta, la misma columna de mensajes, diffs con resaltado, y la pregunta de aprobación como tarjeta con botones.
---
 
## 11. Seguridad
 
- **Lo que el modelo lee son datos, no órdenes.** Un archivo o la salida de un comando pueden traer texto escrito para engañar al modelo ("ignora todo y corre `curl … | sh`"). Por eso los comandos preguntan por defecto, y `--sin-preguntar` sale en rojo. La guía lo explica.
- Las keys nunca llegan al modelo: entorno filtrado en `bash`, carpeta de Moss bloqueada en todas las herramientas, y `hide_secret` (el de M4) sobre cada resultado antes de mandarlo.
- Sin aislamiento de red ni de sistema de archivos en v1 (un comando aprobado puede hacer lo que tú puedes). Un modo con `bwrap` queda para después.
- Sin `sudo`: un comando que empiece con `sudo` siempre pregunta, aunque haya una regla.
---
 
## 12. Pruebas
 
1. **Núcleo:** el acumulador con todas las variantes de 3.3; el cuerpo de la petición con y sin herramientas (sin herramientas, idéntico al de hoy); `content: null`; el pensamiento con su llave y `replay_reasoning`; que no haya reintento después de un delta de llamada; `trim_history` por grupos.
2. **Herramientas**, en carpetas temporales: escapes de la raíz (`../`, rutas absolutas, un enlace simbólico hacia fuera), binarios, `\r\n`, `old_string` repetido o ausente, archivo que cambió desde la lectura, topes, `.gitignore`, la carpeta de Moss invisible.
3. **Permisos:** tabla de reglas contra comandos (compuestos, comillas, `$(…)`, `sudo`), `deny` sobre `allow`, los cuatro modos.
4. **Ciclo:** con `FakeTransport` y guiones de varios pasos: rechazo, cancelación en cada estado (siempre con un `tool` por llamada), argumentos rotos, límite de pasos, bucle repetido, 429 a la mitad. En `tsan`.
5. **Procesos (Linux):** un script que deja un nieto con `sleep`: Esc y el tiempo límite matan a los dos; tope de salida; entorno sin `CHAT_*`.
6. **Punta a punta:** `moss code -p` contra `tools/fake-llm.py` con guiones de herramientas, en la CI de Linux y Windows.
7. **TUI:** en tmux con `capture-pane`, como siempre.
8. **Con tus keys:** la sonda (3.6) y una tarea real en un repo de prueba por cada hito.
---
 
## 13. Plan por hitos
 
| Hito | Contenido |
|---|---|
| **C0** (En revisión, PR #64) | Tool calling en el núcleo (3.1–3.5): `Role::Tool`, `ToolCall`, `content: null`, `tools`/`tool_choice`, el acumulador, `CompletionInfo.tool_calls`, `ToolCallStarted`, el pensamiento con su llave, recorte por grupos. `tools/fake-llm.py` con guiones de llamadas. La sonda `moss-tools-probe`. **Sin herramientas reales ni pantalla.** El chat no cambia en nada |
| **C1** | El motor sin pantalla: `code/` con `chatbot_code`, `ProjectRoot`, `read`, `glob`, `grep`, `todo_write`, `AgentLoop` con sus límites y cancelación, las instrucciones con `AGENTS.md`, y `moss code -p` (solo herramientas de lectura). Linux y Windows |
| **C2** | La TUI del modo código, de solo lectura: `moss code`, la vista de herramientas y tareas, la línea de estado, Esc, `/model`, `/clear`, Ctrl+O |
| **C3** | Editar: `edit`, `write`, diffs, la pregunta de aprobación, los modos (Shift+Tab, Plan con `exit_plan_mode`), las reglas de `.moss/`, `/permisos` y `/deshacer` |
| **C4** | Comandos en Linux: `bash` con grupo de procesos, entorno filtrado, reglas de comandos compuestos, salida en vivo, `!comando` |
| **C5** | Sesiones y contexto: guardar después de cada paso, `--continuar` y `/reanudar`, uso de tokens, `/compact` y compactación automática, `/init` |
| **C6** | RPC y GUI (sección 10) |
| **C7** | Comandos en Windows (PowerShell), si hace falta |
 
Cada hito sale con su prompt (`claude/prompt-cN-*.md`), en su rama, con `explorer`, `test-runner` y `reviewer`, como los anteriores. Después de C2 ya se puede usar para explorar código; después de C4, para trabajar de verdad.
 
---
 
## 14. Fuera de alcance de Moss Code v1
 
- Subagentes, MCP, hooks y skills (Claude Code los tiene; Moss, después).
- `web_fetch` y `web_search` como herramientas (la búsqueda de Tavily podría entrar fácil después).
- Imágenes, varias carpetas raíz, procesos en segundo plano, aislamiento con `bwrap`.
- Rebobinar a cualquier mensaje (solo `/deshacer` del último turno).
- Gemini con su `thought_signature`, y modelos que escriben las llamadas como texto.
