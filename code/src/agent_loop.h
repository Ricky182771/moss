#ifndef CHATBOT_CODE_SRC_AGENT_LOOP_H
#define CHATBOT_CODE_SRC_AGENT_LOOP_H

#include "chatbot/cancel_token.h"
#include "chatbot/chat_client.h"
#include "chatbot/code/code_session.h"
#include "chatbot/code/tool.h"
#include "chatbot/result.h"
#include "chatbot/types.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace chatbot::code {

/// La máquina de estados de un turno (guias/18-moss-code.md, sección 2), en el hilo de la
/// interfaz. La petición al modelo y cada herramienta corren en un hilo de trabajo (uno a la
/// vez), que regresa por post. Usa lo que es de CodeSession (entorno, historial, estado de las
/// herramientas), que vive más que ella. La API es la de CodeSession.
class AgentLoop {
public:
    AgentLoop(CodeSessionEnvironment& env, std::vector<Message>& history, ToolState& tools,
              CodeSession::Post post, CodeObserver observer);
    ~AgentLoop();

    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;
    AgentLoop(AgentLoop&&) = delete;
    AgentLoop& operator=(AgentLoop&&) = delete;

    bool send(std::string text);
    void cancel();
    [[nodiscard]] LoopState state() const { return state_; }
    [[nodiscard]] std::optional<ToolCallInfo> pending_approval() const;
    bool respond_approval(ApprovalDecision decision, const std::string& reason);
    bool set_client(std::unique_ptr<ChatClient> client, std::vector<std::string> secrets);
    [[nodiscard]] const std::vector<TodoItem>& todos() const { return todos_; }

private:
    /// Una llamada de la respuesta en curso.
    struct PendingCall {
        ToolCallInfo info;
        std::string raw_name;      ///< Tal como llegó.
        std::string raw_arguments; ///< Tal como llegó.
        bool valid_name = false;   ///< [A-Za-z0-9_-], 1 a 64 bytes.
        bool valid_arguments = false; ///< Un objeto JSON (o vacío).
    };

    void start_request();
    void on_request_done(Result<CompletionInfo> result, std::string text, std::string reasoning);
    /// Atiende las llamadas desde next_ hasta que una tenga que esperar (hilo de trabajo o
    /// aprobación); con todas respondidas, la siguiente petición.
    void next_call();
    void run_call();
    void on_tool_done(ToolResult result, std::vector<TodoItem> todos);
    /// La llamada actual (nombre o argumentos inválidos, herramienta desconocida) sin ejecutar
    /// nada: el error que da el registro.
    [[nodiscard]] ToolResult invalid_call_result(const PendingCall& call);
    /// Responde la llamada actual y cuenta las inválidas seguidas; true si eso detuvo el turno.
    bool record(const ToolResult& result);
    void answer(const ToolResult& result);
    void answer_rest(const ToolResult& result);
    /// Responde lo pendiente con "Not run: <model_note>", avisa y termina el turno.
    void stop(TurnEnd reason, const std::string& model_note, std::string notice);
    void finish(TurnOutcome outcome);
    void set_state(LoopState state);
    void notify(std::function<void(const CodeObserver&)> call);
    /// Corre job en el hilo de trabajo (antes, join del anterior, que ya terminó).
    void launch(std::function<void()> job);

    CodeSessionEnvironment& env_;
    std::vector<Message>& history_;
    ToolState& tools_; ///< Solo lo toca el hilo de trabajo mientras corre una herramienta.
    CodeSession::Post post_;
    CodeObserver observer_;
    RequestOptions options_;
    /// false desde el destructor: las tareas que sigan en la cola no hacen nada. Solo se lee y
    /// escribe en el hilo de la interfaz.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    LoopState state_ = LoopState::Idle;
    std::vector<PendingCall> calls_; ///< Las de la última respuesta.
    std::size_t next_ = 0;           ///< La primera sin resultado.
    std::size_t steps_ = 0;          ///< Peticiones en este turno.
    std::string last_call_;          ///< Nombre y argumentos de la llamada anterior.
    std::size_t repeated_ = 0;
    std::size_t invalid_ = 0;
    bool cancel_requested_ = false;
    std::vector<TodoItem> todos_; ///< Copia para la interfaz.

    /// Un token por turno. Vive hasta el siguiente send() o el destructor, que antes hacen join.
    std::unique_ptr<CancelToken> token_;
    std::thread worker_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_AGENT_LOOP_H
