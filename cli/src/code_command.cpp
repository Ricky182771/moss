#include "code_command.h"

#include "app_startup.h"
#include "code_mode.h"
#include "conversation_store.h"
#include "interrupt.h"

#include "chatbot/code/code_session.h"
#include "chatbot/curl_transport.h"
#include "chatbot/markdown/markdown.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace chatbot::cli {

namespace {

/// Texto que viene del modelo o de un archivo, para stderr: sin controles y en una línea.
std::string one_line(const std::string& text) {
    std::string clean = md::sanitize(text);
    std::replace(clean.begin(), clean.end(), '\n', ' ');
    return clean;
}

/// La respuesta para stdout: como md::sanitize (sin secuencias de escape, '\r' ni los demás
/// controles), pero conserva '\t' (sanitize lo pasa a espacios) y '\n'.
std::string keep_tabs(const std::string& text) {
    std::string clean;
    for (std::size_t start = 0;;) {
        const std::size_t tab = text.find('\t', start);
        clean += md::sanitize(text.substr(start, tab - start));
        if (tab == std::string::npos) {
            return clean;
        }
        clean += '\t';
        start = tab + 1;
    }
}

void print_error(std::ostream& err, const ChatError& error) {
    err << "[" << error_kind_label(error.kind) << "] " << one_line(error.message) << '\n';
}

/// Cola de tareas del hilo principal: post la llama cualquier hilo.
class TaskQueue {
public:
    using Task = std::function<void()>;

    void post(Task task) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        condition_.notify_one();
    }

    /// La siguiente tarea, esperando como mucho wait; nullopt si no llegó ninguna.
    std::optional<Task> pop(std::chrono::milliseconds wait) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, wait, [this] { return !tasks_.empty(); });
        if (tasks_.empty()) {
            return std::nullopt;
        }
        Task task = std::move(tasks_.front());
        tasks_.pop_front();
        return task;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Task> tasks_;
};

// "●" (U+25CF); no está en la tabla de símbolos de glyphs.cpp y esta salida va a stderr, sin
// pantalla ni modo Sencillos. Escapado porque test_glyphs prohíbe ese carácter en cli/src.
constexpr const char* kToolMark = "\u25CF ";

/// El tiempo máximo que el hilo principal espera una tarea antes de mirar si hubo Ctrl+C.
constexpr std::chrono::milliseconds kPollInterval{50};

} // namespace

int run_code(const CommandLine& line, CodeEnvironment& environment) {
    std::ostream& err = environment.err;

    // Configuración, como la app (también en portable): sin key o sin modelo, el error Config.
    const Result<Config> loaded = environment.load_config();
    if (loaded.is_error()) {
        print_error(err, loaded.error());
        return kExitCodeError;
    }
    const Config& config = loaded.value();
    if (const std::optional<ChatError> incomplete = validate_config(config)) {
        print_error(err, *incomplete);
        return kExitCodeError;
    }
    for (const std::string& warning : config.warnings) {
        err << one_line(warning) << '\n';
    }

    // La carpeta, los límites y las instrucciones personales, como la pantalla (code_mode.h).
    const CodePaths paths{
        .config_path = environment.config_path,
        .credentials_path = environment.credentials_path,
        .data_dir = environment.data_dir,
        .app_data_dir = environment.app_data_dir,
        .portable_dir = environment.portable_dir,
        .home = environment.home,
        .env_secrets = environment.env_secrets,
    };
    const Result<CodeProject> project = open_code_project(line.folder, paths, environment.now);
    if (project.is_error()) {
        print_error(err, project.error());
        return kExitCodeError;
    }
    for (const std::string& notice : project.value().notices) {
        err << one_line(notice) << '\n';
    }

    // Se destruye al revés: la sesión primero (su hilo de trabajo usa la cola y los flujos).
    TaskQueue queue;
    std::optional<code::TurnOutcome> outcome;
    code::CodeObserver observer;
    observer.on_tool_ready = [&err](const code::ToolCallInfo& call) {
        err << kToolMark << one_line(call.summary) << '\n';
    };
    observer.on_tool_result = [&err](const code::ToolCallInfo&, const code::ToolResult& result) {
        if (result.is_error()) {
            err << "  [error] " << one_line(result.display) << '\n';
        }
    };
    observer.on_notice = [&err](const std::string& notice) { err << one_line(notice) << '\n'; };
    observer.on_turn_end = [&outcome](const code::TurnOutcome& end) { outcome = end; };

    code::CodeSessionEnvironment session_environment{
        .root = project.value().root,
        .client = environment.make_client(config),
        .prompt_options = project.value().prompt_options,
        .secrets = code_secrets(config, paths),
        .limits = project.value().limits,
        .interactive = false,
    };
    code::CodeSession session{std::move(session_environment),
                              [&queue](code::CodeSession::Task task) { queue.post(std::move(task)); },
                              std::move(observer)};
    if (session.instructions().instructions_truncated) {
        err << kInstructionsTruncated << '\n';
    }
    if (!session.send(line.prompt.value_or(std::string{}))) {
        err << "[Config] No se pudo empezar el turno.\n";
        return kExitCodeError;
    }

    bool cancel_sent = false;
    while (!outcome.has_value()) {
        if (!cancel_sent && environment.interrupted && environment.interrupted()) {
            session.cancel();
            cancel_sent = true;
        }
        if (std::optional<TaskQueue::Task> task = queue.pop(kPollInterval)) {
            (*task)();
        }
    }

    switch (outcome->reason) {
    case code::TurnEnd::Answered:
        environment.out << keep_tabs(outcome->answer) << '\n' << std::flush;
        return 0;
    case code::TurnEnd::Error:
        if (outcome->error.has_value()) {
            print_error(err, *outcome->error);
        }
        return kExitCodeError;
    case code::TurnEnd::Cancelled:
        err << "Interrumpido.\n";
        return kExitInterrupted;
    case code::TurnEnd::StepLimit:
    case code::TurnEnd::RepeatedCalls:
    case code::TurnEnd::InvalidCalls:
        break; // El aviso ya salió por on_notice.
    }
    return kExitCodeError;
}

int run_code_command(const CommandLine& line) {
    const InterruptGuard guard;
    CodeEnvironment environment{
        .out = std::cout,
        .err = std::cerr,
        .load_config = load_startup_config,
        .config_path = default_config_path(),
        .credentials_path = default_credentials_path(),
        .data_dir = default_data_dir(),
        .app_data_dir = default_app_data_dir(),
        .portable_dir = process_portable_data_dir(),
        .home = personal_home(),
        .env_secrets = {std::string{process_env("CHAT_SEARCH_API_KEY")},
                        std::string{process_env("CHAT_TELEGRAM_TOKEN")}},
        .make_client =
            [](const Config& config) {
                return std::make_unique<ChatClient>(config, std::make_unique<CurlTransport>());
            },
        .interrupted = InterruptGuard::requested,
        .now = [] { return std::time(nullptr); },
    };
    return run_code(line, environment);
}

} // namespace chatbot::cli
