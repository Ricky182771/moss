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
"""

import argparse
import json
import sys
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


class Handler(BaseHTTPRequestHandler):
    opts = None  # argparse.Namespace, lo pone main()
    server_version = "fake-llm"  # run-fake.sh lo busca en la cabecera Server.

    def log_message(self, fmt, *args):  # Sin la conversación en el registro.
        sys.stderr.write("fake-llm: %s %s\n" % (self.command, self.path))

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
        if self.opts.status != 200:
            return self.send_json(
                self.opts.status,
                {"error": {"message": "Error de prueba del servidor falso."}},
                [("Retry-After", "5")] if self.opts.status == 429 else (),
            )
        model = request.get("model", "fake")
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

    def stream(self, model):
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
    Handler.opts = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", Handler.opts.port), Handler)
    print("fake-llm en http://127.0.0.1:%d/v1" % Handler.opts.port, file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
