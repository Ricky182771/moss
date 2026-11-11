# Configurar Moss

Cómo configurar Moss como usuario: la pantalla de configuración (F2 en la terminal, Configuración en la ventana), los archivos y las variables de entorno. Las reglas exactas (precedencia, validación, formatos de archivo) están en [07-configuracion.md](07-configuracion.md); aquí se enlazan en vez de repetirse.

## Proveedor y modelo

La forma más fácil es la pantalla de configuración: ábrela con **F2** o con la fila `⚙ Configuración` de la barra. Si al arrancar falta la key o el modelo, Moss la abre solo con el aviso `Configura un proveedor para empezar.` (mientras tanto, la caja de entrada no envía nada). Ahí eliges el proveedor, escribes la API key (se ve como `•••`) y eliges el modelo de la lista que devuelve el servidor (`GET {URL base}/models`). Con el filtro, ↑/↓ mueven el resaltado y Enter elige; si la lista falla (por ejemplo, Anthropic no documenta ese endpoint), el error aparece en la lista y puedes escribir el modelo a mano. Tab y Shift+Tab cambian de campo; Guardar aplica los cambios sin reiniciar y Esc o Cancelar cierran sin guardar (si hay cambios, pregunta `¿Descartar los cambios? (s/n)`: solo **s** o **n** la contestan y **Esc** la cancela). No se abre mientras hay una respuesta en curso.

El modelo y el proveedor también se pueden cambiar desde la caja con `/model` (ver [Cambiar de modelo con /model](uso.md#cambiar-de-modelo-con-model)), entre los proveedores que ya tienen key guardada.

| Proveedor | URL base | Lista de modelos |
|---|---|---|
| NVIDIA NIM | `https://integrate.api.nvidia.com/v1` | sí |
| OpenAI | `https://api.openai.com/v1` | sí |
| Google Gemini | `https://generativelanguage.googleapis.com/v1beta/openai` | sí |
| Anthropic (Claude) | `https://api.anthropic.com/v1` | no documentada: escribe el modelo |
| Groq | `https://api.groq.com/openai/v1` | no confirmada |
| OpenRouter | `https://openrouter.ai/api/v1` | sí |
| Mistral | `https://api.mistral.ai/v1` | sí |
| DeepSeek | `https://api.deepseek.com` | sí |
| Together AI | `https://api.together.ai/v1` | sí |
| xAI (Grok) | `https://api.x.ai/v1` | no confirmada |
| Cerebras | `https://api.cerebras.ai/v1` | no confirmada |
| Ollama (local) | `http://localhost:11434/v1` | sí (sin key) |

Después de la tabla vienen tus [endpoints personalizados](#endpoints-personalizados) y, al final, `Agregar endpoint…`.

"No confirmada" no quiere decir que falle: Moss la pide igual y, si falla, te deja escribir el modelo.

## Endpoints personalizados

Para un servidor que no está en la tabla (un gateway como OmniRoute, LiteLLM, vLLM, llama.cpp…), da de alta un endpoint en **F2 → Proveedor de IA**:

1. En **Proveedor**, elige `Agregar endpoint…` (la última fila). El foco pasa a **Nombre**.
2. Escribe un **Nombre** (hasta 40 caracteres, sin repetir), la **URL base** y, si el servidor la pide, la **API key**.
3. Pulsa **Actualizar** para pedir la lista de modelos (o escribe el modelo a mano) y **Guardar**. El endpoint queda en uso.

Puedes tener hasta 20, guardados todos a la vez en `config.json` (`"endpoints"`), y cambiar entre ellos igual que entre los de la tabla, en F2 o con [`/model`](uso.md#cambiar-de-modelo-con-model). Cada uno tiene un **id** que Moss saca del nombre al crearlo (`Servidor del lab` → `servidor-del-lab`; si choca, `-2`, `-3`…) y que no cambia nunca: en la lista se ve atenuado junto al nombre, y es lo que escribes en `/model`. Su key se guarda en `credentials.json` como `custom:<id>`, así que cambiar el nombre o la URL no la pierde.

Con un endpoint elegido, el botón **[Borrar endpoint]** lo quita junto con su key, después de preguntar `¿Borrar el endpoint <nombre>? (s/n)`. El que está en uso no se puede borrar (el botón se ve atenuado): cámbiate antes a otro proveedor.

```json
{
    "provider": "custom:omniroute",
    "model": "qwen3",
    "endpoints": [
        {"id": "omniroute", "name": "OmniRoute", "base_url": "https://mi-gateway.example/v1"},
        {"id": "lab", "name": "Servidor del lab", "base_url": "http://localhost:8000/v1"}
    ]
}
```

Si editas `"endpoints"` a mano y una entrada no sirve, Moss la ignora con un aviso al arrancar y usa las demás; al guardar desde F2 se quita. Qué hace válida una entrada (id, nombre, URL): [07-configuracion.md](07-configuracion.md). Si `"provider"` nombra un endpoint que no existe, o un proveedor que no está en la tabla, Moss abre F2 con el error para que elijas otro.

**URL base de un endpoint:** normalmente termina en `/v1` (`https://mi-servidor/v1`, no `https://mi-servidor`). Moss no sigue redirecciones: si el servidor responde con una (301, 302…), el error dice a dónde redirige y, si puede, qué URL base usar.

**Dónde se guarda la key:** en `~/.config/moss/credentials.json` (o `$XDG_CONFIG_HOME/moss/credentials.json`; en Windows, `%APPDATA%\moss\credentials.json`; en [modo portable](#modo-portable), `datos/credentials.json` junto al ejecutable), una por proveedor (la de un endpoint, como `custom:<id>`). El archivo se escribe con permisos 0600 y su carpeta con 0700. Si alguien le abre los permisos (grupo u otros), Moss no lo lee y te pide correr `chmod 600` sobre él (en Windows no: ahí lo protege la carpeta de tu perfil). Al guardar, una key que empieza con `$`, trae espacios o saltos de línea, o mide menos de 20 caracteres no se guarda: suele ser el nombre de una variable (`$NIMKEY`) o una key cortada al copiarla. La key nunca va en `config.json`, en los volcados de depuración ni en las conversaciones guardadas. No copies `credentials.json` al repositorio (está en `.gitignore`). Las reglas del archivo (formato, permisos, Windows): [07-configuracion.md](07-configuracion.md).

**Servidor local (Ollama, llama.cpp…):** elige `Ollama (local)` o agrega un endpoint con una URL como `http://localhost:8080/v1`. Solo se acepta `http://` para `localhost`, `127.0.0.1` o `[::1]` (cualquier puerto), y ahí la key es opcional; cualquier otro servidor necesita `https://`, para que la key y la conversación no viajen sin cifrar.

**Variables de entorno:** también puedes configurar todo sin la pantalla:

```bash
export CHAT_API_KEY="tu-key-de-nvidia"
export CHAT_MODEL="nvidia/nemotron-3-super-120b-a12b"   # solo es un ejemplo
```

No hay modelo por defecto: los modelos se retiran con el tiempo. El del ejemplo fue el más rápido y constante en las pruebas del equipo contra NIM, pero no es un valor por defecto.

Lo que guarda la pantalla va a `~/.config/moss/config.json` (o `$XDG_CONFIG_HOME/moss/config.json`; en Windows, `%APPDATA%\moss\config.json`; en modo portable, `datos/config.json`), que también puedes editar a mano. Al guardar se conservan las demás llaves del archivo. Nunca lleva la key:

```json
{
    "provider": "nvidia",
    "base_url": "https://integrate.api.nvidia.com/v1",
    "model": "nvidia/nemotron-3-super-120b-a12b",
    "timeout_seconds": 120,
    "history_limit": 32000,
    "appearance": { "theme": "catppuccin-mocha", "background": "theme" },
    "system_prompt": "Eres un asistente útil. Responde en español, de forma clara y concisa."
}
```

Las variables de entorno (`CHAT_API_KEY`, `CHAT_BASE_URL`, `CHAT_MODEL`, `CHAT_TIMEOUT`, `CHAT_HISTORY_LIMIT`) tienen prioridad sobre lo guardado. En la pantalla, un campo definido por una de ellas muestra su valor con `(definido por CHAT_…)` y no se puede editar (la key, siempre enmascarada: solo sus últimos 4 caracteres).

`CHAT_HISTORY_LIMIT` (o `history_limit` en el archivo) limita cuánto historial se envía en cada mensaje. Se mide en bytes del texto, más o menos un carácter por byte (los acentos y la ñ cuentan doble). El valor por defecto es 32000; `0` significa sin límite. Si la conversación pasa del límite, se omiten los mensajes más antiguos y la línea de estado lo avisa. Si el servidor rechaza la petición por demasiado larga, Moss recorta el historial a la mitad (del límite o del tamaño real, lo que sea menor) y reintenta una vez.

## Colores y accesibilidad

En la configuración, la categoría **Colores y Accesibilidad** elige el tema, el fondo y los símbolos; se aplican al guardar, sin reiniciar, y se guardan en `config.json` como `"appearance"`.

- **Tema:** `Catppuccin Mocha` (por defecto) usa sus propios colores RGB, iguales en cualquier terminal. `De la terminal` usa la paleta de 16 colores de tu terminal, como antes de los temas.
- **Fondo:** `Del tema` pinta el fondo del tema en toda la pantalla; `Transparente` deja el fondo y el color de texto de tu terminal y solo colorea los acentos (encabezados, código, enlaces…). Con `De la terminal` no aplica.
- **Símbolos:** `Unicode` usa los símbolos de siempre (`⚙`, `✓`, `⚠`, `☑`, esquinas redondeadas…). `Sencillos` cambia los que no tienen las fuentes de la consola de Windows 10 (Consolas y Lucida Console), que ahí saldrían como cuadritos, por otros del mismo ancho: `⚙` → `*`, `✓` → `√`, `⚠` → `!`, `☑`/`☐` → `[x]`/`[ ]`, `▴`/`▾` → `^`/`v`, `▍` → `▌`, `●` (lo elegido) → `•`, las viñetas `◦`/`▪` de los niveles 2 y 3 → `o`/`-`, esquinas redondeadas → rectas y la barra de desplazamiento con bloques (`█`, `▀`, `▄`). La lista sale del `cmap` de `consola.ttf` y `lucon.ttf`; para revisarla otra vez: `powershell -ExecutionPolicy Bypass -File tools\check_glyphs.ps1`. Los acentos, la ñ y el texto de las respuestas no cambian. `Automático` (por defecto) usa `Sencillos` solo en la consola de Windows 10 (CMD o PowerShell sin Windows Terminal; ni `WT_SESSION` ni `TERM_PROGRAM` definidas) y `Unicode` en todo lo demás, también en mintty o la terminal de VS Code. En `config.json`: `"symbols": "auto"`, `"unicode"` o `"simple"`.

Si `config.json` nombra un tema, un fondo o unos símbolos que no existen, se usa el valor por defecto y la línea de estado lo avisa.

En todas las listas, la fila del cursor se marca con el color de selección (o invertida con `De la terminal`) solo mientras la lista tiene el foco, y `●` (`•` con símbolos sencillos) marca lo elegido. El campo o botón con el foco lleva su etiqueta con el color de selección.

Si tu terminal no muestra colores de 24 bits, se aproximan a 256 o 16. Con `NO_COLOR` definida y no vacía se usa automáticamente `De la terminal`, con fondo transparente y cursor invertido. Tema y fondo aparecen deshabilitados con la nota `Desactivado por NO_COLOR`; la configuración guardada se conserva. Los símbolos se eligen igual con `NO_COLOR`.

## Configuración del modelo

La categoría **Configuración del modelo** de F2 tiene tres pestañas arriba, con el modelo en uso en el título (`Configuración del modelo — <modelo> (<proveedor>)`). Con el foco en la barra de pestañas, ← y → cambian de pestaña (también con un clic); Tab y Shift+Tab siguen moviendo el foco entre campos, como en el resto de F2.

- **Instrucciones:** el mensaje de sistema, para todos los modelos (abajo).
- **Parámetros:** los ajustes del modelo en uso, guardados **por proveedor y modelo**: Razonamiento (Predeterminado · Apagado · Bajo · Medio · Alto), Temperatura, Top P, Top K, Máx. tokens, Penalización por frecuencia y por presencia, y Semilla. Cada campo empieza vacío, en **Predeterminado**: lo que no tocas no se manda, y el modelo usa su valor de siempre. Junto a cada campo está su rango, o el error si lo que escribiste no sirve. La ayuda del razonamiento dice qué hace cada nivel con tu proveedor (en NIM, Bajo, Medio y Alto solo lo encienden). **Restaurar todo** regresa todo a Predeterminado.
- **Avanzado:** un objeto JSON que se mezcla al final del cuerpo de la petición (JSON Merge Patch): sus llaves ganan sobre el formulario, los objetos se mezclan por dentro y `null` quita una llave. Sirve para lo que el formulario no cubre, como `{"chat_template_kwargs": {"thinking": true}}` en algunos modelos de NIM. `model`, `messages`, `stream`, `stream_options`, `tools` y `tool_choice` los pone Moss y no se pueden cambiar. Abajo, la **vista previa** muestra el JSON exacto que se mandaría (sin la conversación ni la key), y se actualiza al escribir.

Los ajustes se guardan en `config.json` bajo `"models"` y valen desde el siguiente mensaje. Al cambiar de modelo (F2 o `/model`) se usan los del modelo nuevo; con `CHAT_MODEL`, los de ese modelo. Si el servidor rechaza la petición con un 400 y llevaba ajustes, el error dice `Puede ser un ajuste de <modelo>: revísalo en Configuración del modelo.` Sin modelo elegido, Parámetros y Avanzado piden elegir uno.

```json
{
    "models": {
        "nvidia": {
            "z-ai/glm5": {"reasoning": "high", "temperature": 0.6,
                          "extra": {"chat_template_kwargs": {"clear_thinking": false}}}
        }
    }
}
```

## Instrucciones del sistema

En la configuración, **Configuración del modelo → Instrucciones** edita el mensaje de sistema que se manda al principio de cada conversación (también de las que abres desde la barra), igual para todos los modelos. El campo ajusta el texto al ancho como la caja de la conversación y usa el alto que deja la sección (con barra y rueda si no cabe); PgUp y PgDn mueven el cursor y la vista una altura del campo. Enter agrega un salto de línea; Tab y Shift+Tab cambian de campo. **Restaurar predeterminado** vuelve a poner las instrucciones de fábrica. Al guardar se aplican sin reiniciar y sin perder la conversación abierta: valen desde el siguiente mensaje.

Se guardan en `config.json` como `"system_prompt"`:

```json
{
    "model": "nvidia/nemotron-3-super-120b-a12b",
    "system_prompt": "Eres un tutor de matemáticas.\nExplica paso a paso y en español."
}
```

- **Sin la llave** (o tras «Restaurar predeterminado» y Guardar, que la borra): se usan las predeterminadas, `Eres un asistente útil. Responde en español, de forma clara y concisa.`
- **Cadena vacía (`""`)**: no se manda ningún mensaje de sistema. Sirve con modelos locales que rechazan el rol `system`.
- Si la llave no es una cadena, se usan las predeterminadas y la línea de estado lo avisa.

Al guardar, las instrucciones pueden medir como máximo 8000 bytes (el contador `N / 8000 bytes` está bajo el campo), deben ser UTF-8 válido y no pueden tener caracteres de control salvo saltos de línea y tabuladores (un tabulador escrito a mano en `config.json` se ve en el campo como 4 espacios y se guarda tal cual); si no cumplen, no se guardan y la pantalla dice por qué. El recorte del historial nunca quita las instrucciones: si miden lo mismo que `history_limit` o más, se guardan igual, pero la pantalla avisa que dejarían poco o nada de espacio para la conversación. No hay variable de entorno para las instrucciones. Las conversaciones guardadas no las incluyen.

Nuestro recorte cuenta bytes, no tokens, así que una petición todavía puede pasar de la ventana del modelo, y lo que haga entonces el servidor no depende de nosotros (revisado en octubre de 2026). **NVIDIA NIM**: su [referencia de la API](https://docs.nvidia.com/nim/large-language-models/latest/reference/api-reference.html) no documenta ningún recorte y remite a vLLM, donde recortar es opcional (`truncate_prompt_tokens`, que no mandamos) y [se queda con los últimos tokens](https://docs.vllm.ai/en/stable/api/vllm/renderers/params/), así que puede perder las instrucciones; si el servidor rechaza la petición con un 400, la app reintenta una vez con la mitad del historial. **OpenRouter**: la compresión que antes se llamaba «middle-out» (hoy el plugin `context-compression`) [quita o recorta mensajes de en medio](https://openrouter.ai/docs/guides/features/message-transforms) y viene activada en los modelos de 8192 tokens o menos; la documentación no dice si conserva el mensaje de sistema. **Ollama**: su [página de contexto](https://docs.ollama.com/context-length) no dice qué pasa; en el [código](https://github.com/ollama/ollama/blob/main/server/prompt.go) quita mensajes desde el principio hasta caber en `num_ctx`, conserva los de sistema y el último, y lo hace por defecto, sin avisar. **llama.cpp server**: el [README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md) no lo dice; sus [pruebas](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/tests/unit/test_ctx_shift.py) muestran que rechaza el prompt con el error «exceeds the available context size» (el desplazamiento de contexto, apagado por defecto, solo actúa al generar).

## Búsqueda web

Escribe `/buscar` seguido de tu pregunta (por ejemplo, `/buscar quién ganó el último partido del América`). Moss consulta [Tavily](https://tavily.com), le pasa al modelo los resultados como datos y el modelo responde citando cada dato con `[n]`. Debajo de la respuesta aparece el bloque **Fuentes** con `[n] título (URL)`; ese bloque sale de la búsqueda, no del texto del modelo (que podría inventar direcciones), y Shift+clic sobre un enlace lo abre. Funciona con cualquier modelo.

- Solo `/buscar` en minúsculas, seguido de un espacio y una consulta. `/buscar` solo muestra cómo se usa; cualquier otro texto que empiece con `/` se envía como un mensaje normal.
- Mientras busca, la línea de estado dice `Buscando en la web…`. Esc cancela igual durante la búsqueda que durante la respuesta.
- Si la búsqueda falla o no trae resultados, no se pregunta al modelo: aparece el error (o el aviso `La búsqueda no encontró resultados.`) y el texto regresa a la caja.
- Se buscan 5 resultados en español de México con búsqueda segura (`safe_search`) siempre activa; no se puede desactivar.
- Las conversaciones guardadas conservan los resultados: al abrirlas de nuevo, las fuentes siguen ahí y el modelo vuelve a recibir los mismos datos.

**Cómo sacar la key:** entra a [app.tavily.com](https://app.tavily.com), crea una cuenta (no pide tarjeta) y copia la API key de tu panel. Pégala en la configuración (F2), categoría **Búsqueda web**, y guarda. Se guarda en `credentials.json` con la llave `"search:tavily"`, junto a las demás keys y con los mismos permisos; nunca en `config.json`. También puedes usar la variable `CHAT_SEARCH_API_KEY`, que tiene prioridad y bloquea el campo. Sin key, `/buscar` no se envía y la línea de estado dice `Configura la key de búsqueda en Configuración (F2) → Búsqueda web`.

**Límites del plan gratuito:** 1,000 créditos al mes; cada `/buscar` gasta 1 (búsqueda básica). Al agotarlos aparece `Se agotaron las búsquedas del plan de Tavily`. No hay reintentos automáticos: vuelve a enviar con Enter cuando quieras.

**Privacidad:** a Tavily solo se envía la consulta que escribes después de `/buscar`, nunca la conversación. Lo que llega de las páginas se le pasa al modelo marcado como datos, con la instrucción de ignorar cualquier orden que venga dentro.

## Conversaciones guardadas

Cada conversación se guarda sola después de cada respuesta completa, en un archivo `.json` por conversación dentro de:

- `CHAT_DATA_DIR`, si la defines;
- si no, en [modo portable](#modo-portable), `datos/conversations` junto al ejecutable;
- si no, `$XDG_DATA_HOME/moss/conversations` (solo si `XDG_DATA_HOME` es una ruta absoluta);
- si no, `~/.local/share/moss/conversations`;
- en Windows, si no defines `CHAT_DATA_DIR`, `%LOCALAPPDATA%\moss\conversations`.

Los archivos contienen **el texto completo de tus conversaciones**. Por eso la carpeta se crea con permisos 0700 y los archivos con 0600 (solo tu usuario puede leerlos). Para borrar una conversación, usa Supr (o el clic derecho → Borrar) en la barra de conversaciones (Ctrl+O); para borrarlas todas, borra la carpeta. Si un archivo está dañado o es de una versión más nueva del programa, aparece en la barra como ilegible y el programa nunca lo modifica.

## Modo portable

Si junto al ejecutable hay un archivo llamado `moss.portable` (puede estar vacío; el ZIP de Windows trae `moss.portable.ejemplo` para renombrarlo), la configuración (`config.json`), la key (`credentials.json`) y las conversaciones (`conversations/`) van a la carpeta `datos/`, también junto al ejecutable, en lugar de la carpeta de tu usuario. Sirve para las PC que se restauran al reiniciar (Deep Freeze y similares), donde se perdería lo de `%APPDATA%`, y para llevar Moss en una memoria USB. Funciona en Windows y en Linux.

- Las descargas de `/guardar` y `/exportar` no cambian: siguen en Descargas (o en `CHAT_DOWNLOAD_DIR`).
- Las variables `CHAT_*` siguen ganando: `CHAT_API_KEY` sobre la key de `datos/credentials.json`, `CHAT_DATA_DIR` sobre `datos/conversations`, etc. `XDG_*`, `HOME`, `APPDATA` y `LOCALAPPDATA` ya no cuentan.
- En la configuración (F2), al pie de **Proveedor de IA**, una línea atenuada dice `Modo portable: datos en <ruta>`. Sin modo portable no aparece.
- Al arrancar, Moss crea `datos/` si falta y prueba que se pueda escribir ahí. Si no se puede (la carpeta del programa es de solo lectura, como `Archivos de programa`, o un disco protegido), sale con un error que dice la ruta y la causa, antes de abrir la pantalla; nunca usa en silencio la carpeta de tu usuario, que confundiría a quien cree estar en modo portable.
- El archivo se revisa una vez, al arrancar: crearlo o borrarlo con Moss abierto no cambia nada hasta la siguiente vez.

**La key queda en texto plano junto al ejecutable.** En modo portable, `datos/credentials.json` no está protegido por la carpeta de tu perfil: cualquiera que pueda abrir la carpeta del programa (otra cuenta de la PC, quien tome la memoria USB) puede leer la key y usarla. En Linux el archivo sigue en 0600 y `datos/` en 0700 (y la regla del `chmod 600` sigue valiendo); por eso en una memoria USB con FAT32 o exFAT, donde esos permisos no existen, en Linux Moss no lee `credentials.json`: ahí usa `CHAT_API_KEY` o una partición ext4. La defensa real no es esconder el archivo sino limitar el daño: **una key por PC con tope de gasto diario**. En OpenRouter, por ejemplo, cada key puede tener un límite de crédito (`limit`) que se reinicia cada día (`limit_reset: daily`); así una key copiada gasta, como mucho, el tope de un día y se revoca sin tocar las de las demás PC.

## Depuración

`CHAT_DEBUG_SSE=/ruta/archivo` (solo por variable de entorno) guarda en ese archivo, por cada intento de petición (tanto las respuestas por fragmentos, que usa la interfaz, como las completas, que usa `tools/smoke` sin `--stream`), la fecha, el modelo, el número de intento, el estado HTTP y el cuerpo crudo de la respuesta. Nunca guarda la key, las cabeceras ni lo que tú envías, pero **sí contiene las respuestas del modelo, es decir, la conversación**. Úsalo solo para diagnosticar un problema y borra el archivo después. Si no se puede escribir, Moss sigue funcionando sin avisar.

```bash
CHAT_DEBUG_SSE=/tmp/chat-debug.txt ./build/release/cli/moss
```

`CHAT_DEBUG_KEYS=/ruta/archivo` (solo por variable de entorno) agrega al archivo una línea por cada tecla, clic o evento que recibe la interfaz: la hora con milisegundos; el tipo (`special` con los bytes en hex, `character` con **solo la longitud en bytes**, `mouse` con botón, movimiento y posición, o `custom`); si la configuración está abierta, si la barra de conversaciones tiene el foco, a dónde iría un pegado (`target`) y si hay un pegado entre corchetes abierto. Con la configuración abierta, también qué campo tiene el foco. Tras Ctrl+V, Ctrl+Shift+V o Shift+Insert, una línea `paste-shortcut` con cuántos bytes tenía el portapapeles y si se pegó. Nunca escribe el texto que tecleas o pegas (por ahí pasa la API key). Sirve para ver qué secuencia manda tu terminal. Si no se puede escribir, Moss sigue sin avisar.

```bat
:: CMD
set CHAT_DEBUG_KEYS=%TEMP%\chat-keys.txt
build\release\cli\moss.exe
```
