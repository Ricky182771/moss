# Guiones para `tools/fake-llm.py`

Con `--script archivo.json`, el servidor falso responde la petición n a
`/chat/completions` con el elemento n de una lista JSON. Agotada la lista,
responde HTTP 500 (`fake-llm: script exhausted`). Un guion inválido hace que el
servidor falle al arrancar (código 2) con el motivo.

```
python3 tools/fake-llm.py --port 8765 --script tests/data/fake-llm/dos-pasos.json \
    --log-requests /tmp/peticiones.jsonl --require-reasoning-replay
```

## Elemento (todos los campos son opcionales)

| Campo | Qué es |
|---|---|
| `content` | Texto del asistente. |
| `reasoning` | Pensamiento. |
| `reasoning_key` | Llave del delta: `"reasoning_content"` (defecto) o `"reasoning"`. |
| `tool_calls` | Lista de `{"id"?, "name", "arguments", "omit_id"?}`. `arguments` es un objeto (se manda como JSON compacto) o una cadena (tal cual, para mandar JSON roto). Sin `id` se genera `call_<n>`; con `"omit_id": true` no se manda. |
| `chunk_size` | Caracteres por pedazo de `content`, `reasoning` y `arguments` (defecto 8). |
| `variant` | `"indexed"` (defecto; varias llamadas intercaladas), `"no_index"` (sin `index`, llamadas una tras otra) o `"single_chunk"` (cada llamada completa en un pedazo). |
| `finish_reason` | Defecto `"tool_calls"` si hay llamadas, si no `"stop"`. |
| `mixed_first_chunk` | `true`: el primer pedazo lleva pensamiento, texto y el inicio de la primera llamada. |

Orden del flujo: pensamiento, texto, llamadas. Sin `"stream": true` en la
petición, responde el JSON completo equivalente.

## Banderas

- `--log-requests archivo.jsonl`: una línea por cuerpo recibido (nunca cabeceras).
- `--require-reasoning-replay`: con `tools`, un asistente con `tool_calls` sin
  `reasoning_content` ni `reasoning` recibe 400 y no gasta un elemento del guion.

## Ejemplos

`dos-pasos.json`, `varias-llamadas.json`, `sin-index.json`, `un-pedazo.json`,
`argumentos-rotos.json`.

`sonda-con-index.json`, `sonda-sin-index.json`, `sonda-un-pedazo.json` y
`sonda-texto.json`: toda la secuencia de peticiones de `moss-tools-probe` para un
modelo (cuatro escenarios y el 2 repetido sin pensamiento de regreso; 13
respuestas, 5 en el de texto). Ver `guias/11-compilacion.md`.

`code-tres-pasos.json`, `code-grep.json` y `code-limite.json`: los de `tools/check-code-e2e.py` (`moss code -p` contra `tests/data/code-project/`; con pensamiento en cada paso con llamadas).
