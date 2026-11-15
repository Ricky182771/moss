#ifndef CHATBOT_TYPES_H
#define CHATBOT_TYPES_H

#include <string>
#include <vector>

namespace chatbot {

/// Rol de un mensaje en la conversación.
enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

/// Llave JSON en que llegó el pensamiento del modelo.
enum class ReasoningKey {
    ReasoningContent, ///< "reasoning_content"
    Reasoning,        ///< "reasoning"
};

/// Una llamada a herramienta pedida por el modelo.
struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments; ///< JSON crudo, tal como llegó (se valida al ejecutar).
};

/// Un mensaje de la conversación. El historial lo administra quien llama;
/// el núcleo no lo guarda (hito 1).
struct Message {
    Role role;
    std::string content;
    std::vector<ToolCall> tool_calls{}; ///< Solo Assistant.
    std::string tool_call_id{};         ///< Solo Tool.
    std::string reasoning{};            ///< Solo Assistant.
    ReasoningKey reasoning_key{ReasoningKey::ReasoningContent};
};

} // namespace chatbot

#endif // CHATBOT_TYPES_H
