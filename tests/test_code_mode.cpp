// CodeMode (cli/src/code_mode.h): la lógica del modo código sin FTXUI. El modelo es un
// FakeTransport o un BlockingTransport (SessionFakes), post es una cola que la prueba vacía en su
// hilo, y las carpetas son temporales. Sin red. git del sistema solo se reemplaza en las
// instrucciones de sistema; glob lo usa por run_process, que la prueba inyecta.

#include "code_mode.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"
#include "session_fakes.hpp"
#include "temp_dir.hpp"

#include "chatbot/child_process.h"
#include "chatbot/credentials.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using chatbot::HttpResponse;
using chatbot::Role;
using chatbot::cli::CodeMode;
using chatbot::cli::CodeModeEnvironment;
using chatbot::cli::CodeProject;
using chatbot::cli::Entry;
using chatbot::cli::EntryKind;
using chatbot::cli::TaskItem;
using chatbot::cli::TaskState;
using chatbot::cli::ToolState;
using chatbot::code::LoopState;
using json = nlohmann::json;
namespace fs = std::filesystem;

// --- Guiones SSE (los de test_code_command.cpp, que no se toca, más varias llamadas) ---

std::string chunk(json delta) {
    json choice = json::object();
    choice["delta"] = std::move(delta);
    json body = json::object();
    body["choices"] = json::array({choice});
    return "data: " + body.dump() + "\n\n";
}

const std::string kDone = "data: [DONE]\n\n";

HttpResponse ok(std::string body) {
    return HttpResponse{200, std::move(body), std::nullopt, false, ""};
}

HttpResponse answer(const std::string& text) {
    return ok(chunk({{"content", text}}) + kDone);
}

struct Call {
    std::string name;
    std::string arguments;
};

/// Una respuesta con texto opcional antes y estas llamadas, cada una en su pedazo.
HttpResponse calls(const std::vector<Call>& list, const std::string& text = {}) {
    std::string flow = text.empty() ? std::string{} : chunk({{"content", text}});
    for (std::size_t index = 0; index < list.size(); ++index) {
        json function = json::object();
        function["name"] = list[index].name;
        function["arguments"] = list[index].arguments;
        json call = json::object();
        call["index"] = index;
        call["id"] = "call_" + std::to_string(index) + "_" + list[index].name;
        call["type"] = "function";
        call["function"] = function;
        flow += chunk({{"tool_calls", json::array({call})}});
    }
    return ok(flow + "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n" +
              kDone);
}

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream{path, std::ios::binary} << text;
}

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

constexpr const char* kNvidiaKey = "sk-clave-secreta-de-prueba-1234";

/// Lo que comparte run_process con la prueba (corre en el hilo de trabajo).
struct GitProbe {
    bool block = false;           ///< ls-files espera la cancelación (si no, git "no existe").
    chatbot::CancelToken listing; ///< Aviso: glob pidió la lista a git.
};

/// Un CodeMode sobre un proyecto temporal; la configuración y las conversaciones en otra
/// carpeta (SessionFakes).
struct Harness {
    chatbot_test::SessionFakes fakes;
    chatbot_test::ScopedTempDir projects;
    fs::path project = projects.path() / "proy";
    chatbot_test::TaskQueue queue;
    std::shared_ptr<GitProbe> git = std::make_shared<GitProbe>();
    std::string home;     ///< CodeModeEnvironment::home (el aviso).
    std::string display;  ///< SessionEnvironment::home (el título).
    std::vector<std::optional<std::string>> finished; ///< Lo de cada OnFinished.
    std::unique_ptr<CodeMode> mode;

    Harness() {
        fs::create_directories(project);
        write_file(project / "a.txt", "alfa\nbeta\n");
        write_file(project / "b.txt", "gamma\n");
    }

    CodeModeEnvironment environment() {
        chatbot::cli::SessionEnvironment session = fakes.environment();
        session.home = display;
        return CodeModeEnvironment{
            .session = std::move(session),
            .app_data_dir = std::nullopt,
            .portable_dir = std::nullopt,
            .home = home,
        };
    }

    /// Como la pantalla: open_code_project y luego CodeMode. git no existe, salvo con
    /// git->block: entonces el ls-files de glob espera hasta que lo cancelan.
    void start(const chatbot::Config& config) {
        CodeModeEnvironment env = environment();
        const chatbot::Result<CodeProject> opened = chatbot::cli::open_code_project(
            utf8(project), chatbot::cli::code_paths(env), [] { return std::time_t{1700000000}; });
        REQUIRE(opened.is_ok());
        CodeProject value = opened.value();
        value.prompt_options.git_path = "/fake/git";
        value.prompt_options.run_process = [probe = git](
                                               const std::vector<std::string>& argv,
                                               const chatbot::process::ProcessOptions& options) {
            chatbot::process::ProcessResult result;
            result.status = chatbot::process::ProcessResult::Status::NotFound;
            if (!probe->block || std::find(argv.begin(), argv.end(), "ls-files") == argv.end()) {
                return result;
            }
            probe->listing.cancel();
            const auto deadline =
                std::chrono::steady_clock::now() + 2 * chatbot_test::kThreadTimeout;
            while (options.cancel != nullptr &&
                   !options.cancel->wait_for(std::chrono::milliseconds{20}) &&
                   std::chrono::steady_clock::now() < deadline) {
            }
            result.status = chatbot::process::ProcessResult::Status::Cancelled;
            return result;
        };
        mode = std::make_unique<CodeMode>(
            std::move(env), config, std::move(value),
            [this](std::function<void()> task) { queue.post(std::move(task)); },
            [this](std::optional<std::string> restored) { finished.push_back(std::move(restored)); });
    }

    void start() { start(chatbot_test::test_config()); }

    /// Vacía la cola hasta el fin del turno número count + 1.
    bool wait_turn(std::size_t count) {
        return queue.run_until([this, count] { return finished.size() > count; });
    }

    void run_turn(const std::string& text) {
        const std::size_t count = finished.size();
        REQUIRE(mode->send(text));
        REQUIRE(wait_turn(count));
    }

    [[nodiscard]] const std::vector<Entry>& entries() const { return mode->entries(); }

    [[nodiscard]] std::vector<EntryKind> kinds() const {
        std::vector<EntryKind> result;
        for (const Entry& entry : entries()) {
            result.push_back(entry.kind);
        }
        return result;
    }

    /// Con key de NVIDIA guardada y config.json en nvidia/m1 (como test_session).
    void save_nvidia() const {
        chatbot::Credentials credentials;
        credentials.keys["nvidia"] = kNvidiaKey;
        REQUIRE_FALSE(chatbot::save_credentials(fakes.credentials_path(), credentials).has_value());
        REQUIRE_FALSE(chatbot::save_config_file(fakes.config_path(),
                                                {"nvidia", "https://integrate.api.nvidia.com/v1",
                                                 "m1"})
                          .has_value());
    }
};

/// El historial que se puede mandar: ningún User al final ni dos seguidos, y cada llamada con
/// su resultado justo después.
bool valid_history(const std::vector<chatbot::Message>& history) {
    for (std::size_t i = 0; i < history.size(); ++i) {
        const chatbot::Message& message = history[i];
        if (message.role == Role::User && i + 1 < history.size() &&
            history[i + 1].role == Role::User) {
            return false;
        }
        for (std::size_t call = 0; call < message.tool_calls.size(); ++call) {
            const std::size_t at = i + 1 + call;
            if (at >= history.size() || history[at].role != Role::Tool ||
                history[at].tool_call_id != message.tool_calls[call].id) {
                return false;
            }
        }
    }
    return history.empty() || history.back().role != Role::User;
}

bool has_user(const std::vector<chatbot::Message>& history) {
    return std::any_of(history.begin(), history.end(),
                       [](const chatbot::Message& message) { return message.role == Role::User; });
}

/// Nombre → (contenido, fecha de modificación) de cada archivo bajo dir.
std::map<std::string, std::pair<std::string, fs::file_time_type>> snapshot(const fs::path& dir) {
    std::map<std::string, std::pair<std::string, fs::file_time_type>> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir)) {
        std::ifstream in{entry.path(), std::ios::binary};
        files[utf8(entry.path())] = {
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()},
            entry.last_write_time()};
    }
    return files;
}

} // namespace

TEST_CASE("CodeMode: tres pasos y todo_write, con sus entradas y tareas", "[code_mode]") {
    Harness h;
    h.fakes.responses = {
        calls({{"glob", R"({"pattern":"*.txt"})"}}, "Voy a buscar."),
        calls({{"read", R"({"path":"a.txt"})"}}),
        calls({{"todo_write",
                R"({"todos":[{"content":"Leer","status":"completed","active_form":"Leyendo"},)"
                R"({"content":"Responder","status":"in_progress","active_form":"Respondiendo"}]})"}},
              "Anoto."),
        answer("Hay dos archivos."),
    };
    h.start();
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice});
    CHECK(h.entries()[0].text == "Sin AGENTS.md en esta carpeta");
    CHECK(h.mode->tasks().empty());
    CHECK(h.mode->status().activity.empty());

    h.run_turn("hola");
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::User,
                                                EntryKind::Assistant, EntryKind::Tool,
                                                EntryKind::Tool, EntryKind::Assistant,
                                                EntryKind::Tool, EntryKind::Assistant});
    const std::vector<Entry>& entries = h.entries();
    CHECK(entries[1].text == "hola");
    CHECK(entries[2].text == "Voy a buscar.");
    CHECK(entries[3].tool.summary.starts_with("Buscar"));
    CHECK(entries[3].tool.state == ToolState::Done);
    CHECK_FALSE(entries[3].tool.result.empty());
    CHECK(entries[3].tool.output.find("a.txt") != std::string::npos);
    CHECK(entries[4].tool.summary == "Leer a.txt");
    CHECK(entries[4].tool.state == ToolState::Done);
    CHECK(entries[4].tool.output.find("alfa") != std::string::npos);
    CHECK(entries[5].text == "Anoto.");
    CHECK(entries[6].tool.state == ToolState::Done);
    CHECK(entries[7].text == "Hay dos archivos.");
    for (const Entry& entry : entries) {
        CHECK_FALSE(entry.in_progress);
        CHECK_FALSE(entry.tool.expanded);
    }
    CHECK(h.finished == std::vector<std::optional<std::string>>{std::nullopt});
    CHECK(h.mode->state() == LoopState::Idle);
    CHECK(h.mode->status().activity.empty());
    CHECK(h.mode->status().mode == "Solo lectura");
    CHECK(h.mode->status().model == "modelo-de-prueba");
    CHECK(valid_history(h.mode->history()));

    REQUIRE(h.mode->tasks().size() == 2);
    CHECK(h.mode->tasks()[0].state == TaskState::Done);
    CHECK(h.mode->tasks()[1].state == TaskState::Active);
    CHECK(h.mode->tasks()[1].active_form == "Respondiendo");
    // No todas hechas: siguen en el turno siguiente.
    h.run_turn("sigue");
    CHECK(h.mode->tasks().size() == 2);
}

TEST_CASE("CodeMode: la lista se esconde al empezar el turno si todas están hechas",
          "[code_mode]") {
    Harness h;
    h.fakes.responses = {
        calls({{"todo_write",
                R"({"todos":[{"content":"Leer","status":"completed","active_form":"Leyendo"}]})"}}),
        answer("Listo."),
        answer("Otra."),
    };
    h.start();
    h.run_turn("hola");
    REQUIRE(h.mode->tasks().size() == 1);
    REQUIRE(h.mode->send("otra"));
    CHECK(h.mode->tasks().empty());
    REQUIRE(h.wait_turn(1));
}

TEST_CASE("tasks_for_new_turn: vacía solo si todas están hechas", "[code_mode]") {
    using chatbot::cli::tasks_for_new_turn;
    const TaskItem done{TaskState::Done, "a", "A"};
    const TaskItem active{TaskState::Active, "b", "B"};
    const TaskItem pending{TaskState::Pending, "c", "C"};
    CHECK(tasks_for_new_turn({}).empty());
    CHECK(tasks_for_new_turn({done, done}).empty());
    CHECK(tasks_for_new_turn({done, active}).size() == 2);
    CHECK(tasks_for_new_turn({pending}).size() == 1);
}

TEST_CASE("CodeMode: varias llamadas en una respuesta, una con error", "[code_mode]") {
    Harness h;
    h.fakes.responses = {
        calls({{"read", R"({"path":"a.txt"})"}, {"read", R"({"path":"no-existe.txt"})"}}),
        answer("Listo."),
    };
    h.start();
    h.run_turn("hola");
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::User,
                                                EntryKind::Tool, EntryKind::Tool,
                                                EntryKind::Assistant});
    CHECK(h.entries()[2].tool.summary == "Leer a.txt");
    CHECK(h.entries()[2].tool.state == ToolState::Done);
    CHECK(h.entries()[3].tool.summary == "Leer no-existe.txt");
    CHECK(h.entries()[3].tool.state == ToolState::Failed);
    CHECK_FALSE(h.entries()[3].tool.result.empty());
    CHECK_FALSE(h.entries()[3].tool.output.empty());
    CHECK(valid_history(h.mode->history()));
}

TEST_CASE("CodeMode: el límite de pasos y el error del modelo quedan en la pantalla",
          "[code_mode]") {
    Harness h;
    SECTION("límite de pasos: Notice") {
        write_file(h.fakes.config_path(), R"({"code":{"max_steps":1}})");
        h.fakes.responses = {calls({{"read", R"({"path":"a.txt"})"}}), answer("no")};
        h.start();
        h.run_turn("hola");
        REQUIRE(h.entries().back().kind == EntryKind::Notice);
        CHECK(h.entries().back().text.find("Se detuvo") != std::string::npos);
        CHECK(h.entries()[h.entries().size() - 2].tool.state == ToolState::Done);
    }
    SECTION("401: Error, y el texto vuelve") {
        h.fakes.responses = {
            HttpResponse{401, R"({"error":{"message":"mala key"}})", std::nullopt, false, ""}};
        h.start();
        h.run_turn("hola");
        CHECK(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::Error});
        CHECK(h.entries().back().text.starts_with("[error de autenticación]"));
        CHECK(h.finished == std::vector<std::optional<std::string>>{"hola"});
        CHECK_FALSE(has_user(h.mode->history()));
    }
}

TEST_CASE("CodeMode: cancelar durante la petición devuelve el texto", "[code_mode][cancelar]") {
    Harness h;
    chatbot_test::BlockingTransport* blocking = nullptr;
    h.fakes.block(blocking);
    h.start();
    REQUIRE(h.mode->send("hola"));
    REQUIRE(h.queue.run_until([&h] {
        return h.entries().back().kind == EntryKind::Assistant;
    }));
    CHECK(h.entries().back().in_progress);
    CHECK(h.mode->status().activity == "Pensando…");
    CHECK_FALSE(h.mode->config_changed()); // Con turno en curso no cambia nada.
    CHECK_FALSE(h.mode->clear());
    const auto begin = std::chrono::steady_clock::now();
    h.mode->cancel();
    REQUIRE(h.wait_turn(0));
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
    CHECK(h.finished == std::vector<std::optional<std::string>>{"hola"});
    // La entrada del usuario sale; la respuesta a medias queda marcada.
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::Assistant});
    CHECK(h.entries()[1].cancelled);
    CHECK_FALSE(h.entries()[1].in_progress);
    CHECK_FALSE(has_user(h.mode->history()));
    CHECK(valid_history(h.mode->history()));
}

TEST_CASE("CodeMode: cancelar a la mitad de una herramienta lenta", "[code_mode][cancelar]") {
    if (!chatbot::process::find_in_path("git").has_value()) {
        SKIP("Sin git en el PATH: glob no usa run_process.");
    }
    Harness h;
    h.fakes.responses = {calls({{"glob", R"({"pattern":"*.txt"})"}}), answer("No debería llegar.")};
    h.git->block = true;
    h.start();
    REQUIRE(h.mode->send("hola"));
    // La cola mueve el ciclo hasta que glob corre: su línea en curso y el estado Running.
    REQUIRE(h.queue.run_until([&h] { return h.mode->status().activity == "Buscando…"; }));
    REQUIRE(h.git->listing.wait_for(chatbot_test::kThreadTimeout));
    CHECK(h.mode->state() == LoopState::Running);
    REQUIRE(h.entries().back().kind == EntryKind::Tool);
    CHECK(h.entries().back().tool.state == ToolState::Running);
    const auto begin = std::chrono::steady_clock::now();
    h.mode->cancel();
    REQUIRE(h.wait_turn(0));
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
    // Ya se guardó la respuesta con la llamada: el texto no vuelve.
    CHECK(h.finished == std::vector<std::optional<std::string>>{std::nullopt});
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::User,
                                                EntryKind::Tool, EntryKind::Notice});
    CHECK(h.entries()[2].tool.state == ToolState::Failed);
    CHECK(h.entries()[3].text == "Respuesta cancelada.");
    CHECK(valid_history(h.mode->history()));
    REQUIRE_FALSE(h.mode->history().empty());
    CHECK(h.mode->history().back().role == Role::Tool);
    CHECK(h.mode->status().activity.empty());
}

TEST_CASE("CodeMode: toggle_tool y toggle_last_tool", "[code_mode]") {
    Harness h;
    h.fakes.responses = {
        calls({{"read", R"({"path":"a.txt"})"}, {"read", R"({"path":"b.txt"})"}}),
        answer("Listo."),
    };
    h.start();
    SECTION("sin herramientas") {
        CHECK_FALSE(h.mode->toggle_last_tool());
        CHECK_FALSE(h.mode->toggle_tool(0)); // Un Notice.
        CHECK_FALSE(h.mode->toggle_tool(99));
    }
    SECTION("con salida") {
        h.run_turn("hola");
        REQUIRE(h.entries()[2].kind == EntryKind::Tool);
        REQUIRE(h.entries()[3].kind == EntryKind::Tool);
        CHECK(h.mode->toggle_tool(2));
        CHECK(h.entries()[2].tool.expanded);
        CHECK_FALSE(h.entries()[3].tool.expanded);
        CHECK(h.mode->toggle_last_tool()); // La última con salida: la de b.txt.
        CHECK(h.entries()[3].tool.expanded);
        CHECK(h.mode->toggle_last_tool());
        CHECK_FALSE(h.entries()[3].tool.expanded);
        CHECK(h.mode->toggle_tool(2));
        CHECK_FALSE(h.entries()[2].tool.expanded);
        CHECK_FALSE(h.mode->toggle_tool(1)); // El mensaje del usuario.
    }
}

TEST_CASE("CodeMode: sin proveedor no envía; después de guardarlo, sí", "[code_mode]") {
    Harness h;
    h.fakes.responses = {answer("Hola.")};
    h.start(h.fakes.load()); // Sin config.json ni credentials.json.
    CHECK_FALSE(h.mode->configured());
    CHECK(h.mode->setup_problem() == chatbot::cli::kSetupNotice);
    CHECK_FALSE(h.mode->send("hola"));
    CHECK(h.kinds() == std::vector<EntryKind>{EntryKind::Notice});
    CHECK_FALSE(h.mode->config_changed());

    chatbot::Result<chatbot::cli::ProviderSettings> form = h.mode->session().provider_form();
    REQUIRE(form.is_ok());
    chatbot::cli::ProviderSettings settings = form.value();
    REQUIRE(settings.set_key(kNvidiaKey));
    REQUIRE(settings.set_model("m1"));
    REQUIRE(h.mode->session().save_provider(settings) == std::nullopt);
    CHECK(h.mode->configured());
    REQUIRE(h.mode->config_changed());
    h.run_turn("hola");
    CHECK(h.entries().back().text == "Hola.");
    CHECK(h.mode->status().model == "m1");
}

TEST_CASE("CodeMode: config_changed usa el cliente nuevo y conserva el historial",
          "[code_mode]") {
    using Status = chatbot::cli::ModelSwitch::Status;
    Harness h;
    h.save_nvidia();
    h.fakes.responses = {answer("Primera respuesta.")};
    h.start(h.fakes.load());
    h.run_turn("uno");
    const int clients = h.fakes.clients;
    REQUIRE(h.mode->session().switch_model(std::nullopt, "m2").status == Status::Switched);
    REQUIRE(h.mode->config_changed());
    CHECK(h.fakes.clients == clients + 2); // El de la Session y el del modo código.
    h.run_turn("dos");
    REQUIRE(h.fakes.chat != nullptr);
    REQUIRE(h.fakes.chat->requests.size() == 1);
    const json body = json::parse(h.fakes.chat->requests.back().body);
    CHECK(body["model"] == "m2");
    CHECK(body.dump().find("Primera respuesta.") != std::string::npos);
    CHECK(body.dump().find("uno") != std::string::npos);
    CHECK(h.mode->status().model == "m2");
    CHECK(h.mode->title().ends_with(" — m2"));
}

TEST_CASE("CodeMode no ensucia la carpeta de conversaciones", "[code_mode]") {
    using Status = chatbot::cli::ModelSwitch::Status;
    Harness h;
    h.save_nvidia();
    h.fakes.responses = {answer("Hecho.")};
    const fs::path conversations = h.fakes.path("conversations");
    const bool existed = GENERATE(false, true);
    std::map<std::string, std::pair<std::string, fs::file_time_type>> before;
    if (existed) {
        fs::create_directories(conversations);
        write_file(conversations / "vieja.json", R"({"version":1})");
        before = snapshot(conversations);
    }

    h.start(h.fakes.load());
    REQUIRE(h.mode->session().switch_model(std::nullopt, "m2").status == Status::Switched);
    chatbot::cli::ProviderSettings form = h.mode->session().provider_form().value();
    REQUIRE(form.set_model("m3"));
    REQUIRE(h.mode->session().save_provider(form) == std::nullopt);
    REQUIRE(h.mode->config_changed());
    h.run_turn("hola");
    CHECK(h.entries().back().text == "Hecho.");
    h.mode.reset();

    if (existed) {
        CHECK(snapshot(conversations) == before);
    } else {
        CHECK_FALSE(fs::exists(conversations));
    }
}

TEST_CASE("CodeMode: clear empieza otra sesión con las instrucciones recalculadas",
          "[code_mode]") {
    Harness h;
    write_file(h.project / "AGENTS.md", "Usa tabuladores.\n");
    h.fakes.responses = {answer("Hecho.")};
    h.start();
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice});
    CHECK(h.entries()[0].text == "AGENTS.md cargado");
    CHECK(h.mode->instructions().project == chatbot::code::ProjectInstructions::AgentsMd);
    h.run_turn("hola");
    REQUIRE(has_user(h.mode->history()));

    fs::remove(h.project / "AGENTS.md");
    write_file(h.project / "CLAUDE.md", "Usa espacios.\n");
    REQUIRE(h.mode->clear());
    REQUIRE(h.kinds() == std::vector<EntryKind>{EntryKind::Notice});
    CHECK(h.entries()[0].text == "CLAUDE.md cargado");
    CHECK(h.mode->instructions().project == chatbot::code::ProjectInstructions::ClaudeMd);
    CHECK_FALSE(has_user(h.mode->history()));
    CHECK(h.mode->tasks().empty());
    // La sesión nueva funciona.
    h.run_turn("otra vez");
    CHECK(h.entries().back().text == "Hecho.");
}

TEST_CASE("CodeMode: avisos de arranque (carpeta personal, límites, recorte)", "[code_mode]") {
    Harness h;
    h.home = utf8(h.project);
    write_file(h.fakes.config_path(), R"({"code":{"max_steps":0}})");
    write_file(h.project / "AGENTS.md", std::string(40 * 1024 + 100, 'x'));
    h.start();
    std::string all;
    for (const Entry& entry : h.entries()) {
        CHECK(entry.kind == EntryKind::Notice);
        all += entry.text + "\n";
    }
    CHECK(h.entries()[0].text == "AGENTS.md cargado");
    CHECK(all.find("carpeta personal") != std::string::npos);
    CHECK(all.find("max_steps") != std::string::npos);
    CHECK(all.find(chatbot::cli::kInstructionsTruncated) != std::string::npos);
}

TEST_CASE("CodeMode: open_code_project rechaza la carpeta de Moss", "[code_mode]") {
    Harness h;
    const chatbot::cli::CodePaths paths = chatbot::cli::code_paths(h.environment());
    CHECK(chatbot::cli::open_code_project(h.fakes.dir.path().string(), paths, {}).is_error());
    CHECK(chatbot::cli::open_code_project(utf8(h.projects.path() / "nada"), paths, {}).is_error());
    CHECK(chatbot::cli::open_code_project(utf8(h.project), paths, {}).is_ok());
}

TEST_CASE("code_title abrevia la carpeta personal con ~", "[code_mode]") {
    using chatbot::cli::code_title;
    CHECK(code_title("/home/ana/proy", "/home/ana", "m") == "Moss Code — ~/proy — m");
    CHECK(code_title("/home/ana", "/home/ana", "m") == "Moss Code — ~ — m");
    CHECK(code_title("/srv/proy", "/home/ana", "m") == "Moss Code — /srv/proy — m");
    CHECK(code_title("/home/anabel/proy", "/home/ana", "m") ==
          "Moss Code — /home/anabel/proy — m");
    // Windows: home vacía, ruta completa.
    CHECK(code_title("C:\\Users\\ana\\proy", "", "m") == "Moss Code — C:\\Users\\ana\\proy — m");
    CHECK(code_title("/home/ana/proy", "/home/ana", "") == "Moss Code — ~/proy");

    Harness h;
    h.display = utf8(fs::canonical(h.projects.path()));
    h.start();
    CHECK(h.mode->title() == "Moss Code — ~/proy — modelo-de-prueba");
}

TEST_CASE("code_status: modo, actividad y modelo", "[code_mode]") {
    using chatbot::cli::code_activity;
    CHECK(code_activity(LoopState::Idle, "read").empty());
    CHECK(code_activity(LoopState::Requesting, "") == "Pensando…");
    CHECK(code_activity(LoopState::Checking, "read") == "Pensando…");
    CHECK(code_activity(LoopState::Running, "read") == "Leyendo…");
    CHECK(code_activity(LoopState::Running, "glob") == "Buscando…");
    CHECK(code_activity(LoopState::Running, "grep") == "Buscando…");
    CHECK(code_activity(LoopState::Running, "todo_write") == "Anotando tareas…");
    CHECK(code_activity(LoopState::Running, "otra") == "Ejecutando…");
    const chatbot::cli::CodeStatus status =
        chatbot::cli::code_status(LoopState::Running, "read", "m1");
    CHECK(status.mode == "Solo lectura");
    CHECK(status.activity == "Leyendo…");
    CHECK(status.model == "m1");
}
