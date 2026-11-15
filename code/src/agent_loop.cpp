#include "agent_loop.h"

#include "chatbot/hide_secret.h"
#include "chatbot/trimmed_completion.h"

#include "tool_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace chatbot::code {

namespace {

/// Tope de un nombre de función en la API de OpenAI.
constexpr std::size_t kMaxToolNameBytes = 64;
/// Lo que queda en el historial en lugar de un nombre que el servidor no aceptaría.
constexpr std::string_view kInvalidToolName = "invalid_tool_name";
/// Tope de la copia de los argumentos rotos que se le regresa al modelo.
constexpr std::size_t kMaxEchoedArgumentsBytes = 200;

constexpr std::string_view kRejected = "The user rejected this action.";

bool valid_tool_name(std::string_view name) {
    return !name.empty() && name.size() <= kMaxToolNameBytes &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

/// true si raw es un objeto JSON (o vacío, que el registro toma como {}). stored: lo que va al
/// historial: raw si es un objeto; si no, "{}" (hay servidores que parsean los argumentos del
/// historial y responden 400).
bool object_arguments(std::string_view raw, std::string& stored) {
    if (raw.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        stored = "{}";
        return true;
    }
    if (nlohmann::json::parse(raw, nullptr, false).is_object()) {
        stored = std::string{raw};
        return true;
    }
    stored = "{}";
    return false;
}

ToolResult rejected(const std::string& reason) {
    std::string text{kRejected};
    if (!reason.empty()) {
        text += " Reason: " + reason;
    }
    return ToolResult{std::move(text), "Rechazada", ToolStatus::Error};
}

bool same_todos(const std::vector<TodoItem>& a, const std::vector<TodoItem>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const TodoItem& x, const TodoItem& y) {
                          return x.content == y.content && x.status == y.status &&
                                 x.active_form == y.active_form;
                      });
}

} // namespace

AgentLoop::AgentLoop(CodeSessionEnvironment& env, std::vector<Message>& history, ToolState& tools,
                     CodeSession::Post post, CodeObserver observer)
    : env_(env), history_(history), tools_(tools), post_(std::move(post)),
      observer_(std::move(observer)), options_{env.registry.specs(), true} {}

AgentLoop::~AgentLoop() {
    *alive_ = false;
    if (worker_.joinable()) {
        token_->cancel();
        worker_.join();
    }
    // La invariante también al destruir: lo que no tiene resultado recibe el de cancelación.
    answer_rest(cancelled_result());
}

bool AgentLoop::send(std::string text) {
    if (state_ != LoopState::Idle || text.empty()) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join(); // Ya terminó: su último aviso ya se atendió.
    }
    token_ = std::make_unique<CancelToken>();
    steps_ = 0;
    last_call_.clear();
    repeated_ = 0;
    invalid_ = 0;
    cancel_requested_ = false;
    history_.push_back(Message{Role::User, std::move(text)});
    start_request();
    return true;
}

void AgentLoop::cancel() {
    if (state_ == LoopState::AwaitingApproval) {
        answer(rejected({}));
        answer_rest(cancelled_result());
        finish(TurnOutcome{TurnEnd::Cancelled, {}, std::nullopt});
        return;
    }
    if ((state_ == LoopState::Requesting || state_ == LoopState::Running) && !cancel_requested_) {
        cancel_requested_ = true;
        token_->cancel();
    }
}

std::optional<ToolCallInfo> AgentLoop::pending_approval() const {
    if (state_ != LoopState::AwaitingApproval) {
        return std::nullopt;
    }
    return calls_[next_].info;
}

bool AgentLoop::respond_approval(ApprovalDecision decision, const std::string& reason) {
    if (state_ != LoopState::AwaitingApproval) {
        return false;
    }
    switch (decision) {
    case ApprovalDecision::Reject:
        if (!record(rejected(reason))) {
            next_call();
        }
        return true;
    case ApprovalDecision::AllowAlways:
        env_.permissions.allow_always(calls_[next_].info.name);
        break;
    case ApprovalDecision::Allow:
        break;
    }
    run_call();
    return true;
}

void AgentLoop::start_request() {
    if (steps_ >= env_.limits.max_steps) {
        stop(TurnEnd::StepLimit, {},
             "Se detuvo el turno: llegó al límite de " + std::to_string(env_.limits.max_steps) +
                 " peticiones al modelo (code.max_steps).");
        return;
    }
    ++steps_;
    set_state(LoopState::Requesting);
    launch([self = this, alive = alive_, observer = &observer_, post = post_,
            &client = *env_.client, history = history_, limit = env_.limits.history_limit_bytes,
            &options = options_, &token = *token_] {
        std::string text;
        std::string reasoning;
        const auto deliver = [&](const StreamDelta& delta) {
            std::string piece{delta.text}; // Copia: el string_view deja de valer al regresar.
            if (delta.kind == StreamDelta::Kind::Content) {
                text += piece;
            } else if (delta.kind == StreamDelta::Kind::Reasoning) {
                reasoning += piece;
            }
            post([alive, observer, kind = delta.kind, index = delta.tool_index,
                  piece = std::move(piece)] {
                if (!*alive) {
                    return;
                }
                if (kind == StreamDelta::Kind::ToolCallStarted) {
                    if (observer->on_tool_call_started) {
                        observer->on_tool_call_started(index, piece);
                    }
                } else if (observer->on_delta) {
                    observer->on_delta(kind, piece);
                }
            });
            return true;
        };
        TrimmedCompletion done = complete_trimmed(client, history, limit, deliver, &token, options);
        post([self, alive, result = std::move(done.result), text = std::move(text),
              reasoning = std::move(reasoning)]() mutable {
            if (*alive) {
                self->on_request_done(std::move(result), std::move(text), std::move(reasoning));
            }
        });
    });
}

void AgentLoop::on_request_done(Result<CompletionInfo> result, std::string text,
                                std::string reasoning) {
    // La respuesta a medias se descarta: nunca queda un asistente incompleto.
    if (cancel_requested_ || (result.is_error() && result.error().kind == ErrorKind::Cancelled)) {
        finish(TurnOutcome{TurnEnd::Cancelled, {}, std::nullopt});
        return;
    }
    if (result.is_error()) {
        finish(TurnOutcome{TurnEnd::Error, {}, result.error()});
        return;
    }
    const CompletionInfo& info = result.value();
    Message assistant{Role::Assistant, std::move(text)};
    assistant.reasoning = std::move(reasoning);
    assistant.reasoning_key = info.reasoning_key;
    if (info.tool_calls.empty()) {
        if (assistant.content.empty()) {
            // Un asistente vacío rompería todas las peticiones siguientes: no se guarda.
            finish(TurnOutcome{TurnEnd::Error,
                               {},
                               ChatError{ErrorKind::BadResponse, 0,
                                         "El modelo respondió sin texto ni llamadas a "
                                         "herramientas.",
                                         std::nullopt}});
            return;
        }
        std::string answer_text = assistant.content;
        history_.push_back(std::move(assistant));
        finish(TurnOutcome{TurnEnd::Answered, std::move(answer_text), std::nullopt});
        return;
    }
    calls_.clear();
    next_ = 0;
    for (std::size_t index = 0; index < info.tool_calls.size(); ++index) {
        const ToolCall& call = info.tool_calls[index];
        PendingCall pending;
        pending.raw_name = call.name;
        pending.raw_arguments = call.arguments;
        pending.valid_name = valid_tool_name(call.name);
        std::string stored_arguments;
        pending.valid_arguments = object_arguments(call.arguments, stored_arguments);
        pending.info.index = index;
        pending.info.id = call.id;
        pending.info.name = pending.valid_name ? call.name : std::string{kInvalidToolName};
        pending.info.label = env_.registry.label(call.name).value_or(std::string{});
        pending.info.summary = env_.registry.summarize(call.name, call.arguments);
        assistant.tool_calls.push_back(
            ToolCall{call.id, pending.info.name, std::move(stored_arguments)});
        calls_.push_back(std::move(pending));
    }
    history_.push_back(std::move(assistant));
    next_call();
}

void AgentLoop::next_call() {
    while (next_ < calls_.size()) {
        const PendingCall& call = calls_[next_];
        notify([info = call.info](const CodeObserver& observer) {
            if (observer.on_tool_ready) {
                observer.on_tool_ready(info);
            }
        });
        std::string signature = call.raw_name;
        signature += '\0';
        signature += call.raw_arguments;
        repeated_ = signature == last_call_ ? repeated_ + 1 : 1;
        last_call_ = std::move(signature);
        if (repeated_ >= env_.limits.max_repeated_calls) {
            const std::string times = std::to_string(repeated_);
            stop(TurnEnd::RepeatedCalls,
                 "the same call was repeated " + times + " times in a row, so the turn was stopped.",
                 "Se detuvo el turno: el modelo repitió la misma llamada " + times +
                     " veces seguidas.");
            return;
        }
        set_state(LoopState::Checking);
        if (!call.valid_name || !call.valid_arguments || !env_.registry.label(call.raw_name)) {
            if (record(invalid_call_result(call))) {
                return;
            }
            continue;
        }
        switch (env_.permissions.decide(call.raw_name, call.raw_arguments)) {
        case Permission::Allow:
            run_call();
            return;
        case Permission::Deny:
            if (record(rejected("A permission rule denies it."))) {
                return;
            }
            continue;
        case Permission::Ask:
            if (!env_.interactive) {
                if (record(rejected("It needs approval and nobody can approve it in "
                                    "non-interactive mode."))) {
                    return;
                }
                continue;
            }
            set_state(LoopState::AwaitingApproval);
            return;
        }
    }
    start_request();
}

void AgentLoop::run_call() {
    const PendingCall& call = calls_[next_];
    set_state(LoopState::Running);
    launch([self = this, alive = alive_, post = post_, &registry = env_.registry,
            &root = env_.root, &state = tools_, &token = *token_, secrets = env_.secrets,
            run_process = env_.prompt_options.run_process, name = call.raw_name,
            arguments = call.raw_arguments] {
        ToolContext context{root, token, state, secrets, run_process};
        ToolResult result = registry.execute(name, arguments, context);
        post([self, alive, result = std::move(result), todos = state.todos]() mutable {
            if (*alive) {
                self->on_tool_done(std::move(result), std::move(todos));
            }
        });
    });
}

void AgentLoop::on_tool_done(ToolResult result, std::vector<TodoItem> todos) {
    if (!same_todos(todos, todos_)) {
        todos_ = std::move(todos);
        notify([list = todos_](const CodeObserver& observer) {
            if (observer.on_todos) {
                observer.on_todos(list);
            }
        });
    }
    if (cancel_requested_) {
        answer_rest(cancelled_result());
        finish(TurnOutcome{TurnEnd::Cancelled, {}, std::nullopt});
        return;
    }
    if (!record(result)) {
        next_call();
    }
}

ToolResult AgentLoop::invalid_call_result(const PendingCall& call) {
    // El registro responde sin ejecutar nada: herramienta desconocida o argumentos que no son
    // un objeto. tools_ no se toca (y el hilo de trabajo no corre).
    ToolContext context{env_.root, *token_, tools_, env_.secrets, env_.prompt_options.run_process};
    ToolResult result = env_.registry.execute(call.raw_name, call.raw_arguments, context);
    if (!call.valid_arguments && result.status == ToolStatus::InvalidArguments) {
        std::string echoed = shorten(call.raw_arguments, kMaxEchoedArgumentsBytes);
        for (const std::string& secret : env_.secrets) {
            echoed = hide_secret(std::move(echoed), secret);
        }
        result.text += " Received: " + echoed;
    }
    return result;
}

bool AgentLoop::record(const ToolResult& result) {
    answer(result);
    const bool invalid = result.status == ToolStatus::InvalidArguments ||
                         result.status == ToolStatus::UnknownTool;
    invalid_ = invalid ? invalid_ + 1 : 0;
    if (invalid_ >= env_.limits.max_invalid_calls) {
        const std::string times = std::to_string(invalid_);
        stop(TurnEnd::InvalidCalls,
             times + " calls in a row were invalid, so the turn was stopped.",
             "Se detuvo el turno: el modelo mandó " + times +
                 " llamadas seguidas con argumentos inválidos o a herramientas que no existen.");
        return true;
    }
    return false;
}

void AgentLoop::answer(const ToolResult& result) {
    const ToolCallInfo& info = calls_[next_].info;
    Message message{Role::Tool, result.text};
    message.tool_call_id = info.id;
    history_.push_back(std::move(message));
    notify([info, result](const CodeObserver& observer) {
        if (observer.on_tool_result) {
            observer.on_tool_result(info, result);
        }
    });
    ++next_;
}

void AgentLoop::answer_rest(const ToolResult& result) {
    while (next_ < calls_.size()) {
        answer(result);
    }
}

void AgentLoop::stop(TurnEnd reason, const std::string& model_note, std::string notice) {
    answer_rest(ToolResult{"Not run: " + model_note, "No se ejecutó", ToolStatus::Error});
    notify([notice = std::move(notice)](const CodeObserver& observer) {
        if (observer.on_notice) {
            observer.on_notice(notice);
        }
    });
    finish(TurnOutcome{reason, {}, std::nullopt});
}

void AgentLoop::finish(TurnOutcome outcome) {
    calls_.clear();
    next_ = 0;
    cancel_requested_ = false;
    set_state(LoopState::Idle);
    notify([outcome = std::move(outcome)](const CodeObserver& observer) {
        if (observer.on_turn_end) {
            observer.on_turn_end(outcome);
        }
    });
}

void AgentLoop::set_state(LoopState state) {
    state_ = state;
    notify([state](const CodeObserver& observer) {
        if (observer.on_state) {
            observer.on_state(state);
        }
    });
}

void AgentLoop::notify(std::function<void(const CodeObserver&)> call) {
    if (!*alive_) {
        return;
    }
    // Siempre por post, también desde el hilo de la interfaz: el observador nunca corre a la
    // mitad de un cambio de estado y puede llamar a la API sin reentrar.
    post_([alive = alive_, observer = &observer_, call = std::move(call)] {
        if (*alive) {
            call(*observer);
        }
    });
}

void AgentLoop::launch(std::function<void()> job) {
    if (worker_.joinable()) {
        worker_.join(); // El anterior ya mandó su último aviso.
    }
    worker_ = std::thread(std::move(job));
}

} // namespace chatbot::code
