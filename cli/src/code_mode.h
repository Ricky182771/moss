#ifndef CHATBOT_CLI_CODE_MODE_H
#define CHATBOT_CLI_CODE_MODE_H

#include "commands.h"
#include "conversation.h"
#include "session.h"
#include "task_list.h"

#include "chatbot/code/code_limits.h"
#include "chatbot/code/code_session.h"
#include "chatbot/code/project_root.h"
#include "chatbot/code/system_prompt.h"
#include "chatbot/config.h"
#include "chatbot/error.h"
#include "chatbot/result.h"

#include <cstddef>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// La lógica del modo código sin FTXUI (guias/18-moss-code.md, secciones 1 y 9): al modo código
/// lo que Session es al chat. Solo existe con CHATBOT_BUILD_CODE.
namespace chatbot::cli {

/// El modo de permiso de la línea de estado (en C2 solo hay herramientas de lectura).
inline constexpr std::string_view kReadOnlyMode = "Solo lectura";
/// Aviso de arranque si AGENTS.md o CLAUDE.md se recortó (moss code -p y la pantalla).
inline constexpr std::string_view kInstructionsTruncated =
    "Aviso: AGENTS.md o CLAUDE.md es demasiado largo y se recortó.";

/// Las carpetas de Moss y los secretos del entorno: lo que necesita abrir un proyecto.
struct CodePaths {
    std::optional<std::string> config_path;      ///< default_config_path().
    std::optional<std::string> credentials_path; ///< default_credentials_path().
    std::optional<std::string> data_dir;         ///< default_data_dir().
    /// <datos>/moss por defecto (default_app_data_dir), aunque CHAT_DATA_DIR mande data_dir a
    /// otra parte: ninguna se puede abrir.
    std::optional<std::string> app_data_dir;
    std::optional<std::string> portable_dir; ///< process_portable_data_dir().
    std::string home; ///< La carpeta personal (HOME o USERPROFILE), para el aviso; vacía si no se sabe.
    /// CHAT_SEARCH_API_KEY y CHAT_TELEGRAM_TOKEN: se tapan en lo que va al modelo.
    std::vector<std::string> env_secrets;
};

/// Un proyecto abierto para el modo código.
struct CodeProject {
    code::ProjectRoot root;
    code::CodeLimits limits;                  ///< config.json → "code".
    code::SystemPromptOptions prompt_options; ///< Reloj e instrucciones personales.
    /// Avisos de arranque en español, en orden: carpeta personal y límites ignorados.
    std::vector<std::string> notices;
};

/// El arranque que comparten moss code -p y la pantalla: la carpeta (folder o, vacía, la actual)
/// con ProjectRoot::open y las carpetas de Moss bloqueadas, el aviso si es la carpeta personal,
/// los límites de config.json e instrucciones personales (junto a config.json). El error de
/// ProjectRoot::open (Config) si no se puede abrir.
[[nodiscard]] Result<CodeProject> open_code_project(const std::string& folder,
                                                    const CodePaths& paths,
                                                    std::function<std::time_t()> now);

/// Lo que no debe llegar al modelo: la key del modelo, las del entorno y todas las de
/// credentials.json (búsqueda, Telegram, otros proveedores). Sin vacíos ni repetidos.
[[nodiscard]] std::vector<std::string> code_secrets(const Config& config, const CodePaths& paths);

/// La carpeta de datos de la app por defecto, sin CHAT_DATA_DIR: el padre de las conversaciones.
[[nodiscard]] std::optional<std::string> default_app_data_dir();
/// HOME o, si falta, USERPROFILE (Windows); vacía si no hay ninguna.
[[nodiscard]] std::string personal_home();

/// "AGENTS.md cargado", "CLAUDE.md cargado" o "Sin AGENTS.md en esta carpeta".
[[nodiscard]] std::string instructions_notice(code::ProjectInstructions project);

/// El centro de la línea de estado: "Pensando…" (Requesting, Checking), "Leyendo…" (read),
/// "Buscando…" (glob, grep), "Anotando tareas…" (todo_write), "Ejecutando…" (otra herramienta),
/// "Esperando aprobación…" y vacío en Idle. tool: la herramienta en curso.
[[nodiscard]] std::string code_activity(code::LoopState state, std::string_view tool);

/// Lo que dice la línea de estado del modo código.
struct CodeStatus {
    std::string mode;     ///< kReadOnlyMode.
    std::string activity; ///< code_activity.
    std::string model;    ///< El de la configuración; vacío sin configurar.
};
[[nodiscard]] CodeStatus code_status(code::LoopState state, std::string_view tool,
                                     std::string_view model);

/// "Moss Code — <carpeta> — <modelo>", la carpeta con "~" (display_path con home, que en
/// Windows va vacía: ruta completa); sin modelo, sin la última parte.
[[nodiscard]] std::string code_title(const std::string& folder, const std::string& home,
                                     std::string_view model);

/// El aviso de /ayuda: las teclas del modo código y sus comandos de kCommands, uno por línea.
[[nodiscard]] std::string code_help_text();

/// Lo que CodeMode necesita del mundo, inyectable para las pruebas.
struct CodeModeEnvironment {
    /// El de la Session de apoyo (configuración, make_client, reloj y home de display_home).
    /// Sus config_path, credentials_path, data_dir y env (las CHAT_* secretas) también arman
    /// los CodePaths.
    SessionEnvironment session;
    std::optional<std::string> app_data_dir; ///< default_app_data_dir().
    std::optional<std::string> portable_dir; ///< process_portable_data_dir().
    std::string home;                        ///< personal_home(), para el aviso.
};

/// Los CodePaths de environment.
[[nodiscard]] CodePaths code_paths(const CodeModeEnvironment& environment);

/// CodeModeEnvironment con el sistema real (app_session_environment y lo demás).
[[nodiscard]] CodeModeEnvironment app_code_mode_environment();

/// El modo código. Se usa solo desde el hilo de la interfaz. Es dueño de una code::CodeSession
/// y traduce sus avisos a las entradas, las tareas y el estado que dibuja la pantalla. Tiene una
/// Session solo para la configuración (F2, /model): nunca envía ni guarda conversación por ella,
/// y la configuración vigente es siempre session().config(). No escribe nada en disco.
class CodeMode {
public:
    using Post = code::CodeSession::Post;
    /// Terminó un turno (ya con sus entradas). restored: si no llegó nada al modelo, el texto
    /// para regresar a la caja (como Session::Finished::restored); si no, nullopt. Corre dentro
    /// de un aviso de la CodeSession: desde aquí no se llama a clear().
    using OnFinished = std::function<void(std::optional<std::string> restored)>;

    /// initial: la de load_startup_config (puede faltar la key o el modelo: entonces la
    /// CodeSession queda sin cliente y configured() es false). Crea la CodeSession (corre git
    /// hasta 3 s, pendiente de C1) y las entradas de arranque.
    CodeMode(CodeModeEnvironment environment, Config initial, CodeProject project, Post post,
             OnFinished on_finished = {});
    ~CodeMode();

    CodeMode(const CodeMode&) = delete;
    CodeMode& operator=(const CodeMode&) = delete;
    CodeMode(CodeMode&&) = delete;
    CodeMode& operator=(CodeMode&&) = delete;

    // --- Configuración ---
    /// La Session de apoyo, para SettingsScreen y ModelPicker. Solo su configuración.
    [[nodiscard]] Session& session() { return session_; }
    [[nodiscard]] bool configured() const { return session_.configured(); }
    /// Lo que se muestra sin configuración (Session::setup_problem).
    [[nodiscard]] std::string setup_problem() const { return session_.setup_problem(); }
    /// Después de guardar en F2 o cambiar con /model: el cliente y los secretos se rehacen desde
    /// session().config() para el siguiente turno; el historial se conserva. Si cambió el
    /// proveedor o el modelo, una entrada Notice con el aviso de Session ("Ahora se usa …").
    /// false, sin cambiar nada, con un turno en curso (la pantalla no abre F2 ni /model
    /// entonces); false sin configuración completa.
    bool config_changed();

    // --- Turno ---
    /// Lo que hace Enter con el texto de la caja, comandos incluidos (parse_command con
    /// CommandMode::Code; los del chat son texto normal). Como Session::submit: Started,
    /// Ignored (turno en curso o caja vacía), Status (el texto se queda), Done (/clear, /ayuda o
    /// /model que cambió, ya con su entrada) u OpenModelPicker (/model sin modelo).
    [[nodiscard]] SubmitOutcome submit(const std::string& text);
    /// Para el menú de "/": configured, busy y model_locked.
    [[nodiscard]] CommandContext command_context() const;
    /// Empieza un turno. false (sin entrada nueva) sin configuración, con un turno en curso o
    /// con el texto vacío.
    [[nodiscard]] bool send(const std::string& text);
    /// Esc: cancela la petición o la herramienta en curso; el turno termina limpio.
    void cancel();
    /// true desde send() hasta el fin del turno: F2, /model y clear() esperan.
    [[nodiscard]] bool busy() const;
    [[nodiscard]] code::LoopState state() const;
    /// /clear: sesión nueva en la misma carpeta, con las instrucciones recalculadas y las
    /// entradas de arranque otra vez. false con un turno en curso. Nunca desde un aviso del
    /// observador de la CodeSession (la destruye): la pantalla lo llama desde la caja.
    bool clear();

    // --- Pantalla ---
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    /// La lista de tareas visible (vacía: escondida).
    [[nodiscard]] const std::vector<TaskItem>& tasks() const { return tasks_; }
    [[nodiscard]] CodeStatus status() const;
    [[nodiscard]] std::string title() const;
    /// Clic en la línea del pensamiento de la entrada index, y Ctrl+T (todas).
    void toggle_reasoning(std::size_t index) { chatbot::cli::toggle_reasoning(entries_, index); }
    void toggle_all_reasoning() { chatbot::cli::toggle_all_reasoning(entries_); }
    /// Clic en la línea ● de la entrada index: abre o cierra su salida. false si no es Tool.
    bool toggle_tool(std::size_t index);
    /// Ctrl+O: abre o cierra la salida de la última herramienta con salida. false si no hay.
    bool toggle_last_tool();

    // --- Para las pruebas y el diagnóstico ---
    [[nodiscard]] const std::vector<Message>& history() const;
    [[nodiscard]] const code::CodeSystemPrompt& instructions() const;

private:
    [[nodiscard]] std::unique_ptr<code::CodeSession> make_code_session();
    [[nodiscard]] code::CodeObserver observer();
    void add_startup_entries();
    void add_entry(EntryKind kind, std::string text);
    /// Cierra la respuesta del asistente en curso: lo que llegue después va en otra entrada.
    void close_answer();
    /// La entrada Tool de la llamada index de la respuesta en curso, creándola si hace falta.
    Entry& tool_entry(std::size_t index, const std::string& name);

    void on_delta(StreamDelta::Kind kind, const std::string& text);
    void on_state(code::LoopState state);
    void on_turn_end(const code::TurnOutcome& outcome);

    CodeModeEnvironment env_;
    CodeProject project_;
    Post post_;
    OnFinished on_finished_;
    Session session_;
    std::vector<Entry> entries_;
    std::vector<TaskItem> tasks_;
    code::LoopState state_ = code::LoopState::Idle;
    std::string current_tool_; ///< Nombre de la herramienta en curso (on_tool_ready).
    /// Índice de llamada → entrada, de la respuesta en curso.
    std::map<std::size_t, std::size_t> tool_entries_;
    std::optional<std::size_t> answer_;     ///< Entrada del asistente que sigue llegando.
    std::optional<std::size_t> user_entry_; ///< Entrada del mensaje del turno en curso.
    std::size_t turn_start_ = 0;            ///< Primera entrada del turno en curso.
    /// El proveedor y el modelo del cliente (Session::model) de la última config_changed o
    /// del arranque.
    std::string shown_provider_;
    std::string shown_model_;
    /// Al final: se destruye primero (sus avisos apuntan a lo de arriba).
    std::unique_ptr<code::CodeSession> code_;
};

/// open_code_mode: el modo o por qué no se pudo abrir.
struct OpenedCodeMode {
    std::unique_ptr<CodeMode> mode;
    std::optional<ChatError> error; ///< load_startup_config u open_code_project.
};

/// El modo código con el sistema real: load_startup_config, app_code_mode_environment y
/// open_code_project con folder (vacía: la actual).
[[nodiscard]] OpenedCodeMode open_code_mode(const std::string& folder, CodeMode::Post post,
                                            CodeMode::OnFinished on_finished);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_CODE_MODE_H
