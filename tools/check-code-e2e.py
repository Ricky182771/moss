#!/usr/bin/env python3
"""La prueba de punta a punta de `moss code -p`, sin red ni keys.

Uso: python3 -I tools/check-code-e2e.py <ruta a moss>     (en Windows: python -I ... moss.exe)

Por cada guion de tests/data/fake-llm/code-*.json arranca tools/fake-llm.py en un
puerto libre de 127.0.0.1 (con --require-reasoning-replay y --log-requests) y corre
`moss code tests/data/code-project -p ...` con un entorno aislado: sin las CHAT_* de
quien lo corre, con la configuracion de Moss en una carpeta temporal (una copia de moss
con moss.portable, igual en Linux y en Windows, donde la carpeta de AppData no sale del
entorno) y sin proxy. Comprueba el codigo de salida, stdout (solo la respuesta final),
las lineas de stderr y las peticiones que recibio el servidor. No depende de git ni de rg.
Solo biblioteca estandar.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PROJECT = REPO / "tests" / "data" / "code-project"
SCRIPTS = REPO / "tests" / "data" / "fake-llm"
KEY = "ci-sin-key-real"
MARK = "● "  # La marca de cada herramienta en stderr.
WINDOWS = os.name == "nt"
LIMIT = 120  # Segundos para que moss termine.

CASES = [
    {
        "script": "code-tres-pasos.json",
        "prompt": "Que dice el README",
        "exit": 0,
        "stdout": "Listo: el README.md trae la clave ZORZAL-7731.\n",
        "stderr_has": [MARK + "Buscar archivos *.md", MARK + "Leer README.md"],
        "requests": 3,
        "tool_has": ["README.md", "ZORZAL-7731"],  # El resultado de glob y el de read, en orden.
    },
    {
        "script": "code-grep.json",
        "prompt": "Donde esta la marca",
        "exit": 0,
        "stdout": "La marca GRILLO-4410 está en src/util.cpp.\n",
        "stderr_has": [MARK + 'Buscar "GRILLO-[0-9]+"'],
        "requests": 2,
        "tool_has": ["GRILLO-4410"],
    },
    {
        "script": "code-limite.json",
        "prompt": "Explora todo",
        "config": {"code": {"max_steps": 2}},
        "exit": 1,
        "stdout": "",
        "stderr_has": [MARK + "Buscar archivos *.md", MARK + "Leer README.md",
                       "Se detuvo el turno: llegó al límite de 2 peticiones"],
        "stderr_not": [MARK + "Buscar \"GRILLO"],
        "requests": 2,
        "tool_has": ["README.md"],  # El de read no se manda: el limite corta antes de la 3a peticion.
    },
]


class Fail(Exception):
    pass


def isolated_env(work, port):
    """Un entorno minimo: lo que necesitan python y moss para arrancar, y nada mas."""
    keep = {"PATH", "SYSTEMROOT", "SYSTEMDRIVE", "WINDIR", "COMSPEC", "PATHEXT"}
    env = {k: v for k, v in os.environ.items() if k.upper() in keep}
    tmp = work / "tmp"
    tmp.mkdir()
    home = work / "home"
    home.mkdir()
    env.update(TEMP=str(tmp), TMP=str(tmp), TMPDIR=str(tmp), HOME=str(home),
               USERPROFILE=str(home), APPDATA=str(home / "AppData" / "Roaming"),
               LOCALAPPDATA=str(home / "AppData" / "Local"),
               XDG_CONFIG_HOME=str(work / "xdg-config"), XDG_DATA_HOME=str(work / "xdg-data"),
               XDG_STATE_HOME=str(work / "xdg-state"), XDG_CACHE_HOME=str(work / "xdg-cache"),
               NO_PROXY="127.0.0.1,localhost", no_proxy="127.0.0.1,localhost")
    if not WINDOWS:
        env["LC_ALL"] = "C.UTF-8"
    env.update(CHAT_BASE_URL="http://127.0.0.1:%d/v1" % port, CHAT_API_KEY=KEY,
               CHAT_MODEL="fake", CHAT_DATA_DIR=str(work / "chats"))
    return env


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for_server(server, port, server_err):
    end = time.monotonic() + 30
    while time.monotonic() < end:
        if server.poll() is not None:
            raise Fail("fake-llm.py no arranco (salio con %s): %s"
                       % (server.returncode, server_err.read_text(encoding="utf-8", errors="replace")))
        try:
            socket.create_connection(("127.0.0.1", port), 0.5).close()
            return
        except OSError:
            time.sleep(0.1)
    raise Fail("fake-llm.py no escucha en el puerto %d tras 30 s" % port)


def stop(server):
    if server.poll() is None:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()


def text_of(data):
    return data.decode("utf-8", errors="replace").replace("\r\n", "\n")


def check_log(case, log):
    lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
    for i, line in enumerate(lines):
        for what in ("authorization", KEY):
            if what in line.lower():
                raise Fail("peticion %d: el registro contiene %r" % (i, what))
    reqs = [json.loads(line) for line in lines]
    if len(reqs) != case["requests"]:
        raise Fail("el servidor recibio %d peticiones, se esperaban %d" % (len(reqs), case["requests"]))
    names = sorted(t.get("function", {}).get("name") for t in reqs[0].get("tools") or [])
    if names != ["glob", "grep", "read", "todo_write"]:
        raise Fail("la primera peticion trae las herramientas %s" % names)
    system = [m for m in reqs[0]["messages"] if m.get("role") == "system"]
    if len(system) != 1 or "AGENTS.md" not in system[0]["content"] \
            or "frases cortas" not in system[0]["content"]:
        raise Fail("el mensaje system no trae el AGENTS.md del proyecto de prueba")
    # Todas las peticiones traen lo mismo; la ultima, el historial completo del turno.
    for i, r in enumerate(reqs):
        for m in r["messages"]:
            if m.get("role") == "assistant" and m.get("tool_calls") and not m.get("reasoning_content"):
                raise Fail("peticion %d: un asistente con tool_calls sin reasoning_content" % i)
    tool = [m for m in reqs[-1]["messages"] if m.get("role") == "tool"]
    if len(tool) != len(case["tool_has"]):
        raise Fail("la ultima peticion trae %d mensajes tool, se esperaban %d"
                   % (len(tool), len(case["tool_has"])))
    for m, expected in zip(tool, case["tool_has"]):
        if expected not in m.get("content", ""):
            raise Fail("un mensaje tool no trae %r: %r" % (expected, m.get("content")))


def run_case(moss, case):
    work = Path(tempfile.mkdtemp(prefix="moss-code-e2e-"))
    server = None
    try:
        port = free_port()
        env = isolated_env(work, port)
        # Una copia de moss en modo portable: la configuracion sale de datos/ junto a ella.
        bin_dir = work / "bin"
        (bin_dir / "datos").mkdir(parents=True)
        shutil.copy2(moss, bin_dir)
        (bin_dir / "moss.portable").touch()
        if "config" in case:
            (bin_dir / "datos" / "config.json").write_text(json.dumps(case["config"]), encoding="utf-8")
        log = work / "peticiones.jsonl"
        server_err = work / "fake-llm.err"
        with open(server_err, "wb") as err:
            server = subprocess.Popen(
                [sys.executable, "-I", str(REPO / "tools" / "fake-llm.py"), "--port", str(port),
                 "--script", str(SCRIPTS / case["script"]), "--log-requests", str(log),
                 "--require-reasoning-replay"],
                env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=err)
        wait_for_server(server, port, server_err)
        try:
            done = subprocess.run([str(bin_dir / Path(moss).name), "code", str(PROJECT), "-p", case["prompt"]],
                                  env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=LIMIT)
        except subprocess.TimeoutExpired:
            raise Fail("moss no termino en %d s" % LIMIT)
        out, err = text_of(done.stdout), text_of(done.stderr)
        if done.returncode != case["exit"]:
            raise Fail("moss salio con %d, se esperaba %d. stderr:\n%s" % (done.returncode, case["exit"], err))
        if out != case["stdout"]:
            raise Fail("stdout es %r, se esperaba %r" % (out, case["stdout"]))
        for want in case["stderr_has"]:
            if want not in err:
                raise Fail("stderr no trae %r. stderr:\n%s" % (want, err))
        for unwanted in case.get("stderr_not", []):
            if unwanted in err:
                raise Fail("stderr no deberia traer %r. stderr:\n%s" % (unwanted, err))
        check_log(case, log)
    finally:
        if server is not None:
            stop(server)
        shutil.rmtree(work, ignore_errors=True)


def main():
    if len(sys.argv) != 2:
        sys.exit("Uso: python3 -I tools/check-code-e2e.py <ruta a moss>")
    moss = Path(sys.argv[1])
    if not moss.is_file():
        print("FALLO: no existe %s" % moss)
        return 1
    for case in CASES:
        print("== guion: %s" % case["script"], flush=True)
        try:
            run_case(moss.resolve(), case)
        except Fail as e:
            print("FALLO: %s" % e)
            return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
