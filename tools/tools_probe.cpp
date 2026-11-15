// tools_probe.cpp — sonda de tool calling para elegir modelos (guias/18-moss-code.md, 3.6).
//
// Uso (a mano; las pruebas automáticas nunca llaman a la API real):
//
//   ./build/dev/tools/moss-tools-probe --modelos
//   ./build/dev/tools/moss-tools-probe <modelo> [<modelo>...]
//
// Usa la configuración de la app (proveedor vigente, key guardada o CHAT_API_KEY)
// sin escribir nada en disco, y cambia solo el modelo. Corre cuatro escenarios con
// herramientas falsas en memoria (read y glob sobre archivos inventados) y escribe
// una tabla por modelo. Nunca imprime la key ni cabeceras.

#include "chatbot/chat_client.h"
#include "chatbot/config.h"
#include "chatbot/curl_transport.h"
#include "chatbot/error.h"
#include "chatbot/result.h"
#include "chatbot/types.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace cb = chatbot;

constexpr int kMaxSteps = 8;

// --- Herramientas falsas ---------------------------------------------------

const std::map<std::string, std::string>& fake_files() {
    static const std::map<std::string, std::string> files{
        {"notes/alpha.txt", "Launch code: ALPHA-7431\n"},
        {"notes/beta.txt", "Launch code: BETA-2958\n"},
        {"src/main.cpp", "// Entry point.\n// Build tag: GAMMA-6120\nint main() { return 0; }\n"},
        {"docs/readme.md", "# Demo project\nSecret word: DELTA-8847\n"},
    };
    return files;
}

const std::vector<cb::ToolSpec>& tool_specs() {
    static const std::vector<cb::ToolSpec> specs{
        {"read", "Read a text file and return its contents.",
         R"({"type":"object","properties":{"path":{"type":"string","description":"Path of the file to read."}},"required":["path"]})"},
        {"glob", "List the files whose path matches a pattern ('*' matches any run of characters, '?' one character).",
         R"({"type":"object","properties":{"pattern":{"type":"string","description":"Pattern such as '*.md'."}},"required":["pattern"]})"},
    };
    return specs;
}

bool glob_match(const std::string& p, std::size_t pi, const std::string& t, std::size_t ti) {
    if (pi == p.size()) return ti == t.size();
    if (p[pi] == '*') {
        for (std::size_t k = ti; k <= t.size(); ++k) {
            if (glob_match(p, pi + 1, t, k)) return true;
        }
        return false;
    }
    if (ti == t.size()) return false;
    return (p[pi] == '?' || p[pi] == t[ti]) && glob_match(p, pi + 1, t, ti + 1);
}

/// Estado de las herramientas de un escenario.
struct FakeTools {
    bool fail_first_read = false;
    int read_calls = 0;
    bool glob_called = false;
    std::vector<std::string> read_ok; ///< Rutas leídas con éxito.

    std::string run(const cb::ToolCall& call) {
        if (call.name != "read" && call.name != "glob") {
            return "error: unknown tool '" + call.name + "'";
        }
        const nlohmann::json args = nlohmann::json::parse(call.arguments, nullptr, false);
        if (args.is_discarded() || !args.is_object()) {
            return "error: the arguments are not a valid JSON object";
        }
        const std::string key = call.name == "glob" ? "pattern" : "path";
        const auto it = args.find(key);
        if (it == args.end() || !it->is_string()) {
            return "error: missing or non-string argument '" + key + "'";
        }
        const std::string value = it->get<std::string>();
        if (call.name == "glob") {
            glob_called = true;
            std::string list;
            for (const auto& [path, text] : fake_files()) {
                if (glob_match(value, 0, path, 0)) list += path + "\n";
            }
            return list.empty() ? "(no matches)" : list;
        }
        if (fail_first_read && ++read_calls == 1) {
            return "error: transient I/O failure while reading '" + value +
                   "'; the file is fine, try the same call again";
        }
        const auto file = fake_files().find(value);
        if (file == fake_files().end()) {
            return "error: no such file '" + value + "' (use glob to list files)";
        }
        read_ok.push_back(value);
        return file->second;
    }
};

// --- Escenarios ------------------------------------------------------------

struct Scenario {
    std::string title;
    std::string prompt;
    std::vector<std::string> codes;     ///< Datos que la respuesta final debe traer.
    std::vector<std::string> need_read; ///< Rutas que tienen que leerse con éxito.
    bool need_glob = false;
    bool fail_first_read = false;
    bool replay = true;
    bool report_together = false;       ///< Escenario 3: anotar juntas o en serie.
};

std::vector<Scenario> scenarios() {
    const std::string tail = " Use the tools; do not guess. Answer briefly.";
    return {
        {"1 llamada simple", "Read notes/alpha.txt and tell me its launch code." + tail,
         {"ALPHA-7431"}, {"notes/alpha.txt"}},
        {"2 dos pasos", "Use glob to find the Markdown files, then read the one you find and tell me its secret word." + tail,
         {"DELTA-8847"}, {"docs/readme.md"}, true},
        {"3 varias llamadas", "Read both notes/alpha.txt and notes/beta.txt and tell me both launch codes." + tail,
         {"ALPHA-7431", "BETA-2958"}, {"notes/alpha.txt", "notes/beta.txt"}, false, false, true, true},
        {"4 recuperacion", "Read src/main.cpp and tell me its build tag. If a tool call fails, fix the problem and try again." + tail,
         {"GAMMA-6120"}, {"src/main.cpp"}, false, true},
        {"2 sin pensamiento de regreso", "Use glob to find the Markdown files, then read the one you find and tell me its secret word." + tail,
         {"DELTA-8847"}, {"docs/readme.md"}, true, false, false},
    };
}

struct Outcome {
    std::string result;
    bool ok = false;
    bool invalid_request = false;
    int steps = 0;
    double seconds = 0;
    std::string reasoning; ///< Llave del pensamiento que llegó, o vacío.
};

bool contains(const std::string& text, const std::string& what) {
    return text.find(what) != std::string::npos;
}

Outcome run_scenario(cb::ChatClient& client, const Scenario& sc) {
    const auto started = std::chrono::steady_clock::now();
    Outcome out;
    const auto finish = [&](std::string result) {
        out.result = std::move(result);
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return out;
    };

    FakeTools tools;
    tools.fail_first_read = sc.fail_first_read;
    cb::RequestOptions options;
    options.tools = tool_specs();
    options.replay_reasoning = sc.replay;
    std::vector<cb::Message> messages{
        {cb::Role::System, "You are a coding assistant. You can call the tools read and glob."},
        {cb::Role::User, sc.prompt},
    };

    std::size_t total_calls = 0;
    std::size_t max_per_response = 0;
    std::string content;
    bool done = false;
    for (int step = 1; step <= kMaxSteps && !done; ++step) {
        out.steps = step;
        content.clear();
        std::string reasoning;
        const auto response = client.complete_stream(
            messages,
            [&](const cb::StreamDelta& delta) {
                if (delta.kind == cb::StreamDelta::Kind::Content) content += delta.text;
                if (delta.kind == cb::StreamDelta::Kind::Reasoning) reasoning += delta.text;
                return true;
            },
            nullptr, options);
        if (response.is_error()) {
            const cb::ChatError& e = response.error();
            out.invalid_request = e.kind == cb::ErrorKind::InvalidRequest;
            if (e.kind == cb::ErrorKind::RateLimited) return finish("límite de peticiones");
            std::string why = std::string{"falló: "} + cb::error_kind_label(e.kind);
            if (e.http_status != 0) why += " " + std::to_string(e.http_status);
            return finish(why);
        }
        const cb::CompletionInfo& info = response.value();
        if (!reasoning.empty() && out.reasoning.empty()) {
            out.reasoning = info.reasoning_key == cb::ReasoningKey::Reasoning ? "reasoning" : "reasoning_content";
        }
        if (info.tool_calls.empty()) {
            done = true;
            break;
        }
        total_calls += info.tool_calls.size();
        max_per_response = std::max(max_per_response, info.tool_calls.size());
        messages.push_back({cb::Role::Assistant, content, info.tool_calls, "", reasoning, info.reasoning_key});
        for (const cb::ToolCall& call : info.tool_calls) {
            messages.push_back({cb::Role::Tool, tools.run(call), {}, call.id});
        }
    }

    if (!done) return finish("falló: 8 pasos");
    if (total_calls == 0 && (contains(content, "<tool_call>") || contains(content, "{\"name\":"))) {
        return finish("llamó como texto");
    }
    for (const std::string& path : sc.need_read) {
        bool read = false;
        for (const std::string& p : tools.read_ok) read = read || p == path;
        if (!read) return finish("falló: no llamó la herramienta");
    }
    if (sc.need_glob && !tools.glob_called) return finish("falló: no llamó la herramienta");
    if (content.empty()) return finish("falló: sin respuesta final");
    for (const std::string& code : sc.codes) {
        if (!contains(content, code)) return finish("falló: no usó el resultado");
    }
    out.ok = true;
    return finish(sc.report_together ? (max_per_response > 1 ? "bien (juntas)" : "bien (en serie)") : "bien");
}

// --- Tabla -----------------------------------------------------------------

std::size_t width(const std::string& s) { // Puntos de código, para alinear acentos.
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

std::string pad(const std::string& s, std::size_t w) { return s + std::string(w > width(s) ? w - width(s) : 0, ' '); }

std::string seconds_text(double s) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(1) << s << " s";
    return o.str();
}

void print_table(const std::string& model, const std::vector<Scenario>& list, const std::vector<Outcome>& outs) {
    std::vector<std::vector<std::string>> rows{{"Escenario", "Resultado", "Pasos", "Tiempo", "Pensamiento"}};
    for (std::size_t i = 0; i < list.size(); ++i) {
        rows.push_back({list[i].title, outs[i].result, std::to_string(outs[i].steps), seconds_text(outs[i].seconds),
                        outs[i].reasoning.empty() ? "no" : outs[i].reasoning});
    }
    std::vector<std::size_t> w(5, 0);
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < 5; ++c) w[c] = std::max(w[c], width(row[c]));
    }
    std::cout << "\nModelo: " << model << "\n";
    for (const auto& row : rows) {
        std::cout << "  ";
        for (std::size_t c = 0; c < 5; ++c) std::cout << (c == 4 ? row[c] : pad(row[c], w[c] + 2));
        std::cout << "\n";
    }

    // Exigir el pensamiento de regreso: el 2 pasa con él y sin él da un 400.
    const Outcome& with = outs[1];
    const Outcome& without = outs.back();
    std::string demands = "no se pudo saber";
    if (with.ok && without.ok) demands = "no";
    if (with.ok && without.invalid_request) demands = "sí";
    std::string sends = "no";
    for (const Outcome& o : outs) {
        if (!o.reasoning.empty()) sends = "sí (llave " + o.reasoning + ")";
    }
    std::cout << "  Manda pensamiento: " << sends << ". Exige el pensamiento de regreso: " << demands << ".\n";
}

void print_error(const cb::ChatError& e) {
    std::cerr << "[" << cb::error_kind_label(e.kind) << "] http_status=" << e.http_status << ": " << e.message << '\n';
}

void print_usage(std::ostream& out) {
    out << "Uso: moss-tools-probe --modelos\n"
           "     moss-tools-probe <modelo> [<modelo>...]\n"
           "Corre cuatro escenarios de tool calling con herramientas falsas y escribe una tabla\n"
           "por modelo. Usa la configuración de la app (proveedor y key) sin escribir nada.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        print_usage(std::cout);
        return 0;
    }
    const bool list_only = args.size() == 1 && args[0] == "--modelos";
    bool usage_ok = !args.empty();
    for (const std::string& a : args) usage_ok = usage_ok && (list_only || a.rfind("--", 0) != 0);
    if (!usage_ok) {
        print_usage(std::cerr);
        return 2;
    }

    cb::ConfigOptions options;
    options.allow_missing_key_and_model = true;
    const cb::Result<cb::Config> loaded = cb::load_config(options);
    if (loaded.is_error()) {
        print_error(loaded.error());
        return 1;
    }
    cb::Config config = loaded.value();

    if (list_only) {
        cb::ChatClient client(config, std::make_unique<cb::CurlTransport>());
        const auto models = client.list_models();
        if (models.is_error()) {
            print_error(models.error());
            return 1;
        }
        for (const std::string& m : models.value()) std::cout << m << '\n';
        return 0;
    }

    const std::vector<Scenario> list = scenarios();
    for (const std::string& model : args) {
        config.model = model;
        if (const auto bad = cb::validate_config(config)) {
            print_error(*bad);
            return 1;
        }
        cb::ChatClient client(config, std::make_unique<cb::CurlTransport>());
        std::vector<Outcome> outs;
        for (const Scenario& sc : list) outs.push_back(run_scenario(client, sc));
        print_table(model, list, outs);
    }
    return 0;
}
