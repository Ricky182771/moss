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
#include <memory>
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
            std::function<bool()> interrupted = [] { return false; }) {
    std::ostringstream out;
    std::ostringstream err;
    CodeEnvironment environment{
        .out = out,
        .err = err,
        .load_config = [config] { return chatbot::Result<Config>{config}; },
        .config_path = (layout.config_dir / "config.json").string(),
        .credentials_path = (layout.config_dir / "credentials.json").string(),
        .data_dir = layout.data_dir.string(),
        .portable_dir = std::nullopt,
        .home = {},
        .make_client = std::move(make_client),
        .interrupted = std::move(interrupted),
        .now = [] { return std::time_t{1700000000}; },
    };
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
            .portable_dir = std::nullopt,
            .home = layout.project.string(),
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
        .portable_dir = std::nullopt,
        .home = {},
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

TEST_CASE("moss code sin -p: la pantalla llega en C2", "[code][cli]") {
    const Layout layout;
    const Outcome result = run(layout, {layout.project.string()}, scripted({}));
    CHECK(result.code == 2);
    CHECK(result.out.empty());
    CHECK(result.err ==
          "La pantalla de Moss Code llega en C2; por ahora usa: moss code -p \"…\"\n");
}
