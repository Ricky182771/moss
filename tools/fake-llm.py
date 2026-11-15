#!/usr/bin/env python3
"""Servidor falso compatible con OpenAI para probar Moss sin gastar.

Solo biblioteca estándar. Escucha en 127.0.0.1 (Moss acepta HTTP solo en
localhost) y responde:

  GET  /v1/models            {"data": [{"id": "fake"}, ...]}
  POST /v1/chat/completions  con "stream": true (SSE) o sin él

La respuesta de ejemplo trae Markdown y un bloque de código, partida en trozos
con retardo.

Uso (comando completo en gui/README.md):
  python3 tools/fake-llm.py [--port 8765] [--reasoning] [--slow] [--status 429]
                            [--tls CERT KEY] [--script ARCHIVO.json]
                            [--log-requests ARCHIVO.jsonl]
                            [--require-reasoning-replay]

Con --tls responde por HTTPS con TLS 1.2 como máximo (lo que tiene Schannel en
Windows 10): lo usa la CI de Windows (tools/check-rpc-https.ps1).

Con --script, cada petición a /chat/completions recibe el siguiente elemento de
la lista JSON del archivo (formato en tests/data/fake-llm/README.md): texto,
pensamiento y llamadas a herramientas. Con --log-requests se guarda el cuerpo
de cada petición (nunca cabeceras) y con --require-reasoning-replay se exige que
los asistentes con llamadas devuelvan su pensamiento.
"""

import argparse
import json
import ssl
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ANSWER = """Aquí va un **selection sort** en C++:

```cpp
void selection_sort(std::vector<int>& v) {
    for (size_t i = 0; i + 1 < v.size(); ++i) {
        size_t min = i;
        for (size_t j = i + 1; j < v.size(); ++j)
            if (v[j] < v[min]) min = j;
        std::swap(v[i], v[min]);
    }
}
```

- El ciclo de afuera marca dónde empieza la parte sin ordenar.
- El de adentro busca el mínimo de esa parte.
- `std::swap` lo pone en su lugar.

Es *O(n²)* en todos los casos. Más en [cppreference](https://en.cppreference.com/w/cpp/algorithm/swap).
"""

REASONING = (
    "Piden un ejemplo corto. Conviene una función que reciba un vector por "
    "referencia, dos ciclos y std::swap, y luego tres viñetas, una por pieza."
)

MODELS = ["fake", "fake-pensante"]


def chunks(text, size):
    """Trozos de unos `size` caracteres, como llegan de un modelo real."""
    return [text[i : i + size] for i in range(0, len(text), size)]


SCRIPT_KEYS = {"content", "reasoning", "reasoning_key", "tool_calls", "chunk_size",
               "variant", "finish_reason", "mixed_first_chunk"}
CALL_KEYS = {"id", "name", "arguments", "omit_id"}
VARIANTS = ("indexed", "no_index", "single_chunk")
REASONING_KEYS = ("reasoning_content", "reasoning")
REPLAY_ERROR = "The reasoning_content in the thinking mode must be passed back to the API"


def load_script(path):
    """Lee y valida el guion; ValueError con un mensaje claro si algo no cuadra."""
    try:
        with open(path, encoding="utf-8") as f:
            steps = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        raise ValueError("no se pudo leer %s: %s" % (path, e))
    if not isinstance(steps, list):
        raise ValueError("el guion debe ser una lista JSON")

    def check(where, obj, key, types, allowed=None):
        value = obj.get(key)
        if value is None and key not in obj:
            return
        if isinstance(value, bool) and bool not in types or not isinstance(value, types):
            raise ValueError("%s: \"%s\" tiene el tipo equivocado" % (where, key))
        if allowed is not None and value not in allowed:
            raise ValueError("%s: \"%s\" desconocido (%r)" % (where, key, value))

    for i, step in enumerate(steps):
        where = "elemento %d" % i
        if not isinstance(step, dict):
            raise ValueError("%s: debe ser un objeto" % where)
        for key in step.keys() - SCRIPT_KEYS:
            raise ValueError("%s: llave desconocida \"%s\"" % (where, key))
        for key in ("content", "reasoning", "finish_reason"):
            check(where, step, key, (str,))
        check(where, step, "reasoning_key", (str,), REASONING_KEYS)
        check(where, step, "variant", (str,), VARIANTS)
        check(where, step, "mixed_first_chunk", (bool,))
        check(where, step, "chunk_size", (int,))
        if step.get("chunk_size", 1) < 1:
            raise ValueError("%s: \"chunk_size\" debe ser >= 1" % where)
        check(where, step, "tool_calls", (list,))
        for j, call in enumerate(step.get("tool_calls", [])):
            cwhere = "%s, llamada %d" % (where, j)
            if not isinstance(call, dict):
                raise ValueError("%s: debe ser un objeto" % cwhere)
            for key in call.keys() - CALL_KEYS:
                raise ValueError("%s: llave desconocida \"%s\"" % (cwhere, key))
            if not isinstance(call.get("name"), str) or not call["name"]:
                raise ValueError("%s: falta \"name\" (cadena)" % cwhere)
            if "arguments" not in call:
                raise ValueError("%s: falta \"arguments\"" % cwhere)
            check(cwhere, call, "arguments", (dict, str))
            check(cwhere, call, "id", (str,))
            check(cwhere, call, "omit_id", (bool,))
    return steps


def replay_missing(request):
    """True si hay `tools` y un asistente con tool_calls sin pensamiento de vuelta."""
    if not request.get("tools"):
        return False
    for m in request.get("messages") or []:
        if isinstance(m, dict) and m.get("role") == "assistant" and m.get("tool_calls"):
            if not any(isinstance(m.get(k), str) and m[k] for k in REASONING_KEYS):
                return True
    return False


class Handler(BaseHTTPRequestHandler):
    opts = None  # argparse.Namespace, lo pone main()
    script = None  # lista ya validada de --script, o None
    lock = threading.Lock()  # protege el cursor del guion, el contador de ids y el registro
    next_step = 0
    next_call_id = 0
    server_version = "fake-llm"  # run-fake.sh lo busca en la cabecera Server.

    def log_message(self, fmt, *args):  # Sin la conversación en el registro.
        sys.stderr.write("fake-llm: %s %s\n" % (self.command, self.path))

    def finish(self):
        super().finish()
        # Con --tls, el aviso de cierre de TLS (close_notify): el SSE termina al
        # cerrar la conexión, y sin él curl toma la respuesta como cortada.
        if isinstance(self.connection, ssl.SSLSocket):
            self.connection.settimeout(2)
            try:
                self.connection.unwrap()
            except OSError:
                pass

    def send_json(self, status, body, headers=()):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.rstrip("/") != "/v1/models":
            return self.send_json(404, {"error": {"message": "No existe."}})
        self.send_json(200, {"object": "list", "data": [{"id": m, "object": "model"} for m in MODELS]})

    def do_POST(self):
        if self.path.rstrip("/") != "/v1/chat/completions":
            return self.send_json(404, {"error": {"message": "No existe."}})
        length = int(self.headers.get("Content-Length") or 0)
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            return self.send_json(400, {"error": {"message": "JSON inválido."}})
        if self.opts.log_requests:
            with self.lock:  # Solo el cuerpo: nunca cabeceras ni Authorization.
                with open(self.opts.log_requests, "a", encoding="utf-8") as f:
                    f.write(json.dumps(request, ensure_ascii=False) + "\n")
        if self.opts.status != 200:
            return self.send_json(
                self.opts.status,
                {"error": {"message": "Error de prueba del servidor falso."}},
                [("Retry-After", "5")] if self.opts.status == 429 else (),
            )
        if self.opts.require_reasoning_replay and replay_missing(request):
            return self.send_json(400, {"error": {"message": REPLAY_ERROR}})
        model = request.get("model", "fake")
        if self.script is not None:
            return self.scripted(request, model)
        if request.get("stream"):
            self.stream(model)
        else:
            self.send_json(200, {
                "id": "fake-1",
                "object": "chat.completion",
                "model": model,
                "choices": [{
                    "index": 0,
                    "message": {"role": "assistant", "content": ANSWER},
                    "finish_reason": "stop",
                }],
            })

    def take_step(self):
        """Siguiente elemento del guion y los ids que le tocan; None si se agotó."""
        with self.lock:
            if Handler.next_step >= len(self.script):
                return None
            step = self.script[Handler.next_step]
            Handler.next_step += 1
            calls = []
            for call in step.get("tool_calls", []):
                Handler.next_call_id += 1
                call_id = call.get("id", "call_%d" % Handler.next_call_id)
                args = call["arguments"]
                if not isinstance(args, str):
                    args = json.dumps(args, separators=(",", ":"), ensure_ascii=False)
                calls.append({"id": None if call.get("omit_id") else call_id,
                              "name": call["name"], "arguments": args})
            return step, calls

    def scripted(self, request, model):
        taken = self.take_step()
        if taken is None:
            return self.send_json(500, {"error": {"message": "fake-llm: script exhausted"}})
        step, calls = taken
        key = step.get("reasoning_key", "reasoning_content")
        content, reasoning = step.get("content"), step.get("reasoning")
        finish = step.get("finish_reason", "tool_calls" if calls else "stop")
        if not request.get("stream"):
            message = {"role": "assistant", "content": content or None}
            if reasoning:
                message[key] = reasoning
            if calls:
                message["tool_calls"] = [{
                    **({"id": c["id"]} if c["id"] is not None else {}),
                    "type": "function",
                    "function": {"name": c["name"], "arguments": c["arguments"]},
                } for c in calls]
            return self.send_json(200, {
                "id": "fake-1", "object": "chat.completion", "model": model,
                "choices": [{"index": 0, "message": message, "finish_reason": finish}],
            })

        size = step.get("chunk_size", 8)
        variant = step.get("variant", "indexed")
        deltas = [{key: p, "content": None} for p in chunks(reasoning or "", size)]
        deltas += [{"content": p} for p in chunks(content or "", size)]
        deltas += [{"tool_calls": [piece]} for piece in self.call_pieces(calls, size, variant)]
        first = {"role": "assistant", "content": None}
        if step.get("mixed_first_chunk") and deltas:
            # Un trozo de pensamiento, uno de texto y el primero de la primera llamada.
            first, rest = {"role": "assistant"}, []
            for d in deltas:
                kind = "tool_calls" if "tool_calls" in d else "content" if d["content"] else key
                if kind in first:
                    rest.append(d)
                else:
                    first[kind] = d[kind]
            deltas = rest
        self.stream(model, [first] + deltas, finish)

    @staticmethod
    def call_pieces(calls, size, variant):
        """Pedazos de tool_calls (uno por delta) según la variante."""
        def head(i, c, args):
            piece = {"index": i} if variant != "no_index" else {}
            if c["id"] is not None:
                piece["id"] = c["id"]
            piece.update({"type": "function", "function": {"name": c["name"], "arguments": args}})
            return piece

        if variant == "single_chunk":
            return [head(i, c, c["arguments"]) for i, c in enumerate(calls)]
        per_call = []  # Pedazos de cada llamada: la cabecera y luego los argumentos.
        for i, c in enumerate(calls):
            tail = [{**({"index": i} if variant != "no_index" else {}),
                     "function": {"arguments": a}} for a in chunks(c["arguments"], size)]
            per_call.append([head(i, c, "")] + tail)
        if variant == "no_index":
            return [p for pieces in per_call for p in pieces]
        # indexed: intercalados (el pedazo r de cada llamada, ronda tras ronda).
        longest = max(map(len, per_call), default=0)
        return [pieces[r] for r in range(longest) for pieces in per_call if r < len(pieces)]

    def stream(self, model, deltas=None, finish="stop"):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        delay = 0.25 if self.opts.slow else 0.04

        def event(delta, finish=None):
            body = {
                "id": "fake-1",
                "object": "chat.completion.chunk",
                "model": model,
                "choices": [{"index": 0, "delta": delta, "finish_reason": finish}],
            }
            self.wfile.write(b"data: " + json.dumps(body).encode() + b"\n\n")
            self.wfile.flush()
            time.sleep(delay)

        try:
            if deltas is not None:  # Respuesta del guion: ya viene armada.
                for d in deltas:
                    event(d)
                event({}, finish)
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
                return
            event({"role": "assistant", "content": None})
            if self.opts.reasoning:
                for part in chunks(REASONING, 12):
                    event({"reasoning_content": part, "content": None})
            for part in chunks(ANSWER, 8):
                event({"content": part})
            event({}, "stop")
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass  # Moss canceló (Esc o Detener).


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--reasoning", action="store_true",
                        help="manda pensamiento (reasoning_content) antes del texto")
    parser.add_argument("--slow", action="store_true",
                        help="responde lento (250 ms entre trozos)")
    parser.add_argument("--status", type=int, default=200,
                        help="responde siempre con este código, p. ej. 429")
    parser.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"),
                        help="HTTPS con este certificado y su llave (PEM), hasta TLS 1.2")
    parser.add_argument("--script", metavar="ARCHIVO.json",
                        help="lista JSON: la respuesta de cada petición, en orden")
    parser.add_argument("--log-requests", metavar="ARCHIVO.jsonl",
                        help="agrega una línea JSON por cada cuerpo recibido (sin cabeceras)")
    parser.add_argument("--require-reasoning-replay", action="store_true",
                        help="400 si una petición con tools devuelve un asistente con "
                             "tool_calls sin reasoning_content ni reasoning")
    Handler.opts = parser.parse_args()
    if Handler.opts.script:
        try:
            Handler.script = load_script(Handler.opts.script)
        except ValueError as e:
            print("fake-llm: guion inválido: %s" % e, file=sys.stderr)
            sys.exit(2)
    server = ThreadingHTTPServer(("127.0.0.1", Handler.opts.port), Handler)
    scheme = "http"
    if Handler.opts.tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(*Handler.opts.tls)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    print("fake-llm en %s://127.0.0.1:%d/v1" % (scheme, Handler.opts.port), file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
