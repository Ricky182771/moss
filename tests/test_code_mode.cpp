// CodeMode (cli/src/code_mode.h): la lógica del modo código sin FTXUI. El modelo es un
// FakeTransport o un BlockingTransport (SessionFakes), post es una cola que la prueba vacía en su
// hilo, y las carpetas son temporales. Sin red. git del sistema solo se reemplaza en las
// instrucciones de sistema; glob lo usa por run_process, que la prueba inyecta.

#include "code_mode.h"

#include "blocking_transport.hpp"
#include "code_mode_fakes.hpp"
#include "fake_transport.hpp"
#include "session_fakes.hpp"
#include "temp_dir.hpp"

#include "chatbot/child_process.h"
#include "chatbot/credentials.h"
#include "chatbot/platform.h"

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
using chatbot::cli::Entry;
using chatbot::cli::EntryKind;
using chatbot::cli::TaskItem;
using chatbot::cli::TaskState;
using chatbot::cli::ToolState;
using chatbot::code::LoopState;
using chatbot_test::answer;
using chatbot_test::calls;
using chatbot_test::kNvidiaKey;
using chatbot_test::utf8;
using chatbot_test::write_file;
using Harness = chatbot_test::CodeModeHarness;
using json = nlohmann::json;
namespace fs = std::filesystem;

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

TEST_CASE("CodeMode: sigue ocupado entre el Idle del motor y su on_turn_end",
          "[code_mode][cancelar]") {
    // El motor pasa a Idle y manda on_state y on_turn_end por post: en FTXUI una tecla que ya
    // estaba en la cola se atiende antes que ellos y no debe ver el turno cerrado.
    using Type = chatbot::cli::SubmitOutcome::Type;
    Harness h;
    chatbot_test::BlockingTransport* blocking = nullptr;
    h.fakes.block(blocking);
    h.start();
    REQUIRE(h.mode->send("hola"));
    REQUIRE(h.queue.run_until([&h] {
        return h.entries().back().kind == EntryKind::Assistant;
    }));
    h.mode->cancel();
    // De una tarea en una, hasta la que deja el motor en Idle: el on_turn_end sigue en la cola.
    REQUIRE(h.queue.run_until([&h] { return h.mode->state() == LoopState::Idle; }));
    REQUIRE(h.finished.empty());
    CHECK(h.mode->busy());
    CHECK(h.mode->command_context().busy);
    CHECK_FALSE(h.mode->send("otra"));
    CHECK(h.mode->submit("otra").type == Type::Ignored);
    const chatbot::cli::SubmitOutcome cleared = h.mode->submit("/clear");
    CHECK(cleared.type == Type::Status);
    CHECK(cleared.status == chatbot::cli::kWaitOrCancel);
    CHECK_FALSE(h.mode->clear());
    CHECK_FALSE(h.mode->config_changed());
    CHECK(h.kinds() ==
          std::vector<EntryKind>{EntryKind::Notice, EntryKind::User, EntryKind::Assistant});

    REQUIRE(h.wait_turn(0));
    CHECK(h.finished == std::vector<std::optional<std::string>>{"hola"});
    CHECK(h.kinds() == std::vector<EntryKind>{EntryKind::Notice, EntryKind::Assistant});
    CHECK_FALSE(h.mode->busy());
    CHECK_FALSE(has_user(h.mode->history()));
    CHECK(valid_history(h.mode->history()));

    // Ya cerrado, el siguiente turno sí empieza y termina limpio.
    REQUIRE(h.mode->send("otra"));
    CHECK(h.kinds().back() == EntryKind::User);
    h.mode->cancel();
    REQUIRE(h.wait_turn(1));
    CHECK(h.finished.back() == std::optional<std::string>{"otra"});
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
    if (chatbot::current_os() == chatbot::Os::Windows) {
        // En Windows las rutas van completas, como en el chat.
        CHECK(h.mode->title().starts_with("Moss Code — "));
        CHECK(h.mode->title().ends_with("proy — modelo-de-prueba"));
    } else {
        CHECK(h.mode->title() == "Moss Code — ~/proy — modelo-de-prueba");
    }
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

// --- Comandos con "/" (CodeMode::submit) ---

TEST_CASE("CodeMode::submit: /clear, también con un turno en curso", "[code_mode][comandos]") {
    using Type = chatbot::cli::SubmitOutcome::Type;
    Harness h;
    SECTION("sin turno: sesión nueva y la caja se vacía") {
        h.fakes.responses = {answer("Hecho.")};
        h.start();
        h.run_turn("hola");
        REQUIRE(has_user(h.mode->history()));
        const chatbot::cli::SubmitOutcome outcome = h.mode->submit("/clear");
        CHECK(outcome.type == Type::Done);
        CHECK(outcome.clear);
        CHECK(h.kinds() == std::vector<EntryKind>{EntryKind::Notice});
        CHECK_FALSE(has_user(h.mode->history()));
        CHECK(h.mode->submit("/clear ya").status == chatbot::cli::kClearUsage);
    }
    SECTION("con un turno en curso: nada, y el aviso del chat") {
        chatbot_test::BlockingTransport* blocking = nullptr;
        h.fakes.block(blocking);
        h.start();
        REQUIRE(h.mode->send("hola"));
        CHECK(h.mode->command_context().busy);
        const chatbot::cli::SubmitOutcome outcome = h.mode->submit("/clear");
        CHECK(outcome.type == Type::Status);
        CHECK(outcome.status == chatbot::cli::kWaitOrCancel);
        CHECK_FALSE(outcome.clear);
        CHECK(h.mode->busy());
        // /model tampoco se abre.
        CHECK(h.mode->submit("/model").status == chatbot::cli::kWaitOrCancel);
        h.mode->cancel();
        REQUIRE(h.wait_turn(0));
    }
}

TEST_CASE("CodeMode::submit: /ayuda deja las teclas y los comandos del modo código",
          "[code_mode][comandos]") {
    Harness h;
    h.start();
    const chatbot::cli::SubmitOutcome outcome = h.mode->submit("/ayuda");
    CHECK(outcome.type == chatbot::cli::SubmitOutcome::Type::Done);
    CHECK(outcome.clear);
    REQUIRE(h.entries().back().kind == EntryKind::Notice);
    const std::string& text = h.entries().back().text;
    CHECK(text == chatbot::cli::code_help_text());
    CHECK(text.find("Ctrl+O · abre o cierra la salida completa de la última herramienta") !=
          std::string::npos);
    for (const char* shown : {"/model [proveedor] [modelo]", "/clear", "/ayuda", "Esc", "Ctrl+T",
                              "F2", "PgUp/PgDn", "Ctrl+C", "Alt+Enter"}) {
        INFO(shown);
        CHECK(text.find(shown) != std::string::npos);
    }
    for (const char* hidden : {"Shift+Tab", "/buscar", "/copiar", "/guardar", "/exportar"}) {
        INFO(hidden);
        CHECK(text.find(hidden) == std::string::npos);
    }
    CHECK(h.mode->submit("/ayuda ya").status == chatbot::cli::kHelpUsage);
    CHECK_FALSE(has_user(h.mode->history())); // Nada fue al modelo.
}

TEST_CASE("CodeMode::submit: /model proveedor modelo cambia, avisa y el turno usa el nuevo",
          "[code_mode][comandos]") {
    using Type = chatbot::cli::SubmitOutcome::Type;
    Harness h;
    h.save_nvidia();
    h.fakes.responses = {answer("Hecho.")};
    h.start(h.fakes.load());
    const int clients = h.fakes.clients;
    const chatbot::cli::SubmitOutcome outcome = h.mode->submit("/model nvidia m2");
    CHECK(outcome.type == Type::Done);
    CHECK(outcome.clear);
    CHECK(h.fakes.clients == clients + 2); // El de la Session y el del modo código (config_changed).
    REQUIRE(h.entries().back().kind == EntryKind::Notice);
    CHECK(h.entries().back().text == "Ahora se usa m2 de NVIDIA NIM.");
    CHECK(h.mode->status().model == "m2");
    // El mismo: sin cambio ni aviso nuevo.
    const std::size_t entries = h.entries().size();
    CHECK(h.mode->submit("/model m2").type == Type::Status);
    CHECK(h.entries().size() == entries);
    // Sin modelo: el selector.
    const chatbot::cli::SubmitOutcome picker = h.mode->submit("/model nvidia");
    CHECK(picker.type == Type::OpenModelPicker);
    CHECK(picker.picker_provider == "nvidia");
    CHECK(h.mode->submit("/model a b c").status == chatbot::cli::kModelUsage);

    h.run_turn("hola");
    REQUIRE(h.fakes.chat != nullptr);
    REQUIRE_FALSE(h.fakes.chat->requests.empty());
    CHECK(json::parse(h.fakes.chat->requests.back().body)["model"] == "m2");
}

TEST_CASE("CodeMode::submit: /model con CHAT_MODEL fijado avisa", "[code_mode][comandos]") {
    Harness h;
    h.save_nvidia();
    h.fakes.env.values["CHAT_MODEL"] = "modelo-del-entorno";
    h.start(h.fakes.load());
    CHECK(h.mode->command_context().model_locked);
    for (const char* text : {"/model", "/model m2"}) {
        INFO(text);
        const chatbot::cli::SubmitOutcome outcome = h.mode->submit(text);
        CHECK(outcome.type == chatbot::cli::SubmitOutcome::Type::Status);
        CHECK(outcome.status == "El modelo está fijado por CHAT_MODEL.");
    }
}

TEST_CASE("CodeMode::submit: un comando del chat va al modelo como texto",
          "[code_mode][comandos]") {
    Harness h;
    h.fakes.responses = {answer("No busco en la web.")};
    h.start();
    const std::size_t count = h.finished.size();
    const chatbot::cli::SubmitOutcome outcome = h.mode->submit("/buscar algo");
    CHECK(outcome.type == chatbot::cli::SubmitOutcome::Type::Started);
    CHECK(outcome.command == chatbot::cli::ParsedCommand::Type::Normal);
    CHECK(outcome.clear);
    REQUIRE(h.wait_turn(count));
    CHECK(h.entries()[1].kind == EntryKind::User);
    CHECK(h.entries()[1].text == "/buscar algo");
    CHECK(h.entries().back().text == "No busco en la web.");
    // Caja vacía: nada.
    CHECK(h.mode->submit("  ").type == chatbot::cli::SubmitOutcome::Type::Ignored);
}
