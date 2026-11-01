#ifndef CHATBOT_CLI_SESSION_H
#define CHATBOT_CLI_SESSION_H

#include "block_actions.h"
#include "code_blocks.h"
#include "commands.h"
#include "conversation.h"
#include "conversation_store.h"
#include "model_params_form.h"
#include "provider_settings.h"
#include "request_runner.h"
#include "search_settings.h"

#include "chatbot/chat_client.h"
#include "chatbot/config.h"
#include "chatbot/error.h"
#include "chatbot/model_params.h"
#include "chatbot/result.h"
#include "chatbot/web_search.h"

#include <cstddef>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// Aviso mientras no hay una configuración completa (falta key o modelo).
inline constexpr std::string_view kSetupNotice = "Configura un proveedor para empezar.";
/// Abrir, crear o borrar una conversación, o guardar la configuración, con
/// una respuesta en curso.
inline constexpr std::string_view kWaitOrCancel = "Espera la respuesta o cancélala con Esc.";
/// Tope de "gui" en config.json, medido sobre el JSON compacto.
inline constexpr std::size_t kMaxGuiSettingsBytes = 64 * 1024;

/// Por qué json no puede ser "gui": no es JSON, no es un objeto o pasa de
/// kMaxGuiSettingsBytes en su forma compacta. nullopt si se puede guardar.
[[nodiscard]] std::optional<std::string> gui_settings_problem(std::string_view json);

/// Lo que Session necesita del mundo, inyectable para las pruebas. En la app
/// lo arma main.cpp con el entorno real; Session no lee el entorno por su cuenta.
struct SessionEnvironment {
    std::optional<std::string> config_path;      ///< default_config_path()
    std::optional<std::string> credentials_path; ///< default_credentials_path()
    std::string data_dir;                        ///< default_data_dir().value_or("")
    /// CHAT_*: para load_config al guardar, SettingsEnv y la key de búsqueda.
    chatbot::EnvLookup env;
    /// Crea el cliente con una configuración completa (CurlTransport en la app).
    std::function<std::unique_ptr<ChatClient>(const Config&)> make_client;
    /// Crea el proveedor de /buscar con una key (TavilySearch en la app).
    std::function<std::shared_ptr<SearchProvider>(const std::string& key)> make_search;
    std::function<std::time_t()> now;                          ///< Fecha de /buscar y /exportar.
    std::function<std::optional<std::string>()> download_base; ///< default_download_dir
    std::string home;                                          ///< display_home
};

/// Lo que pasó con Session::send.
enum class SendResult {
    Sent,             ///< Se envió al modelo; al terminar llega OnFinished.
    NotConfigured,    ///< Falta la key o el modelo (kSetupNotice).
    Busy,             ///< Hay una respuesta en curso.
    Empty,            ///< El texto (o la consulta de búsqueda) está vacío.
    MissingSearchKey, ///< Búsqueda sin key (kMissingSearchKey).
};

/// Lo que la interfaz hace con la caja después de Enter (Session::submit).
struct SubmitOutcome {
    enum class Type {
        Started, ///< Se envió al modelo.
        Ignored, ///< Enter no hace nada (respuesta en curso, caja vacía).
        Status,  ///< Aviso para la línea de estado; el texto se queda en la caja.
        Done,    ///< /guardar, /exportar o /model que cambió: el resultado ya está en el historial.
        Copy,    ///< /copiar: la interfaz copia block y llama report().
        /// /model sin modelo: la interfaz abre su selector, en el paso de
        /// modelo de picker_provider si lo hay; si no, en el de proveedor.
        OpenModelPicker,
    };
    Type type = Type::Ignored;
    std::string status; ///< Status: el aviso.
    bool clear = false; ///< Si la caja se vacía (Started, Copy, Done si salió bien).
    CodeBlock block;    ///< Copy: el bloque elegido.
    /// Lo que dio parse_command (Normal: texto para el modelo).
    ParsedCommand::Type command = ParsedCommand::Type::Normal;
    /// Si el texto fue a send (texto normal y /buscar): lo que dio, para
    /// distinguir "respuesta en curso" de "caja vacía". nullopt con
    /// /copiar, /guardar y /exportar.
    std::optional<SendResult> sent;
    ActionResult action; ///< Done: lo de save_block, export_conversation o el aviso de /model.
    std::optional<std::string> picker_provider; ///< OpenModelPicker: id del proveedor.
};

/// Un proveedor (de la tabla o un endpoint) para el selector de /model y el
/// RPC, en el orden de ProviderSettings::provider_list. Nunca lleva la key:
/// solo si hay una guardada.
struct ProviderChoice {
    std::string id{}; ///< "nvidia" o "custom:<id>". ({}: las pruebas lo omiten sin aviso.)
    std::string name{};
    std::string base_url{};
    bool needs_key = true;
    bool endpoint = false;  ///< Un endpoint de "endpoints".
    bool current = false;   ///< Es el de config.json.
    bool key_saved = false; ///< Hay key en credentials.json (credentials_key).
    bool local = false;     ///< Servidor local: no necesita key.
    bool selectable = false;
    std::string note{}; ///< "" o kNoKeyNote (si no es selectable).
};
inline constexpr std::string_view kNoKeyNote = "sin key";

/// El aviso de save_provider y switch_model.
[[nodiscard]] std::string provider_switched_notice(std::string_view model, std::string_view label);

/// La pista que agrega Session a un 400/422 si la petición llevó ajustes.
[[nodiscard]] std::string model_params_hint(std::string_view model);

/// Lo que pasó con Session::delete_endpoint.
struct EndpointDeletion {
    enum class Status {
        Deleted, ///< message: "Se borró el endpoint <nombre>."
        Invalid, ///< Es el que está en uso o ya no existe.
        Busy,    ///< Hay una respuesta en curso (kWaitOrCancel).
        Failed,  ///< No se pudo leer o escribir la configuración.
    };
    Status status = Status::Invalid;
    std::string message;
};

/// Lo que pasó con Session::switch_model.
struct ModelSwitch {
    enum class Status {
        Switched,  ///< Cambió: message es el aviso que quedó en el historial.
        Unchanged, ///< Ya era el actual: no se escribió nada.
        Invalid,   ///< Proveedor desconocido, sin key, CHAT_* o modelo inválido.
        Busy,      ///< Hay una respuesta en curso (kWaitOrCancel).
        Failed,    ///< No se pudo leer o escribir la configuración.
    };
    Status status = Status::Invalid;
    std::string message;
};

/// Guardar un bloque o exportar sin pasar por el historial, como los
/// botones [Guardar] y [Exportar] (Session::save_code_block,
/// Session::export_to_downloads).
struct FileOutcome {
    enum class Status {
        Attempted, ///< Se intentó escribir: result dice si salió bien (y su path).
        Invalid,   ///< Número de bloque que no existe, nombre inválido o sin pares.
        NoFolder,  ///< No hay carpeta de descargas (no_download_dir_message).
        Unreadable, ///< export_stored: no se pudo cargar (ilegible o no existe).
    };
    Status status = Status::Attempted;
    ActionResult result; ///< El aviso (Wording::Long) o el motivo; error si no se escribió.
};

/// Lo que pasó con Session::rename_conversation.
struct RenameOutcome {
    enum class Status {
        Renamed, ///< message: el título ya normalizado.
        Invalid, ///< Título inválido o conversación sin guardar.
        Failed,  ///< Ilegible, no existe o no se pudo escribir.
    };
    Status status = Status::Invalid;
    std::string message;
};
inline constexpr std::string_view kNothingToRename = "Todavía no hay nada guardado que renombrar.";

/// La lógica de la app sin FTXUI: configuración vigente, cliente y runner,
/// búsqueda web, la conversación abierta (una sola) y su almacén, los
/// comandos de la caja y los guardados de la configuración. La interfaz solo
/// decide dónde mostrar los textos que devuelve.
///
/// Como Conversation, se usa solo desde el hilo de la interfaz; los hilos de
/// trabajo solo le hablan por post. El destructor cancela la petición en
/// curso y espera al hilo (join); lo que quede en la cola de post apunta a
/// la sesión y no debe ejecutarse después (en la app, FTXUI no ejecuta
/// tareas después de Loop(): App::~App y TaskRunner::~TaskRunner las descartan).
class Session {
public:
    using Post = RequestRunner::Post;
    /// Cómo terminó una respuesta.
    struct Finished {
        /// El texto para regresar a la caja (error, cancelación, sin
        /// resultados, respuesta sin texto), o nullopt si el par terminó bien.
        std::optional<std::string> restored;
        /// nullopt si terminó bien; una respuesta sin texto es BadResponse
        /// con kEmptyAnswer (o kOutOfTokensThinking: Conversation::empty_answer_error),
        /// como la entrada de error.
        std::optional<ChatError> error;
        std::string finish_reason; ///< Solo si terminó bien ("" si no llegó).
        std::size_t dropped = 0;   ///< Mensajes omitidos por el recorte.
        bool saved = false;        ///< El par se guardó (la lista cambió).
    };
    /// Terminó una respuesta (vía post), ya con el par guardado si salió bien.
    using OnFinished = std::function<void(Finished finished)>;

    /// Carga las instrucciones del sistema y la key de búsqueda y abre el
    /// almacén. initial: la de load_config con allow_missing_key_and_model;
    /// si validate_config la acepta, crea el cliente y el runner.
    Session(SessionEnvironment environment, Config initial, Post post, OnFinished on_finished);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    ~Session() = default;

    // --- Estado ---
    [[nodiscard]] bool configured() const { return runner_ != nullptr; }
    [[nodiscard]] bool busy() const { return runner_ != nullptr && runner_->busy(); }
    /// /buscar en curso, antes de que llegue la búsqueda.
    [[nodiscard]] bool searching() const { return searching_; }
    /// Modelo del cliente; vacío sin configuración.
    [[nodiscard]] const std::string& model() const { return model_; }
    /// La hora del entorno (para los grupos de la lista de conversaciones).
    [[nodiscard]] std::time_t now() const { return env_.now(); }
    [[nodiscard]] const Config& config() const { return config_; }
    /// Las vigentes (vacías: sin mensaje de sistema).
    [[nodiscard]] const std::string& system_prompt() const { return system_prompt_; }
    /// "system_prompt" está en config.json (si no, son las predeterminadas).
    [[nodiscard]] bool system_prompt_stored() const { return system_prompt_stored_; }
    [[nodiscard]] std::size_t last_dropped() const { return last_dropped_; }
    [[nodiscard]] const Conversation& conversation() const { return conversation_; }
    /// Clic en la línea del pensamiento de la entrada index, y Ctrl+T (todas).
    /// Solo cambia cómo se ve: también con una respuesta en curso.
    void toggle_reasoning(std::size_t index) { conversation_.toggle_reasoning(index); }
    void toggle_all_reasoning() { conversation_.toggle_all_reasoning(); }
    [[nodiscard]] const ConversationStore& store() const { return store_; }
    /// Avisos de arranque (instrucciones del sistema y búsqueda), separados
    /// por un espacio; vacío si no hay.
    [[nodiscard]] const std::string& startup_warning() const { return startup_warning_; }
    /// Lo que se muestra mientras no hay configuración: el error del
    /// proveedor de config.json (Config::provider_error) o kSetupNotice.
    [[nodiscard]] std::string setup_problem() const;
    /// Los mismos avisos, uno por elemento.
    [[nodiscard]] const std::vector<std::string>& startup_warnings() const {
        return startup_warnings_;
    }
    /// Hay key de búsqueda (CHAT_SEARCH_API_KEY o credentials.json).
    [[nodiscard]] bool has_search_key() const { return search_provider_ != nullptr; }
    /// Para el menú de comandos.
    [[nodiscard]] CommandContext command_context();
    /// Los bloques de código de la conversación, actualizados.
    [[nodiscard]] const CodeBlockIndex& code_blocks();

    // --- Caja de entrada ---
    /// Lo que hace Enter con este texto (comandos incluidos).
    [[nodiscard]] SubmitOutcome submit(std::string_view text);
    /// Envía text al modelo sin interpretar comandos. Con search, hace lo de
    /// /buscar con esa consulta y text es lo que se muestra y se guarda.
    [[nodiscard]] SendResult send(std::string_view text, std::optional<std::string> search = {});
    /// Cancela la petición en curso, si la hay.
    void cancel();
    /// Resultado de copiar (SubmitOutcome::Copy) al historial: aviso o error.
    void report(const ActionResult& result);

    // --- Botones: no tocan el historial y funcionan con una respuesta en curso ---
    /// save_block del bloque number (choose_code_block: sin número, el
    /// último) con file_name (validate_file_name; vacío: bloque-<n>.<ext>).
    [[nodiscard]] FileOutcome save_code_block(std::optional<int> number,
                                              std::string_view file_name);
    /// export_conversation de la conversación abierta.
    [[nodiscard]] FileOutcome export_to_downloads();
    /// export_conversation de la guardada con ese id, abierta o no: lo que
    /// hay en disco (los pares de una respuesta en curso no están ahí todavía).
    [[nodiscard]] FileOutcome export_stored(const std::string& id);

    // --- Conversaciones: un error para la línea de estado, o nullopt ---
    [[nodiscard]] std::optional<std::string> new_conversation();
    [[nodiscard]] std::optional<std::string> open(const std::string& id);
    /// Si era la abierta, empieza una nueva.
    [[nodiscard]] std::optional<std::string> remove(const std::string& id);
    /// ConversationStore::rename; si es la abierta, también su título en
    /// memoria, para que el siguiente guardado no regrese al viejo. Se
    /// permite con una respuesta en curso. id vacío: kNothingToRename.
    [[nodiscard]] RenameOutcome rename_conversation(const std::string& id,
                                                    std::string_view title);

    // --- Configuración (F2) ---
    /// Formularios con lo guardado y las CHAT_*; un error si no se puede leer.
    [[nodiscard]] Result<ProviderSettings> provider_form() const;
    [[nodiscard]] Result<SearchSettings> search_form() const;
    /// Guardan y aplican sin reiniciar; el aviso va al historial. Devuelven
    /// el error para el formulario (kWaitOrCancel con una respuesta en
    /// curso, sin escribir nada), o nullopt.
    [[nodiscard]] std::optional<std::string> save_provider(const ProviderSettings& form);
    /// Quita el endpoint id ("custom:<id>" o el id solo) de config.json y su
    /// key de credentials.json. No el que está en uso.
    [[nodiscard]] EndpointDeletion delete_endpoint(std::string_view id);

    // --- /model (y settings.provider.switch del RPC) ---
    /// CHAT_MODEL está definida: /model no puede cambiar nada.
    [[nodiscard]] bool model_locked() const;
    /// Cada proveedor de ProviderSettings::provider_list (sin la fila de
    /// agregar), en orden, con su estado.
    [[nodiscard]] Result<std::vector<ProviderChoice>> provider_choices() const;
    /// Por qué no se puede pasar al proveedor id (los textos de /model), o
    /// nullopt si se puede.
    [[nodiscard]] std::optional<std::string> provider_problem(std::string_view id) const;
    /// La configuración para pedir los modelos del proveedor id con su key
    /// guardada, como F2 (request_config). Un error si provider_problem.
    [[nodiscard]] Result<Config> models_config(std::string_view id) const;
    /// Cambia al proveedor (nullopt: el actual) y al modelo con save_provider:
    /// la misma validación, escritura y aviso que F2. Nunca toca
    /// credentials.json ni la conversación abierta.
    [[nodiscard]] ModelSwitch switch_model(const std::optional<std::string>& provider,
                                           const std::string& model);
    // --- Ajustes del modelo efectivo (provider y model de config(), con CHAT_MODEL) ---
    /// Los guardados en "models" (Config::model_params).
    [[nodiscard]] const ModelParams& model_params() const { return config_.model_params; }
    /// kNoModelForParams si no hay proveedor o modelo; nullopt si se puede ajustar.
    [[nodiscard]] std::optional<std::string> model_params_problem() const;
    /// validate_model_params y la vista previa con el historial abierto más
    /// el siguiente mensaje. Sin modelo, el problema va en "model".
    [[nodiscard]] ModelParamsCheck check_model_params(const ModelParams& draft) const;
    /// Como los demás save_*: kWaitOrCancel con una respuesta en curso; si
    /// no, valida, escribe "models" y aplica desde el siguiente mensaje (el
    /// aviso va al historial). Sin ajustes, quita el modelo de "models".
    [[nodiscard]] std::optional<std::string> save_model_params(const ModelParams& draft);

    [[nodiscard]] std::optional<std::string> save_system_prompt(
        const std::optional<std::string>& stored);
    /// validate_system_prompt y luego save_system_prompt con
    /// system_prompt_to_store (el predeterminado borra la llave).
    [[nodiscard]] std::optional<std::string> save_system_prompt_text(const std::string& text);
    [[nodiscard]] std::optional<std::string> save_search_key(const std::string& key);

    // --- Preferencias de la GUI ("gui" en config.json, sin interpretarlas) ---
    /// El texto JSON de "gui", o nullopt si falta.
    [[nodiscard]] Result<std::optional<std::string>> gui_settings() const;
    /// Escribe json (gui_settings_problem) o, con nullopt, borra la llave.
    /// Funciona con una respuesta en curso. El error, o nullopt.
    [[nodiscard]] std::optional<std::string> save_gui_settings(
        const std::optional<std::string>& json);

private:
    /// (Re)construye el cliente y el runner con config_ (completa). El
    /// runner viejo se destruye (join) antes que el cliente que usa.
    void connect();
    /// Proveedor de /buscar con la key vigente (CHAT_SEARCH_API_KEY o
    /// credentials.json), o nullptr si no hay; si credentials.json no se
    /// puede leer, nullptr y el motivo en warning.
    [[nodiscard]] std::shared_ptr<SearchProvider> load_search(std::string& warning) const;
    /// /copiar, /guardar y /exportar.
    [[nodiscard]] SubmitOutcome run_local_command(const ParsedCommand& command);
    [[nodiscard]] SubmitOutcome run_model_command(const ParsedCommand& command);
    /// provider_form() con el proveedor id ya elegido (vacío: el guardado) y,
    /// en problem, por qué no se puede usar para /model.
    [[nodiscard]] Result<ProviderSettings> form_for(std::string_view id,
                                                    std::optional<std::string>& problem) const;

    SessionEnvironment env_;
    Config config_; ///< La vigente; cambia al guardar la configuración.
    Post post_;
    OnFinished on_finished_;
    std::string system_prompt_;
    bool system_prompt_stored_ = false;
    std::string startup_warning_;
    std::vector<std::string> startup_warnings_;
    /// Se comparte con el hilo de trabajo: se puede reemplazar al guardar la
    /// key aunque haya un hilo terminado sin unir.
    std::shared_ptr<SearchProvider> search_provider_;
    bool searching_ = false;
    ConversationStore store_;
    Conversation conversation_;
    CodeBlockIndex code_blocks_;
    std::size_t last_dropped_ = 0;
    std::string model_;
    // El runner va después del cliente: se destruye antes (join).
    std::unique_ptr<ChatClient> client_;
    std::unique_ptr<RequestRunner> runner_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_SESSION_H
