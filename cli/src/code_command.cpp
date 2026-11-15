#include "code_command.h"

#include "app_startup.h"
#include "conversation_store.h"
#include "interrupt.h"

#include "chatbot/code/code_limits.h"
#include "chatbot/code/code_session.h"
#include "chatbot/curl_transport.h"
#include "chatbot/markdown/markdown.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace chatbot::cli {

namespace {

namespace fs = std::filesystem;

// Los strings de la app son UTF-8; fs::path en Windows no lo es (string() usa la página ANSI).
fs::path path_of(const std::string& text) {
    std::u8string utf8(text.size(), u8'\0');
    for (std::size_t i = 0; i < text.size(); ++i) {
        utf8[i] = static_cast<char8_t>(static_cast<unsigned char>(text[i]));
    }
    return fs::path{utf8};
}

std::string utf8_of(const fs::path& path) {
    const std::u8string utf8 = path.u8string();
    std::string text(utf8.size(), '\0');
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        text[i] = static_cast<char>(static_cast<unsigned char>(utf8[i]));
    }
    return text;
}

/// Texto que viene del modelo o de un archivo, para stderr: sin controles y en una línea.
std::string one_line(const std::string& text) {
    std::string clean = md::sanitize(text);
    std::replace(clean.begin(), clean.end(), '\n', ' ');
    return clean;
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
    if (!line.prompt.has_value()) {
        err << "La pantalla de Moss Code llega en C2; por ahora usa: moss code -p \"…\"\n";
        return kExitUsage;
    }

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

    // La raíz: la indicada o la actual; nunca la carpeta de Moss (configuración ni datos).
    std::error_code ignored;
    const std::string folder =
        line.folder.empty() ? utf8_of(fs::current_path(ignored)) : line.folder;
    std::vector<std::string> blocked;
    for (const std::optional<std::string>& file : {environment.config_path,
                                                   environment.credentials_path}) {
        if (file.has_value()) {
            blocked.push_back(utf8_of(path_of(*file).parent_path()));
        }
    }
    for (const std::optional<std::string>& dir : {environment.data_dir, environment.portable_dir}) {
        if (dir.has_value()) {
            blocked.push_back(*dir);
        }
    }
    const Result<code::ProjectRoot> root = code::ProjectRoot::open(folder, blocked);
    if (root.is_error()) {
        print_error(err, root.error());
        return kExitCodeError;
    }
    if (!environment.home.empty() && root.value().is_same_directory(environment.home)) {
        err << "Aviso: la carpeta del proyecto es tu carpeta personal; el modelo podrá leer "
               "todo lo que hay en ella.\n";
    }

    // Límites e instrucciones personales (junto a config.json).
    code::CodeLimits limits;
    if (environment.config_path.has_value()) {
        const code::LoadedCodeLimits loaded_limits = code::load_code_limits(*environment.config_path);
        limits = loaded_limits.limits;
        for (const std::string& warning : loaded_limits.warnings) {
            err << one_line(warning) << '\n';
        }
    }
    code::SystemPromptOptions prompt_options;
    prompt_options.now = environment.now;
    if (environment.config_path.has_value()) {
        prompt_options.personal_instructions =
            utf8_of(path_of(*environment.config_path).parent_path() / "AGENTS.md");
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
        .root = root.value(),
        .client = environment.make_client(config),
        .prompt_options = std::move(prompt_options),
        .secrets = config.api_key.empty() ? std::vector<std::string>{}
                                          : std::vector<std::string>{config.api_key},
        .limits = limits,
        .interactive = false,
    };
    code::CodeSession session{std::move(session_environment),
                              [&queue](code::CodeSession::Task task) { queue.post(std::move(task)); },
                              std::move(observer)};
    if (!session.send(*line.prompt)) {
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
        environment.out << md::sanitize(outcome->answer) << '\n' << std::flush;
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
        .portable_dir = process_portable_data_dir(),
        .home = std::string{process_env("HOME")},
        .make_client =
            [](const Config& config) {
                return std::make_unique<ChatClient>(config, std::make_unique<CurlTransport>());
            },
        .interrupted = InterruptGuard::requested,
        .now = [] { return std::time(nullptr); },
    };
    if (environment.home.empty()) {
        environment.home = std::string{process_env("USERPROFILE")}; // Windows.
    }
    return run_code(line, environment);
}

} // namespace chatbot::cli
