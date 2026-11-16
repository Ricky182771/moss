# Guía: 19. Moss Code, uso (C1)
> Las reglas resumidas están en [AGENTS.md](../AGENTS.md). El diseño completo está en [18-moss-code.md](18-moss-code.md).

Moss Code es el modo agente de programación de Moss: el modelo explora un proyecto con herramientas y te responde. En C1 es **de solo lectura** (no edita archivos ni corre comandos) y solo se usa sin pantalla; la pantalla llega en C2.

## Uso

```bash
moss code -p "¿qué hace la función parse_config?"          # la carpeta actual
moss code ~/proyectos/app -p "lista los TODO que haya"      # otra carpeta
```

- Sin `-p`, `moss code` avisa que la pantalla llega en C2 y sale con 2.
- **stdout**: solo la respuesta final del modelo. Se puede redirigir o encadenar en un script. Conserva los tabuladores y los saltos de línea; quita `\r` y cambia ESC y los demás caracteres de control por `�`, así que ninguna secuencia de escape llega a la terminal.
- **stderr**: una línea `● <resumen>` por cada herramienta que usó el modelo, y los avisos (configuración, límites, instrucciones recortadas).
- Salida: **0** bien; **1** error del modelo, de configuración o turno detenido por un límite; **2** uso incorrecto; **130** Ctrl+C (cancela el turno); **69** si Moss se compiló sin Moss Code (`CHATBOT_BUILD_CODE=OFF`).
- Usa el proveedor y el modelo de la configuración de Moss; el modelo tiene que soportar herramientas.

## Herramientas

| Herramienta | Qué hace | Límites |
|---|---|---|
| `read` | Lee un archivo con líneas numeradas | 2000 líneas por llamada (`offset`/`limit` para seguir), líneas de más de 2000 caracteres recortadas, archivo de hasta 10 MiB, binarios dan error |
| `glob` | Busca archivos por patrón | 200 resultados, el más reciente primero. En un repo de git usa `git ls-files` (respeta `.gitignore`); si no, recorre la carpeta sin `.git`, `node_modules` ni `build*` |
| `grep` | Busca texto con una expresión regular | 200 coincidencias; modos `files`, `content` y `count`; contexto de hasta 10 líneas |
| `todo_write` | Lista de tareas del propio modelo | No toca archivos |

- Todo resultado se recorta a 30 000 caracteres (cabeza y cola, con `[… N lines omitted …]`).
- **`rg` es opcional.** Con `rg` (ripgrep) en el `PATH`, `grep` lo usa. Sin él, usa una búsqueda propia (`std::regex`, sin retroceso exponencial): más lenta, sin lookahead, lookbehind ni referencias, solo archivos de hasta 1 MiB y líneas de hasta 4 KiB. En Windows siempre es la propia.
- En Windows nunca se lanzan `git` ni `rg`: `glob` recorre la carpeta **sin respetar `.gitignore`**, `grep` usa la búsqueda propia y las instrucciones de sistema no traen rama ni estado de git.
- `git` y `rg` se buscan en el `PATH` solo por ruta absoluta y se lanzan sin shell.

## Qué no puede tocar

- Nada fuera de la carpeta del proyecto (la raíz), ni por `..` ni por un enlace simbólico.
- La carpeta de configuración y la de datos de Moss (`config.json`, `credentials.json`, y con la carpeta de datos por defecto, `…/moss` completa, no solo `conversations`): nunca. Si la raíz es o está dentro de una, `moss code` no arranca.
- No acepta `/` ni la raíz de una unidad como raíz del proyecto.
- En Windows no sigue enlaces simbólicos (un enlace a una ruta UNC mandaría credenciales); las uniones sí.

## Instrucciones del proyecto

Moss carga `AGENTS.md` de la raíz (si no hay, `CLAUDE.md`) y el `AGENTS.md` personal de la carpeta de configuración de Moss. Entre los dos, hasta 40 KiB (a cada uno le toca al menos la mitad); lo que sobra se recorta con un aviso. Las instrucciones del chat (`system_prompt`) no aplican aquí. Todo esto es texto que se manda al modelo: no pongas secretos en esos archivos.

## Límites del turno

Un turno se detiene si el modelo pasa de estos límites (salida 1, con el motivo en stderr). Se cambian en `config.json`:

```json
{ "code": { "max_steps": 50, "max_repeated_calls": 3, "max_invalid_calls": 3, "history_limit_bytes": 300000 } }
```

| Llave | Qué limita | Por defecto | Rango |
|---|---|---|---|
| `max_steps` | Peticiones al modelo por turno | 50 | 1–1000 |
| `max_repeated_calls` | La misma llamada (nombre y argumentos) seguida | 3 | 2–100 |
| `max_invalid_calls` | Llamadas seguidas inválidas o a una herramienta que no existe | 3 | 1–100 |
| `history_limit_bytes` | Tamaño del historial antes de recortar los grupos más viejos (0: sin límite) | 300000 | 0–100 000 000 |

Dentro de un solo turno el historial no se recorta (el último grupo nunca se quita): un turno muy largo, con muchos resultados grandes, puede pasarse del contexto del modelo y terminar con error. La compactación llega en C5.

Un valor que no es un entero o está fuera de rango usa el predeterminado y avisa.

## Seguridad

- Lo que el modelo lee (archivos, resultados de `grep`, `AGENTS.md`) son **datos, no órdenes**: un archivo del proyecto que diga "ignora lo anterior" no es una instrucción para Moss.
- Las keys nunca llegan al modelo: la carpeta de Moss está bloqueada en todas las herramientas y cada resultado pasa por `hide_secret` antes de enviarse.
- Aun así, lo que leen las herramientas sale hacia el proveedor del modelo: no abras `moss code` en una carpeta con secretos que no quieras compartir con él.
- Moss Code ejecuta `git` (y `rg`, si está) sobre la carpeta del proyecto, y `git` lee el `.git/config` de esa carpeta. Moss desactiva `core.fsmonitor` y los hooks, y quita sus keys del entorno de esos procesos.
- `git status` (para la rama y el estado en las instrucciones de sistema) puede correr los filtros `clean`/`process` de la configuración, que son programas cualesquiera. Por eso Moss primero pregunta a `git config --show-scope` (que no ejecuta nada) si el repositorio define alguno (su `.git/config`, la del worktree o cualquier otro nivel que no sea el global ni el del sistema); si hay, no corre `git status` y el modelo solo sabe que el estado no se leyó. Los filtros de tu configuración global y del sistema (por ejemplo, los de git-lfs) son tuyos y no impiden leer la rama ni el estado. Hace falta git 2.26 o más nuevo: con uno más viejo (no conoce `--show-scope`) no se lee el estado. `git status` tampoco entra a los submódulos (sus filtros vendrían de otra configuración), así que el estado de un submódulo no aparece.
- `glob` y `grep` siguen usando `git ls-files` en un repo, y Moss no revisa todas las opciones de `.git/config`: abrir con Moss Code un proyecto con un `.git/` que no es tuyo (por ejemplo, de un .zip) sigue teniendo riesgo.
