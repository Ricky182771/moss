// moss code -p (run_code): el modelo es un FakeTransport o un BlockingTransport, los flujos son
// ostringstream, y las carpetas son temporales. Sin red ni Ctrl+C real: la interrupción es una
// función inyectada.

#include "code_command.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using chatbot::Config;
using chatbot::HttpResponse;
using chatbot::cli::CodeEnvironment;
using chatbot::cli::CommandLine;
using chatbot::cli::parse_command_line;
using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kMark = "● ";

std::string chunk(json delta) {
    json choice = json::object();
    choice["delta"] = std::move(delta);
    json body = json::object();
    body["choices"] = json::array({choice});
    return "data: " + body.dump() + "\n\n";
}

const std::string kDone = "data: [DONE]\n\n";

std::string answer_flow(const std::string& text) {
    return chunk({{"content", text}}) + kDone;
}

/// Una respuesta del modelo con una llamada a una herramienta (y texto antes, opcional).
std::string call_flow(const std::string& name, const std::string& arguments,
                      const std::string& text = {}) {
    std::string flow = text.empty() ? std::string{} : chunk({{"content", text}});
    json function = json::object();
    function["name"] = name;
    function["arguments"] = arguments;
    json call = json::object();
    call["index"] = 0;
    call["id"] = "call_" + name;
    call["type"] = "function";
    call["function"] = function;
    return flow + chunk({{"tool_calls", json::array({call})}}) +
           "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n" + kDone;
}

HttpResponse ok(std::string body) {
    return HttpResponse{200, std::move(body), std::nullopt, false, ""};
}

using MakeClient = std::function<std::unique_ptr<chatbot::ChatClient>(const Config&)>;

MakeClient scripted(std::vector<HttpResponse> responses) {
    return [responses = std::move(responses)](const Config& config) {
        auto transport = std::make_unique<chatbot_test::FakeTransport>();
        transport->responses = responses;
        return std::make_unique<chatbot::ChatClient>(config, std::move(transport),
                                                     std::make_unique<chatbot_test::FakeSleeper>());
    };
}

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream{path} << text;
}

/// Un proyecto, la carpeta de configuración y la de datos, hermanos en una carpeta temporal.
struct Layout {
    chatbot_test::ScopedTempDir temp;
    fs::path project = temp.path() / "proy";
    fs::path config_dir = temp.path() / "cfg";
    fs::path data_dir = temp.path() / "datos";

    Layout() {
        fs::create_directories(project);
        fs::create_directories(config_dir);
        write_file(project / "a.txt", "alfa\nbeta\n");
        write_file(project / "b.txt", "gamma\n");
    }
};

struct Outcome {
    int code = -1;
    std::string out;
    std::string err;
};

Outcome run(const Layout& layout, const std::vector<std::string>& args, MakeClient make_client,
            Config config = chatbot_test::test_config(),
            std::function<bool()> interrupted = [] { return false; },
            const std::function<void(CodeEnvironment&)>& tweak = {}) {
    std::ostringstream out;
    std::ostringstream err;
    CodeEnvironment environment{
        .out = out,
        .err = err,
        .load_config = [config] { return chatbot::Result<Config>{config}; },
        .config_path = (layout.config_dir / "config.json").string(),
        .credentials_path = (layout.config_dir / "credentials.json").string(),
        .data_dir = layout.data_dir.string(),
        .app_data_dir = std::nullopt,
        .portable_dir = std::nullopt,
        .home = {},
        .env_secrets = {},
        .make_client = std::move(make_client),
        .interrupted = std::move(interrupted),
        .now = [] { return std::time_t{1700000000}; },
    };
    if (tweak) {
        tweak(environment);
    }
    std::vector<std::string> full{"code"};
    full.insert(full.end(), args.begin(), args.end());
    const int code = chatbot::cli::run_code(parse_command_line(full), environment);
    return {code, out.str(), err.str()};
}

std::vector<std::string> prompt_with(const std::string& command, const Layout& layout) {
    return {command, layout.project.string(), "-p", "hola"};
}

std::vector<std::string> prompt_in(const Layout& layout, const std::string& text = "hola") {
    return {layout.project.string(), "-p", text};
}

} // namespace

TEST_CASE("moss code -p: tres pasos, stdout solo la respuesta final", "[code][cli]") {
    const Layout layout;
    const Outcome result = run(
        layout, prompt_in(layout),
        scripted({ok(call_flow("glob", R"({"pattern":"*.txt"})", "Voy a buscar.")),
                  ok(call_flow("read", R"({"path":"a.txt"})")),
                  ok(answer_flow("Hay dos archivos: a.txt y b.txt."))}));
    CHECK(result.code == 0);
    CHECK(result.out == "Hay dos archivos: a.txt y b.txt.\n");
    // Una línea ● por herramienta, en orden; sin el texto intermedio.
    const std::size_t glob = result.err.find(std::string{kMark} + "Buscar");
    const std::size_t read = result.err.find(std::string{kMark} + "Leer a.txt");
    REQUIRE(glob != std::string::npos);
    REQUIRE(read != std::string::npos);
    CHECK(glob < read);
    CHECK(result.err.find("Voy a buscar.") == std::string::npos);
}

TEST_CASE("moss code -p: stdout conserva tabuladores y saltos, sin escapes ni controles",
          "[code][cli]") {
    const Layout layout;
    const Outcome result =
        run(layout, prompt_in(layout),
            scripted({ok(answer_flow("a\tb\nc \x1b[31mrojo\r\nfin\x07\t."))}));
    CHECK(result.code == 0);
    CHECK(result.out.starts_with("a\tb\nc "));
    CHECK(result.out.find("rojo\nfin") != std::string::npos);
    CHECK(result.out.ends_with("\t.\n"));
    for (const char forbidden : {'\x1b', '\x07', '\r'}) {
        CHECK(result.out.find(forbidden) == std::string::npos);
    }
}

TEST_CASE("moss code -p: una herramienta con error deja su línea [error] en stderr", "[code][cli]") {
    const Layout layout;
    const Outcome result =
        run(layout, prompt_in(layout),
            scripted({ok(call_flow("read", R"({"path":"no-existe.txt"})")), ok(answer_flow("Listo."))}));
    CHECK(result.code == 0);
    CHECK(result.out == "Listo.\n");
    CHECK(result.err.find("[error]") != std::string::npos);
}

TEST_CASE("moss code -p: error del modelo, sin key y límite de pasos salen con 1", "[code][cli]") {
    const Layout layout;
    SECTION("401") {
        const Outcome result = run(
            layout, prompt_in(layout),
            scripted({HttpResponse{401, R"({"error":{"message":"mala key"}})", std::nullopt, false,
                                   ""}}));
        CHECK(result.code == 1);
        CHECK(result.out.empty());
        CHECK(result.err.find("[error de autenticación]") != std::string::npos);
    }
    SECTION("sin key") {
        Config config = chatbot_test::test_config();
        config.api_key.clear();
        const Outcome result =
            run(layout, prompt_in(layout), scripted({ok(answer_flow("no"))}), config);
        CHECK(result.code == 1);
        CHECK(result.out.empty());
        CHECK(result.err.find("[error de configuración]") != std::string::npos);
    }
    SECTION("sin modelo") {
        Config config = chatbot_test::test_config();
        config.model.clear();
        const Outcome result =
            run(layout, prompt_in(layout), scripted({ok(answer_flow("no"))}), config);
        CHECK(result.code == 1);
        CHECK(result.out.empty());
    }
    SECTION("límite de pasos") {
        write_file(layout.config_dir / "config.json", R"({"code":{"max_steps":1}})");
        const Outcome result =
            run(layout, prompt_in(layout),
                scripted({ok(call_flow("read", R"({"path":"a.txt"})")), ok(answer_flow("no"))}));
        CHECK(result.code == 1);
        CHECK(result.out.empty());
        CHECK(result.err.find(std::string{kMark} + "Leer a.txt") != std::string::npos);
        CHECK(result.err.find("Se detuvo") != std::string::npos);
    }
}

TEST_CASE("moss code -p: la carpeta tiene que existir y no ser de Moss", "[code][cli]") {
    const Layout layout;
    const MakeClient never = scripted({ok(answer_flow("no"))});
    SECTION("inexistente") {
        const Outcome result =
            run(layout, {(layout.temp.path() / "nada").string(), "-p", "hola"}, never);
        CHECK(result.code == 1);
        CHECK(result.out.empty());
        CHECK(result.err.find("[error de configuración]") != std::string::npos);
    }
    SECTION("la carpeta de configuración y lo que hay dentro") {
        fs::create_directories(layout.config_dir / "sub");
        CHECK(run(layout, {layout.config_dir.string(), "-p", "hola"}, never).code == 1);
        CHECK(run(layout, {(layout.config_dir / "sub").string(), "-p", "hola"}, never).code == 1);
    }
    SECTION("la carpeta de datos") {
        fs::create_directories(layout.data_dir);
        CHECK(run(layout, {layout.data_dir.string(), "-p", "hola"}, never).code == 1);
    }
    SECTION("la carpeta personal avisa y sigue") {
        std::ostringstream out;
        std::ostringstream err;
        CodeEnvironment environment{
            .out = out,
            .err = err,
            .load_config = [] { return chatbot::Result<Config>{chatbot_test::test_config()}; },
            .config_path = (layout.config_dir / "config.json").string(),
            .credentials_path = std::nullopt,
            .data_dir = std::nullopt,
            .app_data_dir = std::nullopt,
            .portable_dir = std::nullopt,
            .home = layout.project.string(),
            .env_secrets = {},
            .make_client = scripted({ok(answer_flow("Hecho."))}),
            .interrupted = [] { return false; },
            .now = [] { return std::time_t{1700000000}; },
        };
        const int code = chatbot::cli::run_code(
            parse_command_line({"code", layout.project.string(), "-p", "hola"}), environment);
        CHECK(code == 0);
        CHECK(out.str() == "Hecho.\n");
        CHECK(err.str().find("carpeta personal") != std::string::npos);
    }
}

TEST_CASE("moss code -p: bloquea la carpeta de datos de la app y la de conversaciones vigente",
          "[code][cli]") {
    Layout layout;
    const fs::path app = layout.project / "moss";
    fs::create_directories(app / "conversations");
    fs::create_directories(layout.project / "otras");
    write_file(app / "otro.txt", "hermano\n");
    write_file(layout.project / "otras" / "c.txt", "conversaciones\n");
    const auto read_of = [&](const char* path, const std::optional<std::string>& app_dir) {
        return run(layout, prompt_in(layout),
                   scripted({ok(call_flow("read", std::string{R"({"path":")"} + path + R"("})")),
                             ok(answer_flow("Listo."))}),
                   chatbot_test::test_config(), [] { return false; },
                   [&](CodeEnvironment& environment) { environment.app_data_dir = app_dir; });
    };
    SECTION("la de por defecto: <datos>/moss entero") {
        layout.data_dir = app / "conversations";
        const Outcome result = read_of("moss/otro.txt", app.string());
        CHECK(result.code == 0);
        CHECK(result.err.find("[error]") != std::string::npos);
        CHECK(read_of("a.txt", app.string()).err.find("[error]") == std::string::npos);
    }
    SECTION("con CHAT_DATA_DIR: esa carpeta y también la de por defecto") {
        layout.data_dir = layout.project / "otras";
        CHECK(read_of("otras/c.txt", app.string()).err.find("[error]") != std::string::npos);
        CHECK(read_of("moss/otro.txt", app.string()).err.find("[error]") != std::string::npos);
        // Sin la de por defecto, solo queda la vigente.
        CHECK(read_of("moss/otro.txt", std::nullopt).err.find("[error]") == std::string::npos);
    }
    SECTION("como carpeta del proyecto") {
        const MakeClient never = scripted({ok(answer_flow("no"))});
        const auto in = [&](const fs::path& folder) {
            return run(layout, {folder.string(), "-p", "hola"}, never, chatbot_test::test_config(),
                       [] { return false; },
                       [&](CodeEnvironment& environment) {
                           environment.app_data_dir = app.string();
                       });
        };
        layout.data_dir = layout.project / "otras";
        CHECK(in(app).code == 1);
        CHECK(in(app / "conversations").code == 1);
        CHECK(in(layout.project / "otras").code == 1);
    }
}

TEST_CASE("moss code -p: avisa si AGENTS.md se recortó", "[code][cli]") {
    const Layout layout;
    const MakeClient answer = scripted({ok(answer_flow("Hecho."))});
    SECTION("recortado") {
        write_file(layout.project / "AGENTS.md",
                   std::string(40 * 1024 + 100, 'x')); // Más que kMaxInstructionsBytes.
        const Outcome result = run(layout, prompt_in(layout), answer);
        CHECK(result.code == 0);
        CHECK(result.err.find("se recortó") != std::string::npos);
    }
    SECTION("corto: sin aviso") {
        write_file(layout.project / "AGENTS.md", "Usa tabuladores.\n");
        const Outcome result = run(layout, prompt_in(layout), answer);
        CHECK(result.code == 0);
        CHECK(result.err.find("se recortó") == std::string::npos);
    }
}

TEST_CASE("moss code -p: las keys guardadas y las del entorno llegan tapadas al modelo",
          "[code][cli]") {
    const Layout layout;
    write_file(layout.project / "secretos.txt",
               "tavily tvly-GUARDADA-1\ntelegram 123456:TOKEN-GUARDADO\nproveedor otra-key-9\n"
               "busqueda del entorno ENV-BUSQUEDA\ntoken del entorno ENV-TELEGRAM\n"
               "modelo " + chatbot_test::test_config().api_key + "\npublico visible\n");
    const fs::path credentials = layout.config_dir / "credentials.json";
    write_file(credentials,
               R"({"version":1,"keys":{"search:tavily":"tvly-GUARDADA-1","telegram":)"
               R"("123456:TOKEN-GUARDADO","custom:x":"otra-key-9","vacia":""}})");
#ifndef _WIN32
    fs::permissions(credentials, fs::perms::owner_read | fs::perms::owner_write);
#endif
    std::vector<std::string> bodies;
    const std::vector<HttpResponse> responses{
        ok(call_flow("read", R"({"path":"secretos.txt"})")), ok(answer_flow("Listo."))};
    const MakeClient recording = [&bodies, responses](const Config& config) {
        auto transport = std::make_unique<chatbot_test::FakeTransport>();
        transport->responses = responses;
        chatbot_test::FakeTransport* raw = transport.get();
        transport->on_send = [raw, &bodies] { bodies.push_back(raw->requests.back().body); };
        return std::make_unique<chatbot::ChatClient>(config, std::move(transport),
                                                     std::make_unique<chatbot_test::FakeSleeper>());
    };
    const Outcome result = run(layout, prompt_in(layout), recording, chatbot_test::test_config(),
                               [] { return false; }, [](CodeEnvironment& environment) {
                                   environment.env_secrets = {"ENV-BUSQUEDA", "", "ENV-TELEGRAM",
                                                              "ENV-BUSQUEDA"};
                               });
    CHECK(result.code == 0);
    REQUIRE(bodies.size() == 2);
    // La segunda petición lleva el resultado de read, con lo secreto tapado y lo demás intacto.
    CHECK(bodies[1].find("publico visible") != std::string::npos);
    for (const std::string& secret : std::vector<std::string>{"tvly-GUARDADA-1", "123456:TOKEN-GUARDADO", "otra-key-9", "ENV-BUSQUEDA", "ENV-TELEGRAM",
          chatbot_test::test_config().api_key}) {
        INFO(secret.size());
        CHECK(bodies[1].find(secret) == std::string::npos);
        CHECK(result.err.find(secret) == std::string::npos);
        CHECK(result.out.find(secret) == std::string::npos);
    }
}

TEST_CASE("moss code -p: Ctrl+C durante la petición sale con 130 en menos de 2 s", "[code][cli]") {
    const Layout layout;
    chatbot_test::BlockingTransport* transport = nullptr;
    const MakeClient blocking = [&transport](const Config& config) {
        auto owned = std::make_unique<chatbot_test::BlockingTransport>(
            chatbot_test::BlockingTransport::Mode::Block);
        transport = owned.get();
        return std::make_unique<chatbot::ChatClient>(config, std::move(owned),
                                                     std::make_unique<chatbot_test::FakeSleeper>());
    };
    const auto begin = std::chrono::steady_clock::now();
    const Outcome result = run(layout, prompt_in(layout), blocking, chatbot_test::test_config(),
                               [&transport] { return transport->started.is_cancelled(); });
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
    CHECK(result.code == 130);
    CHECK(result.out.empty());
}

TEST_CASE("moss code -p: Ctrl+C con una herramienta pendiente sale con 130", "[code][cli]") {
    const Layout layout;
    std::ostringstream out;
    std::ostringstream err;
    // Se interrumpe en cuanto sale la línea ● de la herramienta (se consulta desde el hilo
    // principal, como la bandera real): la herramienta está lista y el turno no ha terminado.
    CodeEnvironment environment{
        .out = out,
        .err = err,
        .load_config = [] { return chatbot::Result<Config>{chatbot_test::test_config()}; },
        .config_path = (layout.config_dir / "config.json").string(),
        .credentials_path = std::nullopt,
        .data_dir = std::nullopt,
        .app_data_dir = std::nullopt,
        .portable_dir = std::nullopt,
        .home = {},
        .env_secrets = {},
        .make_client = scripted({ok(call_flow("read", R"({"path":"a.txt"})")),
                                 ok(answer_flow("No debería llegar."))}),
        .interrupted = [&err] { return err.str().find(kMark) != std::string::npos; },
        .now = [] { return std::time_t{1700000000}; },
    };
    const int code = chatbot::cli::run_code(parse_command_line(prompt_with("code", layout)),
                                            environment);
    CHECK(code == 130);
    CHECK(out.str().empty());
}

