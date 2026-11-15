// El ciclo del agente (CodeSession y AgentLoop) con FakeTransport y guiones SSE de varios pasos:
// la invariante de un mensaje tool por llamada, la cancelación en cada estado, los límites, los
// errores del modelo y los permisos. post es una cola que la prueba vacía en su hilo (TaskQueue),
// así el orden es determinista; nada espera con sleep.

#include "chatbot/code/code_session.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"
#include "performance.hpp"
#include "project_root_fixture.hpp"
#include "tool_internal.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using chatbot::ErrorKind;
using chatbot::HttpResponse;
using chatbot::Message;
using chatbot::Role;
using chatbot::code::CodeLimits;
using chatbot::code::CodeObserver;
using chatbot::code::CodeSession;
using chatbot::code::CodeSessionEnvironment;
using chatbot::code::LoopState;
using chatbot::code::Permission;
using chatbot::code::PermissionEngine;
using chatbot::code::ProjectInstructions;
using chatbot::code::TodoItem;
using chatbot::code::TodoStatus;
using chatbot::code::Tool;
using chatbot::code::ToolCallInfo;
using chatbot::code::ToolContext;
using chatbot::code::ToolRegistry;
using chatbot::code::ToolResult;
using chatbot::code::ToolStatus;
using chatbot::code::TurnEnd;
using chatbot::code::TurnOutcome;
using chatbot::code::ApprovalDecision;
using chatbot_test::kThreadTimeout;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// --- Guiones SSE ---

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

HttpResponse answer_response(const std::string& text) {
    return ok(chunk({{"content", text}}) + kDone);
}

struct Call {
    std::string id;
    std::string name; ///< Vacío: el pedazo no trae nombre.
    std::string arguments;
};

/// Una respuesta con pensamiento y texto opcionales y estas llamadas, cada una en un pedazo.
HttpResponse calls_response(const std::vector<Call>& calls, const std::string& text = {},
                            const std::string& reasoning_key = {},
                            const std::string& reasoning = {}) {
    std::string flow;
    if (!reasoning.empty()) {
        flow += chunk({{reasoning_key, reasoning}});
    }
    if (!text.empty()) {
        flow += chunk({{"content", text}});
    }
    for (std::size_t index = 0; index < calls.size(); ++index) {
        json function = json::object();
        if (!calls[index].name.empty()) {
            function["name"] = calls[index].name;
        }
        function["arguments"] = calls[index].arguments;
        json call = json::object();
        call["index"] = index;
        call["id"] = calls[index].id;
        call["type"] = "function";
        call["function"] = function;
        flow += chunk({{"tool_calls", json::array({call})}});
    }
    return ok(flow + "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n" +
              kDone);
}

/// FakeTransport que puede dejar las primeras peticiones esperando la cancelación (como el
/// callback de progreso de curl), después de entregar un pedazo. Lo usa solo el hilo de trabajo;
/// la prueba lo lee después de que ese hilo avisó por la cola.
class GatedTransport final : public chatbot::Transport {
public:
    chatbot_test::FakeTransport inner;
    std::size_t block_first = 0;    ///< Cuántas peticiones (las primeras) esperan.
    std::string chunk_before_block; ///< Se entrega con 200 antes de esperar.
    chatbot::CancelToken blocked;   ///< Aviso: una petición está esperando.

    [[nodiscard]] HttpResponse send(const chatbot::HttpRequest& request) override {
        return inner.send(request);
    }

    [[nodiscard]] HttpResponse send_stream(const chatbot::HttpRequest& request,
                                           const chatbot::StreamCallback& on_chunk) override {
        if (handled_++ >= block_first) {
            return inner.send_stream(request, on_chunk);
        }
        inner.requests.push_back(request);
        HttpResponse cancelled;
        cancelled.error = "cancelado";
        cancelled.cancelled = true;
        cancelled.retryable = false;
        if (!chunk_before_block.empty() && !on_chunk(chunk_before_block, 200)) {
            return cancelled;
        }
        blocked.cancel();
        const auto deadline = Clock::now() + 2 * kThreadTimeout;
        while (request.cancel != nullptr &&
               !request.cancel->wait_for(std::chrono::milliseconds{20}) &&
               Clock::now() < deadline) {
        }
        return cancelled;
    }

private:
    std::size_t handled_ = 0;
};

// --- Herramientas de prueba ---

/// Lo que comparten las herramientas de prueba (corren en el hilo de trabajo) y la prueba.
struct Probe {
    std::mutex mutex;
    std::vector<std::string> counted_arguments; ///< Los argumentos de cada count, en orden.
    chatbot::CancelToken block_started;         ///< Aviso: block empezó.

    std::vector<std::string> counted() {
        const std::lock_guard<std::mutex> lock(mutex);
        return counted_arguments;
    }
};

const json kAnyObject = json::parse(R"({"type": "object"})");

/// Cuenta sus llamadas.
std::shared_ptr<const Tool> count_tool(const std::shared_ptr<Probe>& probe) {
    return std::make_shared<const Tool>(
        Tool{"count", "Counts.", kAnyObject, "Contar", {},
             [probe](const json& arguments, ToolContext&) {
                 const std::lock_guard<std::mutex> lock(probe->mutex);
                 probe->counted_arguments.push_back(arguments.dump());
                 return ToolResult{"count " + std::to_string(probe->counted_arguments.size()),
                                   "contado", ToolStatus::Ok};
             }});
}

/// Siempre falla.
std::shared_ptr<const Tool> fail_tool() {
    return std::make_shared<const Tool>(
        Tool{"fail", "Fails.", kAnyObject, "Fallar", {}, [](const json&, ToolContext&) {
                 return ToolResult{"Something failed.", "falló", ToolStatus::Error};
             }});
}

/// Se queda esperando hasta que la cancelan.
std::shared_ptr<const Tool> block_tool(const std::shared_ptr<Probe>& probe) {
    return std::make_shared<const Tool>(
        Tool{"block", "Blocks.", kAnyObject, "Esperar", {},
             [probe](const json&, ToolContext& context) {
                 probe->block_started.cancel();
                 const auto deadline = Clock::now() + 2 * kThreadTimeout;
                 while (!context.cancel.wait_for(std::chrono::milliseconds{20}) &&
                        Clock::now() < deadline) {
                 }
                 return chatbot::code::cancelled_result();
             }});
}

chatbot::code::SystemPromptOptions no_git_options() {
    chatbot::code::SystemPromptOptions options;
    options.now = [] { return std::time_t{1700000000}; };
    options.git_path = "/fake/git";
    options.run_process = [](const std::vector<std::string>&,
                             const chatbot::process::ProcessOptions&) {
        chatbot::process::ProcessResult result;
        result.status = chatbot::process::ProcessResult::Status::NotFound;
        return result;
    };
    return options;
}

Permission allow_all(std::string_view, std::string_view) {
    return Permission::Allow;
}

struct Options {
    CodeLimits limits{};
    PermissionEngine::Rule rule = allow_all;
    bool interactive = true;
};

/// Una sesión de código sobre un proyecto temporal, con su cola y lo que vio el observador.
struct Harness {
    project_root_test::Project project;
    chatbot_test::TaskQueue queue;
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    GatedTransport* transport = nullptr; ///< Es del cliente, que es de la sesión.
    std::thread::id ui_thread = std::this_thread::get_id();

    bool off_thread = false; ///< Algún aviso corrió fuera del hilo de la prueba.
    std::vector<std::string> texts;
    std::string reasoning;
    std::vector<std::pair<std::size_t, std::string>> started;
    std::vector<ToolCallInfo> ready;
    std::vector<std::pair<ToolCallInfo, ToolResult>> results;
    std::vector<std::vector<TodoItem>> todo_lists;
    std::vector<LoopState> states;
    std::vector<std::string> notices;
    std::vector<TurnOutcome> turns;
    /// Gancho de la prueba después de cada on_tool_result.
    std::function<void(const ToolCallInfo&)> after_result;

    std::unique_ptr<CodeSession> session; ///< Al final: se destruye primero.

    void open(const Options& options = {}) {
        auto gated = std::make_unique<GatedTransport>();
        transport = gated.get();
        auto client = std::make_unique<chatbot::ChatClient>(
            chatbot_test::test_config(), std::move(gated),
            std::make_unique<chatbot_test::FakeSleeper>());
        session = std::make_unique<CodeSession>(
            CodeSessionEnvironment{
                .root = project_root_test::open_root(project.proy, {project.moss}),
                .client = std::move(client),
                .registry = ToolRegistry{{count_tool(probe), fail_tool(), block_tool(probe),
                                          chatbot::code::make_todo_write_tool()}},
                .prompt_options = no_git_options(),
                .secrets = {},
                .limits = options.limits,
                .permissions = PermissionEngine{options.rule},
                .interactive = options.interactive,
            },
            [this](CodeSession::Task task) { queue.post(std::move(task)); }, observer());
    }

    void mark() { off_thread |= std::this_thread::get_id() != ui_thread; }

    CodeObserver observer() {
        CodeObserver callbacks;
        callbacks.on_delta = [this](chatbot::StreamDelta::Kind kind, const std::string& text) {
            mark();
            if (kind == chatbot::StreamDelta::Kind::Content) {
                texts.push_back(text);
            } else {
                reasoning += text;
            }
        };
        callbacks.on_tool_call_started = [this](std::size_t index, const std::string& name) {
            mark();
            started.emplace_back(index, name);
        };
        callbacks.on_tool_ready = [this](const ToolCallInfo& call) {
            mark();
            ready.push_back(call);
        };
        callbacks.on_tool_result = [this](const ToolCallInfo& call, const ToolResult& result) {
            mark();
            results.emplace_back(call, result);
            if (after_result) {
                after_result(call);
            }
        };
        callbacks.on_todos = [this](const std::vector<TodoItem>& todos) {
            mark();
            todo_lists.push_back(todos);
        };
        callbacks.on_state = [this](LoopState state) {
            mark();
            states.push_back(state);
        };
        callbacks.on_notice = [this](const std::string& notice) {
            mark();
            notices.push_back(notice);
        };
        callbacks.on_turn_end = [this](const TurnOutcome& outcome) {
            mark();
            turns.push_back(outcome);
        };
        return callbacks;
    }

    void respond(std::vector<HttpResponse> responses) {
        transport->inner.responses = std::move(responses);
    }

    /// Vacía la cola hasta que done() (con límite de tiempo).
    bool drain_until(const std::function<bool()>& done) { return queue.run_until(done); }

    /// Vacía la cola hasta el fin del turno número count + 1.
    bool wait_turn(std::size_t count) {
        return drain_until([this, count] { return turns.size() > count; });
    }

    TurnOutcome run_turn(const std::string& text) {
        const std::size_t count = turns.size();
        REQUIRE(session->send(text));
        REQUIRE(wait_turn(count));
        return turns.back();
    }

    /// Espera (vaciando la cola) a que la herramienta block esté corriendo.
    void wait_block_running() {
        REQUIRE(drain_until([this] {
            return !ready.empty() && ready.back().name == "block" &&
                   session->state() == LoopState::Running;
        }));
        REQUIRE(probe->block_started.wait_for(kThreadTimeout));
    }

    [[nodiscard]] json body(std::size_t request) const {
        REQUIRE(request < transport->inner.requests.size());
        return json::parse(transport->inner.requests[request].body);
    }

    [[nodiscard]] std::vector<std::string> tool_contents() const {
        std::vector<std::string> contents;
        for (const Message& message : session->history()) {
            if (message.role == Role::Tool) {
                contents.push_back(message.content);
            }
        }
        return contents;
    }
};

/// Cada llamada tiene exactamente un resultado tool, con su id, no vacío, justo después de su
/// asistente y en orden; y no hay ningún tool suelto.
bool one_result_per_call(const std::vector<Message>& history) {
    for (std::size_t at = 0; at < history.size(); ++at) {
        const Message& message = history[at];
        if (message.role == Role::Tool) {
            return false;
        }
        for (std::size_t call = 0; call < message.tool_calls.size(); ++call) {
            const std::size_t result = at + 1 + call;
            if (result >= history.size() || history[result].role != Role::Tool ||
                history[result].tool_call_id != message.tool_calls[call].id ||
                history[result].content.empty()) {
                return false;
            }
        }
        at += message.tool_calls.size();
    }
    return true;
}

/// Después de un turno que se cortó: un mensaje nuevo produce una petición que el núcleo valida y
/// manda, y el historial sigue cumpliendo la invariante.
void check_next_message_works(Harness& harness) {
    const std::size_t requests = harness.transport->inner.requests.size();
    harness.transport->inner.responses.push_back(answer_response("Sigo aquí."));
    const TurnOutcome next = harness.run_turn("¿sigues?");
    CHECK(next.reason == TurnEnd::Answered);
    CHECK(next.answer == "Sigo aquí.");
    CHECK(harness.transport->inner.requests.size() == requests + 1);
    CHECK(one_result_per_call(harness.session->history()));
}

const std::string kCancelled = "Cancelled by the user.";

} // namespace

TEST_CASE("code session: respuesta de solo texto, con las instrucciones y las herramientas",
          "[code_session]") {
    Harness harness;
    project_root_test::write_file(harness.project.proy / "AGENTS.md", "Usa tabuladores.");
    harness.open();
    harness.respond({answer_response("Hola.")});

    CHECK_FALSE(harness.session->send(""));
    const TurnOutcome turn = harness.run_turn("hola");
    CHECK(turn.reason == TurnEnd::Answered);
    CHECK(turn.answer == "Hola.");
    CHECK_FALSE(turn.error.has_value());
    CHECK_FALSE(harness.session->busy());

    CHECK(harness.session->instructions().project == ProjectInstructions::AgentsMd);
    const std::vector<Message>& history = harness.session->history();
    REQUIRE(history.size() == 3);
    CHECK(history[0].role == Role::System);
    CHECK(history[0].content == harness.session->instructions().text);
    CHECK(history[0].content.find("Usa tabuladores.") != std::string::npos);
    CHECK(history[1].content == "hola");
    CHECK(history[2].role == Role::Assistant);
    CHECK(history[2].content == "Hola.");

    const json body = harness.body(0);
    CHECK(body["tool_choice"] == "auto");
    REQUIRE(body["tools"].size() == 4);
    CHECK(body["tools"][0]["function"]["name"] == "count");
    CHECK(body["messages"][0]["content"] == harness.session->instructions().text);

    CHECK(harness.texts == std::vector<std::string>{"Hola."});
    CHECK(harness.states ==
          std::vector<LoopState>{LoopState::Requesting, LoopState::Idle});
    CHECK_FALSE(harness.off_thread);
}

TEST_CASE("code session: llamadas encadenadas, con el pensamiento y los tool en la petición",
          "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({
        calls_response({{"call_1", "count", R"({"n":1})"}}, "Voy", "reasoning", "pienso"),
        calls_response({{"call_2", "count", R"({"n":2})"}}),
        answer_response("Listo."),
    });

    const TurnOutcome turn = harness.run_turn("cuenta");
    CHECK(turn.reason == TurnEnd::Answered);
    CHECK(turn.answer == "Listo.");
    REQUIRE(harness.transport->inner.requests.size() == 3);

    const json second = harness.body(1)["messages"];
    REQUIRE(second.size() == 4);
    CHECK(second[2]["role"] == "assistant");
    CHECK(second[2]["content"] == "Voy");
    CHECK(second[2]["reasoning"] == "pienso"); // Con la llave en que llegó.
    CHECK_FALSE(second[2].contains("reasoning_content"));
    REQUIRE(second[2]["tool_calls"].size() == 1);
    CHECK(second[2]["tool_calls"][0]["id"] == "call_1");
    CHECK(second[2]["tool_calls"][0]["function"]["name"] == "count");
    CHECK(second[2]["tool_calls"][0]["function"]["arguments"] == R"({"n":1})");
    CHECK(second[3] == json{{"role", "tool"}, {"tool_call_id", "call_1"}, {"content", "count 1"}});

    const json third = harness.body(2)["messages"];
    REQUIRE(third.size() == 6);
    CHECK(third[4]["content"].is_null());
    CHECK(third[5]["tool_call_id"] == "call_2");
    CHECK(third[5]["content"] == "count 2");

    CHECK(harness.session->history().size() == 7);
    CHECK(one_result_per_call(harness.session->history()));
    CHECK(harness.probe->counted() == std::vector<std::string>{R"({"n":1})", R"({"n":2})"});
    CHECK(harness.reasoning == "pienso");
    CHECK(harness.started ==
          std::vector<std::pair<std::size_t, std::string>>{{0, "count"}, {0, "count"}});
    REQUIRE(harness.ready.size() == 2);
    CHECK(harness.ready[0].id == "call_1");
    CHECK(harness.ready[0].label == "Contar");
    CHECK(harness.ready[0].summary == "Contar");
    REQUIRE(harness.results.size() == 2);
    CHECK(harness.results[1].first.id == "call_2");
    CHECK(harness.results[1].second.display == "contado");
    CHECK_FALSE(harness.off_thread);
}

TEST_CASE("code session: varias llamadas en una respuesta, en orden", "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({
        calls_response({{"c1", "count", R"({"n":1})"}, {"c2", "fail", "{}"},
                        {"c3", "count", R"({"n":2})"}}),
        answer_response("Hecho."),
    });

    CHECK(harness.run_turn("varias").reason == TurnEnd::Answered);
    CHECK(harness.tool_contents() ==
          std::vector<std::string>{"count 1", "Something failed.", "count 2"});
    CHECK(harness.probe->counted() == std::vector<std::string>{R"({"n":1})", R"({"n":2})"});
    REQUIRE(harness.ready.size() == 3);
    CHECK(harness.ready[2].index == 2);
    REQUIRE(harness.results.size() == 3);
    CHECK(harness.results[1].second.status == ToolStatus::Error);
    CHECK(harness.results[2].second.status == ToolStatus::Ok);
    CHECK(one_result_per_call(harness.session->history()));
}

TEST_CASE("code session: cancelar durante la petición descarta la respuesta a medias",
          "[code_session][cancelar]") {
    Harness harness;
    harness.open();
    harness.transport->block_first = 1;
    harness.transport->chunk_before_block = chunk({{"content", "a medias"}});

    REQUIRE(harness.session->send("hola"));
    REQUIRE(harness.transport->blocked.wait_for(kThreadTimeout));
    CHECK(harness.session->busy());
    CHECK(harness.session->state() == LoopState::Requesting);
    CHECK_FALSE(harness.session->send("otro")); // Ocupada.

    const auto start = Clock::now();
    harness.session->cancel();
    REQUIRE(harness.wait_turn(0));
    chatbot_test::report_latency("cancelar durante la petición", Clock::now() - start);
    CHECK(harness.turns.back().reason == TurnEnd::Cancelled);
    REQUIRE(harness.session->history().size() == 2); // Sistema y usuario: sin asistente a medias.
    CHECK(harness.session->history()[1].role == Role::User);

    check_next_message_works(harness);
}

TEST_CASE("code session: cancelar durante una herramienta con otras pendientes",
          "[code_session][cancelar]") {
    Harness harness;
    harness.open();
    harness.respond({calls_response(
        {{"c1", "count", "{}"}, {"c2", "block", "{}"}, {"c3", "count", R"({"n":3})"}})});

    REQUIRE(harness.session->send("corre"));
    harness.wait_block_running();
    const auto start = Clock::now();
    harness.session->cancel();
    REQUIRE(harness.wait_turn(0));
    chatbot_test::report_latency("cancelar durante una herramienta", Clock::now() - start);

    CHECK(harness.turns.back().reason == TurnEnd::Cancelled);
    CHECK(harness.tool_contents() == std::vector<std::string>{"count 1", kCancelled, kCancelled});
    CHECK(harness.probe->counted().size() == 1);
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: cancelar esperando aprobación termina el turno ya",
          "[code_session][cancelar]") {
    Harness harness;
    Options options;
    options.rule = [](std::string_view name, std::string_view) {
        return name == "count" ? Permission::Ask : Permission::Allow;
    };
    harness.open(options);
    harness.respond({calls_response({{"c1", "count", "{}"}, {"c2", "count", R"({"n":2})"}})});

    REQUIRE(harness.session->send("corre"));
    REQUIRE(harness.drain_until(
        [&] { return harness.session->state() == LoopState::AwaitingApproval; }));
    const std::optional<ToolCallInfo> pending = harness.session->pending_approval();
    REQUIRE(pending.has_value());
    CHECK(pending->id == "c1");
    CHECK(pending->name == "count");

    harness.session->cancel();
    CHECK_FALSE(harness.session->busy());
    CHECK_FALSE(harness.session->pending_approval().has_value());
    REQUIRE(harness.wait_turn(0));
    CHECK(harness.turns.back().reason == TurnEnd::Cancelled);
    CHECK(harness.tool_contents() ==
          std::vector<std::string>{"The user rejected this action.", kCancelled});
    CHECK(harness.probe->counted().empty());
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: cancelar entre dos llamadas", "[code_session][cancelar]") {
    Harness harness;
    harness.open();
    harness.respond({calls_response({{"c1", "count", R"({"n":1})"},
                                     {"c2", "count", R"({"n":2})"},
                                     {"c3", "count", R"({"n":3})"}})});
    // El aviso del primer resultado llega cuando la segunda ya salió al hilo de trabajo.
    harness.after_result = [&](const ToolCallInfo& call) {
        if (call.id == "c1") {
            harness.session->cancel();
        }
    };

    CHECK(harness.run_turn("corre").reason == TurnEnd::Cancelled);
    CHECK(harness.tool_contents() == std::vector<std::string>{"count 1", kCancelled, kCancelled});
    CHECK(one_result_per_call(harness.session->history()));

    harness.after_result = {};
    check_next_message_works(harness);
}

TEST_CASE("code session: argumentos rotos se guardan como {} y el modelo corrige",
          "[code_session]") {
    const std::string broken =
        GENERATE(std::string{R"({"n": 1)"}, std::string{"[1, 2]"}, std::string{R"("texto")"});
    Harness harness;
    harness.open();
    harness.respond({
        calls_response({{"c1", "count", broken}}),
        calls_response({{"c2", "count", R"({"n":1})"}}),
        answer_response("Listo."),
    });

    CHECK(harness.run_turn("cuenta").reason == TurnEnd::Answered);
    const std::vector<Message>& history = harness.session->history();
    REQUIRE(history.size() >= 4);
    REQUIRE(history[2].tool_calls.size() == 1);
    CHECK(history[2].tool_calls[0].arguments == "{}");
    CHECK(history[3].content.starts_with("Invalid arguments for count: "));
    CHECK(history[3].content.ends_with(" Received: " + broken));
    CHECK(harness.body(1)["messages"][2]["tool_calls"][0]["function"]["arguments"] == "{}");
    REQUIRE_FALSE(harness.results.empty());
    CHECK(harness.results[0].second.status == ToolStatus::InvalidArguments);
    CHECK(harness.probe->counted() == std::vector<std::string>{R"({"n":1})"});
    CHECK(one_result_per_call(history));
}

TEST_CASE("code session: nombre inválido o vacío se guarda con un nombre válido",
          "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({
        calls_response({{"c1", "rm -rf /", "{}"}, {"c2", "", R"({"a":1})"}}),
        answer_response("Listo."),
    });

    CHECK(harness.run_turn("borra").reason == TurnEnd::Answered);
    const std::vector<Message>& history = harness.session->history();
    REQUIRE(history[2].tool_calls.size() == 2);
    CHECK(history[2].tool_calls[0].name == "invalid_tool_name");
    CHECK(history[2].tool_calls[1].name == "invalid_tool_name");
    CHECK(history[2].tool_calls[1].arguments == R"({"a":1})");
    const std::string available = " Available tools: count, fail, block, todo_write.";
    CHECK(history[3].content == "Unknown tool: rm -rf /." + available);
    CHECK(history[4].content == "Unknown tool: ." + available);
    const json sent = harness.body(1)["messages"][2]["tool_calls"];
    CHECK(sent[0]["function"]["name"] == "invalid_tool_name");
    CHECK(sent[1]["function"]["name"] == "invalid_tool_name");
    REQUIRE(harness.ready.size() == 2);
    CHECK(harness.ready[0].summary == "Herramienta desconocida: rm -rf /");
    CHECK(harness.ready[0].label.empty());
    CHECK(harness.results[0].second.status == ToolStatus::UnknownTool);
    CHECK(one_result_per_call(history));
}

TEST_CASE("code session: herramienta desconocida", "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({calls_response({{"c1", "nope", "{}"}}), answer_response("Listo.")});

    CHECK(harness.run_turn("usa nope").reason == TurnEnd::Answered);
    const std::vector<Message>& history = harness.session->history();
    CHECK(history[2].tool_calls[0].name == "nope");
    CHECK(history[3].content ==
          "Unknown tool: nope. Available tools: count, fail, block, todo_write.");
    REQUIRE(harness.results.size() == 1);
    CHECK(harness.results[0].second.status == ToolStatus::UnknownTool);
    CHECK(harness.results[0].second.display == "Herramienta desconocida: nope");
}

TEST_CASE("code session: límite de peticiones por turno", "[code_session][limites]") {
    Harness harness;
    Options options;
    options.limits.max_steps = 2;
    harness.open(options);
    harness.respond({
        calls_response({{"c1", "count", R"({"n":1})"}}),
        calls_response({{"c2", "count", R"({"n":2})"}}),
    });

    CHECK(harness.run_turn("sin fin").reason == TurnEnd::StepLimit);
    CHECK(harness.transport->inner.requests.size() == 2);
    CHECK(harness.probe->counted().size() == 2);
    CHECK(harness.notices == std::vector<std::string>{
                                 "Se detuvo el turno: llegó al límite de 2 peticiones al modelo "
                                 "(code.max_steps)."});
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: la misma llamada tres veces seguidas detiene el turno",
          "[code_session][limites]") {
    Harness harness;
    harness.open();
    // FakeTransport repite la última respuesta: la misma llamada en cada paso (el núcleo cambia
    // los ids repetidos).
    harness.respond({calls_response({{"c1", "count", R"({"n":1})"}})});

    CHECK(harness.run_turn("repite").reason == TurnEnd::RepeatedCalls);
    CHECK(harness.transport->inner.requests.size() == 3);
    CHECK(harness.probe->counted().size() == 2); // La tercera no se ejecuta.
    const std::vector<std::string> contents = harness.tool_contents();
    REQUIRE(contents.size() == 3);
    CHECK(contents[2] ==
          "Not run: the same call was repeated 3 times in a row, so the turn was stopped.");
    CHECK(harness.notices ==
          std::vector<std::string>{
              "Se detuvo el turno: el modelo repitió la misma llamada 3 veces seguidas."});
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: tres llamadas inválidas seguidas detienen el turno",
          "[code_session][limites]") {
    Harness harness;
    harness.open();
    // Argumentos rotos, una herramienta desconocida y otros argumentos rotos; la cuarta queda
    // pendiente y recibe su resultado sin ejecutarse.
    harness.respond({calls_response({{"c1", "count", "{"},
                                     {"c2", "nope", "{}"},
                                     {"c3", "count", "[1"},
                                     {"c4", "count", R"({"n":1})"}})});

    CHECK(harness.run_turn("rompe").reason == TurnEnd::InvalidCalls);
    CHECK(harness.transport->inner.requests.size() == 1);
    CHECK(harness.probe->counted().empty());
    const std::vector<std::string> contents = harness.tool_contents();
    REQUIRE(contents.size() == 4);
    CHECK(contents[3] == "Not run: 3 calls in a row were invalid, so the turn was stopped.");
    REQUIRE(harness.notices.size() == 1);
    CHECK(harness.notices[0].starts_with("Se detuvo el turno: el modelo mandó 3 llamadas seguidas"));
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: 429 o error de red a la mitad del turno", "[code_session]") {
    const int status = GENERATE(429, 0);
    Harness harness;
    harness.open();
    HttpResponse failure{status, "", std::nullopt, false, status == 0 ? "sin red" : ""};
    harness.respond({calls_response({{"c1", "count", "{}"}}), failure});

    const TurnOutcome turn = harness.run_turn("cuenta");
    CHECK(turn.reason == TurnEnd::Error);
    REQUIRE(turn.error.has_value());
    CHECK(turn.error->kind == (status == 429 ? ErrorKind::RateLimited : ErrorKind::Network));
    // Lo completado (el paso con la herramienta) se queda.
    REQUIRE(harness.session->history().size() == 4);
    CHECK(harness.session->history()[3].content == "count 1");
    CHECK(one_result_per_call(harness.session->history()));

    check_next_message_works(harness);
}

TEST_CASE("code session: respuesta sin texto ni llamadas no se guarda", "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({ok(kDone)});

    const TurnOutcome turn = harness.run_turn("hola");
    CHECK(turn.reason == TurnEnd::Error);
    REQUIRE(turn.error.has_value());
    CHECK(turn.error->kind == ErrorKind::BadResponse);
    CHECK(harness.session->history().size() == 2);

    check_next_message_works(harness);
}

TEST_CASE("code session: preguntar, permitir y permitir siempre", "[code_session][permisos]") {
    Harness harness;
    Options options;
    options.rule = [](std::string_view name, std::string_view) {
        return name == "count" ? Permission::Ask : Permission::Allow;
    };
    harness.open(options);
    harness.respond({
        calls_response({{"c1", "count", R"({"n":1})"}}),
        calls_response({{"c2", "count", R"({"n":2})"}}),
        calls_response({{"c3", "count", R"({"n":3})"}}),
        answer_response("Listo."),
    });

    CHECK_FALSE(harness.session->respond_approval(ApprovalDecision::Allow)); // Nada pendiente.
    REQUIRE(harness.session->send("cuenta"));
    const auto awaiting = [&] { return harness.session->state() == LoopState::AwaitingApproval; };
    REQUIRE(harness.drain_until(awaiting));
    CHECK(harness.session->pending_approval()->id == "c1");
    CHECK(harness.probe->counted().empty());
    REQUIRE(harness.session->respond_approval(ApprovalDecision::Allow));

    REQUIRE(harness.drain_until(awaiting));
    CHECK(harness.session->pending_approval()->id == "c2");
    REQUIRE(harness.session->respond_approval(ApprovalDecision::AllowAlways));

    REQUIRE(harness.wait_turn(0)); // c3 ya no pregunta.
    CHECK(harness.turns.back().reason == TurnEnd::Answered);
    CHECK(harness.probe->counted().size() == 3);
    CHECK(std::count(harness.states.begin(), harness.states.end(),
                     LoopState::AwaitingApproval) == 2);
    CHECK(one_result_per_call(harness.session->history()));
}

TEST_CASE("code session: rechazar con motivo y el turno sigue", "[code_session][permisos]") {
    Harness harness;
    Options options;
    options.rule = [](std::string_view name, std::string_view) {
        return name == "count" ? Permission::Ask : Permission::Allow;
    };
    harness.open(options);
    harness.respond({calls_response({{"c1", "count", "{}"}}), answer_response("Entendido.")});

    REQUIRE(harness.session->send("cuenta"));
    REQUIRE(harness.drain_until(
        [&] { return harness.session->state() == LoopState::AwaitingApproval; }));
    REQUIRE(harness.session->respond_approval(ApprovalDecision::Reject, "no hace falta"));
    REQUIRE(harness.wait_turn(0));

    CHECK(harness.turns.back().reason == TurnEnd::Answered);
    CHECK(harness.tool_contents() ==
          std::vector<std::string>{"The user rejected this action. Reason: no hace falta"});
    CHECK(harness.transport->inner.requests.size() == 2);
    CHECK(harness.probe->counted().empty());
}

TEST_CASE("code session: Deny y el modo sin interfaz rechazan sin preguntar",
          "[code_session][permisos]") {
    Harness harness;
    Options options;
    std::string expected;
    SECTION("Deny") {
        options.rule = [](std::string_view name, std::string_view) {
            return name == "count" ? Permission::Deny : Permission::Allow;
        };
        expected = "The user rejected this action. Reason: A permission rule denies it.";
    }
    SECTION("sin interfaz, Ask se rechaza") {
        options.rule = [](std::string_view, std::string_view) { return Permission::Ask; };
        options.interactive = false;
        expected = "The user rejected this action. Reason: It needs approval and nobody can "
                   "approve it in non-interactive mode.";
    }
    harness.open(options);
    harness.respond({calls_response({{"c1", "count", "{}"}}), answer_response("Entendido.")});

    CHECK(harness.run_turn("cuenta").reason == TurnEnd::Answered);
    CHECK(harness.tool_contents() == std::vector<std::string>{expected});
    CHECK(std::count(harness.states.begin(), harness.states.end(),
                     LoopState::AwaitingApproval) == 0);
    CHECK(harness.probe->counted().empty());
}

TEST_CASE("code session: el recorte usa el límite del modo código, no el del chat",
          "[code_session][limites]") {
    Harness harness;
    Options options;
    std::size_t expected_messages = 0;
    SECTION("300 000 por defecto: cabe lo que el chat (32 000) quitaría") {
        expected_messages = 4;
    }
    SECTION("un límite pequeño sí recorta") {
        options.limits.history_limit_bytes = 1000;
        expected_messages = 2;
    }
    harness.open(options);
    harness.respond({answer_response("uno"), answer_response("dos")});

    CHECK(harness.run_turn(std::string(20000, 'a')).reason == TurnEnd::Answered);
    CHECK(harness.run_turn(std::string(20000, 'b')).reason == TurnEnd::Answered);
    CHECK(harness.body(1)["messages"].size() == expected_messages);
}

TEST_CASE("code session: lista de tareas", "[code_session]") {
    Harness harness;
    harness.open();
    harness.respond({
        calls_response({{"c1", "todo_write",
                         R"({"todos":[{"content":"Leer","status":"in_progress","active_form":"Leyendo"}]})"}}),
        answer_response("Listo."),
    });

    CHECK(harness.run_turn("planea").reason == TurnEnd::Answered);
    REQUIRE(harness.todo_lists.size() == 1);
    REQUIRE(harness.session->todos().size() == 1);
    CHECK(harness.session->todos()[0].content == "Leer");
    CHECK(harness.session->todos()[0].status == TodoStatus::InProgress);
}

TEST_CASE("code session: el destructor con una herramienta bloqueada termina en < 2 s",
          "[code_session][cancelar]") {
    Harness harness;
    harness.open();
    harness.respond({calls_response({{"c1", "block", "{}"}})});

    REQUIRE(harness.session->send("espera"));
    harness.wait_block_running();
    const auto start = Clock::now();
    harness.session.reset();
    chatbot_test::report_latency("destruir la sesión con una herramienta", Clock::now() - start);

    // Lo que quedó en la cola ya no hace nada.
    const std::size_t results = harness.results.size();
    harness.queue.run_until([] { return false; }, std::chrono::milliseconds{50});
    CHECK(harness.results.size() == results);
    CHECK(harness.turns.empty());
}
