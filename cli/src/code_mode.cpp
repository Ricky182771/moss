#include "code_mode.h"

#include "app_startup.h"
#include "conversation_store.h"
#include "downloads.h"

#include "chatbot/credentials.h"
#include "chatbot/markdown/markdown.h"
#include "chatbot/platform.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <utility>

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

std::vector<TaskItem> task_items(const std::vector<code::TodoItem>& todos) {
    std::vector<TaskItem> tasks;
    tasks.reserve(todos.size());
    for (const code::TodoItem& todo : todos) {
        TaskState state = TaskState::Pending;
        if (todo.status == code::TodoStatus::InProgress) {
            state = TaskState::Active;
        } else if (todo.status == code::TodoStatus::Completed) {
            state = TaskState::Done;
        }
        tasks.push_back(TaskItem{state, todo.content, todo.active_form});
    }
    return tasks;
}

} // namespace

Result<CodeProject> open_code_project(const std::string& folder, const CodePaths& paths,
                                      std::function<std::time_t()> now) {
    // La raíz: la indicada o la actual; nunca la carpeta de Moss (configuración ni datos).
    std::error_code ignored;
    const std::string root_text = folder.empty() ? utf8_of(fs::current_path(ignored)) : folder;
    std::vector<std::string> blocked;
    for (const std::optional<std::string>& file : {paths.config_path, paths.credentials_path}) {
        if (file.has_value()) {
            blocked.push_back(utf8_of(path_of(*file).parent_path()));
        }
    }
    for (const std::optional<std::string>& dir :
         {paths.data_dir, paths.app_data_dir, paths.portable_dir}) {
        if (dir.has_value()) {
            blocked.push_back(*dir);
        }
    }
    const Result<code::ProjectRoot> root = code::ProjectRoot::open(root_text, blocked);
    if (root.is_error()) {
        return root.error();
    }
    std::vector<std::string> notices;
    if (!paths.home.empty() && root.value().is_same_directory(paths.home)) {
        notices.emplace_back("Aviso: la carpeta del proyecto es tu carpeta personal; el modelo "
                             "podrá leer todo lo que hay en ella.");
    }

    // Límites e instrucciones personales (junto a config.json).
    code::CodeLimits limits;
    code::SystemPromptOptions prompt_options;
    prompt_options.now = std::move(now);
    if (paths.config_path.has_value()) {
        code::LoadedCodeLimits loaded = code::load_code_limits(*paths.config_path);
        limits = loaded.limits;
        std::move(loaded.warnings.begin(), loaded.warnings.end(), std::back_inserter(notices));
        prompt_options.personal_instructions =
            utf8_of(path_of(*paths.config_path).parent_path() / "AGENTS.md");
    }
    return CodeProject{root.value(), limits, std::move(prompt_options), std::move(notices)};
}

std::vector<std::string> code_secrets(const Config& config, const CodePaths& paths) {
    std::vector<std::string> secrets;
    const auto add = [&secrets](const std::string& secret) {
        if (!secret.empty() && std::find(secrets.begin(), secrets.end(), secret) == secrets.end()) {
            secrets.push_back(secret);
        }
    };
    add(config.api_key);
    for (const std::string& secret : paths.env_secrets) {
        add(secret);
    }
    if (paths.credentials_path.has_value()) {
        // Ilegible o con permisos abiertos: sin esas keys (la app tampoco las usa).
        const Result<Credentials> stored = load_credentials(*paths.credentials_path);
        if (!stored.is_error()) {
            for (const auto& [name, key] : stored.value().keys) {
                add(key);
            }
        }
    }
    return secrets;
}

std::optional<std::string> default_app_data_dir() {
    const auto env = [](const char* name) -> std::optional<std::string> {
        const std::string_view value = process_env(name);
        return value.empty() ? std::nullopt : std::optional<std::string>{std::string{value}};
    };
    const std::optional<std::string> conversations =
        current_os() == Os::Windows
            ? resolve_windows_data_dir(std::nullopt, process_portable_data_dir(),
                                       known_folder(KnownFolder::LocalAppData),
                                       env("LOCALAPPDATA"))
            : resolve_data_dir(std::nullopt, process_portable_data_dir(), env("XDG_DATA_HOME"),
                               env("HOME"));
    if (!conversations.has_value()) {
        return std::nullopt;
    }
    return utf8_of(path_of(*conversations).parent_path());
}

std::string personal_home() {
    const std::string_view home = process_env("HOME");
    return std::string{home.empty() ? process_env("USERPROFILE") : home};
}

std::string instructions_notice(code::ProjectInstructions project) {
    switch (project) {
    case code::ProjectInstructions::AgentsMd:
        return "AGENTS.md cargado";
    case code::ProjectInstructions::ClaudeMd:
        return "CLAUDE.md cargado";
    case code::ProjectInstructions::None:
        break;
    }
    return "Sin AGENTS.md en esta carpeta";
}

std::string code_activity(code::LoopState state, std::string_view tool) {
    switch (state) {
    case code::LoopState::Idle:
        return {};
    case code::LoopState::Requesting:
    case code::LoopState::Checking:
        return "Pensando…";
    case code::LoopState::AwaitingApproval:
        return "Esperando aprobación…";
    case code::LoopState::Running:
        break;
    }
    if (tool == "read") {
        return "Leyendo…";
    }
    if (tool == "glob" || tool == "grep") {
        return "Buscando…";
    }
    if (tool == "todo_write") {
        return "Anotando tareas…";
    }
    return "Ejecutando…";
}

CodeStatus code_status(code::LoopState state, std::string_view tool, std::string_view model) {
    return CodeStatus{std::string{kReadOnlyMode}, code_activity(state, tool), std::string{model}};
}

std::string code_title(const std::string& folder, const std::string& home,
                       std::string_view model) {
    // display_path deja la carpeta personal completa; aquí es "~".
    const std::string shown = !home.empty() && folder == home ? "~" : display_path(folder, home);
    std::string title = "Moss Code — " + shown;
    if (!model.empty()) {
        title += " — " + std::string{model};
    }
    return title;
}

CodePaths code_paths(const CodeModeEnvironment& environment) {
    const SessionEnvironment& session = environment.session;
    CodePaths paths{
        .config_path = session.config_path,
        .credentials_path = session.credentials_path,
        .data_dir = session.data_dir.empty() ? std::nullopt
                                             : std::optional<std::string>{session.data_dir},
        .app_data_dir = environment.app_data_dir,
        .portable_dir = environment.portable_dir,
        .home = environment.home,
        .env_secrets = {},
    };
    if (session.env) {
        for (const char* name : {"CHAT_SEARCH_API_KEY", "CHAT_TELEGRAM_TOKEN"}) {
            paths.env_secrets.emplace_back(session.env(name));
        }
    }
    return paths;
}

CodeModeEnvironment app_code_mode_environment() {
    return CodeModeEnvironment{
        .session = app_session_environment(),
        .app_data_dir = default_app_data_dir(),
        .portable_dir = process_portable_data_dir(),
        .home = personal_home(),
    };
}

// --- CodeMode ---

CodeMode::CodeMode(CodeModeEnvironment environment, Config initial, CodeProject project,
                   Post post, OnFinished on_finished)
    : env_(std::move(environment)), project_(std::move(project)), post_(std::move(post)),
      on_finished_(std::move(on_finished)),
      // Nunca envía: su OnFinished no llega.
      session_(env_.session, std::move(initial), post_, [](Session::Finished) {}) {
    code_ = make_code_session();
    add_startup_entries();
}

CodeMode::~CodeMode() = default;

std::unique_ptr<code::CodeSession> CodeMode::make_code_session() {
    const Config& config = session_.config();
    std::unique_ptr<ChatClient> client;
    if (session_.configured()) {
        client = env_.session.make_client(config);
    }
    // interactive = false: en C2 la pantalla no dibuja aprobaciones; lo que hubiera que
    // preguntar se rechaza sin esperar, como en -p, en vez de dejar el turno colgado.
    return std::make_unique<code::CodeSession>(
        code::CodeSessionEnvironment{
            .root = project_.root,
            .client = std::move(client),
            .prompt_options = project_.prompt_options,
            .secrets = code_secrets(config, code_paths(env_)),
            .limits = project_.limits,
            .interactive = false,
        },
        post_, observer());
}

void CodeMode::add_startup_entries() {
    const code::CodeSystemPrompt& loaded = code_->instructions();
    add_entry(EntryKind::Notice, instructions_notice(loaded.project));
    for (const std::string& warning : session_.config().warnings) {
        add_entry(EntryKind::Notice, md::sanitize(warning)); // Texto de config.json.
    }
    for (const std::string& notice : project_.notices) {
        add_entry(EntryKind::Notice, md::sanitize(notice));
    }
    if (loaded.instructions_truncated) {
        add_entry(EntryKind::Notice, std::string{kInstructionsTruncated});
    }
}

void CodeMode::add_entry(EntryKind kind, std::string text) {
    Entry entry;
    entry.kind = kind;
    entry.text = std::move(text);
    entries_.push_back(std::move(entry));
}

code::CodeObserver CodeMode::observer() {
    code::CodeObserver callbacks;
    callbacks.on_delta = [this](StreamDelta::Kind kind, const std::string& text) {
        on_delta(kind, text);
    };
    callbacks.on_tool_call_started = [this](std::size_t index, const std::string& name) {
        tool_entry(index, name);
    };
    callbacks.on_tool_ready = [this](const code::ToolCallInfo& call) {
        current_tool_ = call.name;
        Entry& entry = tool_entry(call.index, call.name);
        if (!call.summary.empty()) {
            entry.tool.summary = call.summary;
        }
    };
    callbacks.on_tool_result = [this](const code::ToolCallInfo& call,
                                      const code::ToolResult& result) {
        ToolLine& line = tool_entry(call.index, call.name).tool;
        line.state = result.is_error() ? ToolState::Failed : ToolState::Done;
        line.result = result.display;
        line.output = result.text;
    };
    callbacks.on_todos = [this](const std::vector<code::TodoItem>& todos) {
        tasks_ = task_items(todos);
    };
    callbacks.on_state = [this](code::LoopState state) { on_state(state); };
    callbacks.on_notice = [this](const std::string& notice) {
        close_answer();
        add_entry(EntryKind::Notice, notice);
    };
    callbacks.on_turn_end = [this](const code::TurnOutcome& outcome) { on_turn_end(outcome); };
    return callbacks;
}

void CodeMode::close_answer() {
    if (answer_.has_value()) {
        entries_[*answer_].in_progress = false;
        answer_.reset();
    }
}

Entry& CodeMode::tool_entry(std::size_t index, const std::string& name) {
    const auto found = tool_entries_.find(index);
    if (found != tool_entries_.end()) {
        return entries_[found->second];
    }
    close_answer();
    Entry entry;
    entry.kind = EntryKind::Tool;
    entry.tool.summary = name; // Hasta que llegue el resumen.
    entries_.push_back(std::move(entry));
    tool_entries_[index] = entries_.size() - 1;
    return entries_.back();
}

void CodeMode::on_delta(StreamDelta::Kind kind, const std::string& text) {
    if (!answer_.has_value()) {
        Entry entry;
        entry.kind = EntryKind::Assistant;
        entry.in_progress = true;
        entries_.push_back(std::move(entry));
        answer_ = entries_.size() - 1;
    }
    Entry& answer = entries_[*answer_];
    (kind == StreamDelta::Kind::Reasoning ? answer.reasoning : answer.text) += text;
}

void CodeMode::on_state(code::LoopState state) {
    state_ = state;
    if (state == code::LoopState::Requesting) {
        // Un paso nuevo: su texto y sus llamadas van en entradas nuevas.
        close_answer();
        tool_entries_.clear();
        current_tool_.clear();
    }
}

void CodeMode::on_turn_end(const code::TurnOutcome& outcome) {
    const bool cancelled = outcome.reason == code::TurnEnd::Cancelled;
    // Llamadas que se quedaron a medias (la respuesta se descartó): nunca siguen "en curso".
    for (std::size_t i = turn_start_; i < entries_.size(); ++i) {
        ToolLine& line = entries_[i].tool;
        if (entries_[i].kind == EntryKind::Tool && line.state == ToolState::Running) {
            line.state = ToolState::Failed;
            line.result = cancelled ? "Cancelado" : "Sin terminar";
        }
    }
    if (answer_.has_value()) {
        Entry& answer = entries_[*answer_];
        answer.cancelled = cancelled;
        answer.incomplete = outcome.reason == code::TurnEnd::Error;
    } else if (cancelled) {
        add_entry(EntryKind::Notice, "Respuesta cancelada.");
    }
    close_answer();
    if (outcome.reason == code::TurnEnd::Error) {
        ChatError error;
        error.kind = ErrorKind::BadResponse;
        error.message = std::string{kEmptyAnswer};
        if (outcome.error.has_value()) {
            error = *outcome.error;
        }
        add_entry(EntryKind::Error,
                  "[" + std::string{error_kind_label(error.kind)} + "] " + error.message);
    }
    tool_entries_.clear();
    current_tool_.clear();
    state_ = code::LoopState::Idle;
    std::optional<std::string> restored;
    if (!outcome.unsent_input.empty()) {
        // No llegó nada al modelo: el mensaje sale de la pantalla y vuelve a la caja.
        if (user_entry_.has_value() && *user_entry_ < entries_.size()) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*user_entry_));
        }
        restored = outcome.unsent_input;
    }
    user_entry_.reset();
    if (on_finished_) {
        on_finished_(std::move(restored));
    }
}

bool CodeMode::config_changed() {
    if (busy() || !session_.configured()) {
        return false;
    }
    const Config& config = session_.config();
    return code_->set_client(env_.session.make_client(config),
                             code_secrets(config, code_paths(env_)));
}

bool CodeMode::send(const std::string& text) {
    if (!configured() || !code_->send(text)) {
        return false;
    }
    tasks_ = tasks_for_new_turn(std::move(tasks_));
    turn_start_ = entries_.size();
    add_entry(EntryKind::User, text);
    user_entry_ = entries_.size() - 1;
    return true;
}

void CodeMode::cancel() {
    code_->cancel();
}

bool CodeMode::busy() const {
    return code_->busy();
}

code::LoopState CodeMode::state() const {
    return code_->state();
}

bool CodeMode::clear() {
    if (busy()) {
        return false;
    }
    code_.reset(); // Primero: su hilo de trabajo y sus avisos.
    entries_.clear();
    tasks_.clear();
    state_ = code::LoopState::Idle;
    current_tool_.clear();
    tool_entries_.clear();
    answer_.reset();
    user_entry_.reset();
    turn_start_ = 0;
    code_ = make_code_session();
    add_startup_entries();
    return true;
}

CodeStatus CodeMode::status() const {
    return code_status(state_, current_tool_, session_.config().model);
}

std::string CodeMode::title() const {
    return code_title(utf8_of(project_.root.path()), env_.session.home, session_.config().model);
}

void CodeMode::toggle_reasoning(std::size_t index) {
    if (index >= entries_.size()) {
        return;
    }
    Entry& entry = entries_[index];
    const ReasoningView view = reasoning_view(entry);
    if (view != ReasoningView::None) {
        entry.reasoning_fold =
            view == ReasoningView::Open ? ReasoningFold::Closed : ReasoningFold::Open;
    }
}

void CodeMode::toggle_all_reasoning() {
    const bool all_open = std::all_of(entries_.begin(), entries_.end(), [](const Entry& entry) {
        const ReasoningView view = reasoning_view(entry);
        return view == ReasoningView::None || view == ReasoningView::Open;
    });
    for (Entry& entry : entries_) {
        if (reasoning_view(entry) != ReasoningView::None) {
            entry.reasoning_fold = all_open ? ReasoningFold::Closed : ReasoningFold::Open;
        }
    }
}

bool CodeMode::toggle_tool(std::size_t index) {
    if (index >= entries_.size() || entries_[index].kind != EntryKind::Tool) {
        return false;
    }
    entries_[index].tool.expanded = !entries_[index].tool.expanded;
    return true;
}

bool CodeMode::toggle_last_tool() {
    for (auto entry = entries_.rbegin(); entry != entries_.rend(); ++entry) {
        if (entry->kind == EntryKind::Tool && !entry->tool.output.empty()) {
            entry->tool.expanded = !entry->tool.expanded;
            return true;
        }
    }
    return false;
}

const std::vector<Message>& CodeMode::history() const {
    return code_->history();
}

const code::CodeSystemPrompt& CodeMode::instructions() const {
    return code_->instructions();
}

OpenedCodeMode open_code_mode(const std::string& folder, CodeMode::Post post,
                              CodeMode::OnFinished on_finished) {
    // Primero la configuración: prepara el modo portable, del que dependen las rutas.
    const Result<Config> config = load_startup_config();
    if (config.is_error()) {
        return {nullptr, config.error()};
    }
    CodeModeEnvironment environment = app_code_mode_environment();
    const Result<CodeProject> project =
        open_code_project(folder, code_paths(environment), environment.session.now);
    if (project.is_error()) {
        return {nullptr, project.error()};
    }
    return {std::make_unique<CodeMode>(std::move(environment), config.value(), project.value(),
                                       std::move(post), std::move(on_finished)),
            std::nullopt};
}

} // namespace chatbot::cli
