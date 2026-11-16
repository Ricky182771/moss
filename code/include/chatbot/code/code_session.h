#ifndef CHATBOT_CODE_CODE_SESSION_H
#define CHATBOT_CODE_CODE_SESSION_H

#include "chatbot/chat_client.h"
#include "chatbot/code/code_limits.h"
#include "chatbot/code/permissions.h"
#include "chatbot/code/project_root.h"
#include "chatbot/code/system_prompt.h"
#include "chatbot/code/tool.h"
#include "chatbot/error.h"
#include "chatbot/types.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chatbot::code {

/// Estado del ciclo del agente (guias/18-moss-code.md, sección 2).
enum class LoopState {
    Idle,             ///< Sin turno: send() acepta un mensaje.
    Requesting,       ///< Petición al modelo, en el hilo de trabajo.
    Checking,         ///< Decidiendo el permiso de una llamada (no espera nada).
    Running,          ///< Herramienta en el hilo de trabajo.
    AwaitingApproval, ///< Esperando respond_approval; pending_approval() dice qué llamada.
};

/// Por qué terminó un turno.
enum class TurnEnd {
    Answered,      ///< Respuesta final sin llamadas (TurnOutcome::answer).
    Error,         ///< Error del modelo, o respuesta sin texto ni llamadas (TurnOutcome::error).
    Cancelled,     ///< cancel().
    StepLimit,     ///< CodeLimits::max_steps peticiones en el turno.
    RepeatedCalls, ///< La misma llamada CodeLimits::max_repeated_calls veces seguidas.
    InvalidCalls,  ///< CodeLimits::max_invalid_calls llamadas inválidas seguidas.
};

/// La respuesta a una llamada que espera aprobación.
enum class ApprovalDecision { Allow, AllowAlways, Reject };

/// Una llamada del modelo, para la pantalla.
struct ToolCallInfo {
    std::size_t index = 0; ///< Su lugar en la respuesta (el de ToolCallStarted).
    std::string id;
    std::string name;    ///< El que quedó en el historial (un nombre inválido, cambiado).
    std::string label;   ///< "Leer"; vacío si la herramienta no existe.
    std::string summary; ///< ToolRegistry::summarize. Texto del modelo: md::sanitize al dibujar.
};

/// Cómo terminó un turno.
struct TurnOutcome {
    TurnEnd reason = TurnEnd::Answered;
    std::string answer;             ///< Answered: la respuesta final.
    std::optional<ChatError> error; ///< Error: el del modelo.
    /// Si la primera petición del turno se canceló o falló (sin ningún asistente guardado), el
    /// mensaje del usuario sale del historial, como en Session: nunca quedan dos User seguidos
    /// (plantillas estrictas responden 400). Aquí regresa, para la caja; vacío en los demás casos.
    std::string unsent_input{};
};

/// Avisos a la interfaz. Siempre corren en el hilo de la interfaz, por post y en el orden en que
/// pasaron, nunca dentro de una llamada a CodeSession: desde ellos se puede llamar a send, cancel
/// o respond_approval. Cualquiera puede ir vacío. Lo que traen del modelo (texto, nombres,
/// resúmenes, display) se pasa por md::sanitize al dibujar.
struct CodeObserver {
    /// Texto (Content) o pensamiento (Reasoning) conforme llega.
    std::function<void(StreamDelta::Kind kind, const std::string& text)> on_delta;
    /// Empezó a llegar una llamada (ToolCallStarted): su índice y el nombre que mandó el modelo.
    std::function<void(std::size_t index, const std::string& name)> on_tool_call_started;
    /// Una llamada está lista: antes de revisar su permiso y de ejecutarla.
    std::function<void(const ToolCallInfo& call)> on_tool_ready;
    /// Una llamada recibió su resultado: también rechazada, cancelada o sin ejecutar.
    std::function<void(const ToolCallInfo& call, const ToolResult& result)> on_tool_result;
    /// Cambió la lista de tareas (todo_write).
    std::function<void(const std::vector<TodoItem>& todos)> on_todos;
    std::function<void(LoopState state)> on_state;
    /// Aviso en español para el usuario (por qué se detuvo el turno).
    std::function<void(const std::string& notice)> on_notice;
    /// Fin del turno, después de su último on_state(Idle).
    std::function<void(const TurnOutcome& outcome)> on_turn_end;
};

/// Lo que CodeSession necesita del mundo, inyectable para las pruebas. root va primero porque no
/// tiene valor por defecto: se arma con inicializadores designados.
struct CodeSessionEnvironment {
    ProjectRoot root;
    /// El cliente del modelo (CurlTransport en la app); obligatorio para send().
    std::unique_ptr<ChatClient> client{};
    ToolRegistry registry = ToolRegistry::standard();
    /// Para build_code_system_prompt (reloj, instrucciones personales, git). Su run_process es
    /// también el de las herramientas (git y rg).
    SystemPromptOptions prompt_options{};
    /// Se ocultan en todo resultado de herramienta (hide_secret): la key del modelo y las demás.
    std::vector<std::string> secrets{};
    CodeLimits limits{};
    PermissionEngine permissions{};
    /// false (moss code -p): lo que haya que preguntar se rechaza sin esperar.
    bool interactive = true;
};

class AgentLoop;

/// Una sesión del modo código: al modo código lo que Session es al chat. Se usa solo desde el
/// hilo de la interfaz. Arma las instrucciones de sistema una vez (build_code_system_prompt; las
/// del chat no aplican) y es dueña del historial, del estado de las herramientas y del ciclo del
/// agente: la petición al modelo y cada herramienta corren en un hilo de trabajo, uno a la vez,
/// y regresan por post. Invariante: cada llamada del modelo recibe exactamente un mensaje tool,
/// justo después de su asistente, pase lo que pase (también al cancelar o destruir la sesión);
/// el historial pasa la validación del núcleo cada vez que puede mandarse. No guarda nada en
/// disco (C5).
class CodeSession {
public:
    using Task = std::function<void()>;
    /// Pasa una tarea al hilo de la interfaz (como RequestRunner::Post). Se llama desde el hilo
    /// de trabajo y desde el de la interfaz: debe ser segura entre hilos. Las tareas que corran
    /// después de destruir la sesión no hacen nada.
    using Post = std::function<void(Task)>;

    /// Arma las instrucciones de sistema (corre git con su tiempo límite corto).
    CodeSession(CodeSessionEnvironment environment, Post post, CodeObserver observer);
    /// Cancela lo que esté en curso y espera al hilo de trabajo (< 2 s); las llamadas sin
    /// resultado reciben el de cancelación. No avisa al observador.
    ~CodeSession();

    CodeSession(const CodeSession&) = delete;
    CodeSession& operator=(const CodeSession&) = delete;
    CodeSession(CodeSession&&) = delete;
    CodeSession& operator=(CodeSession&&) = delete;

    /// Agrega el mensaje del usuario y empieza un turno. false si hay uno en curso, el texto está
    /// vacío o no hay cliente.
    [[nodiscard]] bool send(std::string text);

    /// Cancela el turno: en Requesting se descarta la respuesta a medias; en Running la
    /// herramienta recibe la cancelación; en ambos, el turno termina cuando regresa el hilo de
    /// trabajo. En AwaitingApproval termina ya: la llamada queda rechazada. Las llamadas sin
    /// resultado reciben "Cancelled by the user.".
    void cancel();

    /// true desde send() hasta el fin del turno.
    [[nodiscard]] bool busy() const;
    [[nodiscard]] LoopState state() const;

    /// La llamada que espera aprobación, solo en AwaitingApproval.
    [[nodiscard]] std::optional<ToolCallInfo> pending_approval() const;

    /// Responde la aprobación pendiente. Reject le dice al modelo "The user rejected this
    /// action." (y " Reason: <reason>" si no está vacío) y el turno sigue. false si no hay
    /// ninguna pendiente.
    bool respond_approval(ApprovalDecision decision, const std::string& reason = {});

    /// Cambia el cliente del modelo y la lista de secretos (reemplaza la anterior: pasa todos
    /// los que deban ocultarse) desde el siguiente turno, al guardar en F2 o con /model. No toca
    /// el historial, las tareas, las instrucciones ni los límites. false, sin cambiar nada, si
    /// hay un turno en curso (busy(), también en AwaitingApproval) o client es nulo; en ese caso
    /// client se destruye.
    [[nodiscard]] bool set_client(std::unique_ptr<ChatClient> client,
                                  std::vector<std::string> secrets);

    [[nodiscard]] const std::vector<Message>& history() const { return history_; }
    [[nodiscard]] const std::vector<TodoItem>& todos() const;
    /// Las instrucciones de sistema y qué archivos se cargaron.
    [[nodiscard]] const CodeSystemPrompt& instructions() const { return instructions_; }

private:
    CodeSessionEnvironment env_;
    CodeSystemPrompt instructions_;
    std::vector<Message> history_;
    ToolState tools_;
    /// Al final: se destruye primero, mientras lo demás sigue vivo.
    std::unique_ptr<AgentLoop> loop_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_CODE_SESSION_H
