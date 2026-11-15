#ifndef CHATBOT_COMPLETION_INFO_H
#define CHATBOT_COMPLETION_INFO_H

#include "chatbot/types.h"

#include <string>
#include <vector>

namespace chatbot {

/// Datos de una respuesta de streaming que terminó bien.
struct CompletionInfo {
    /// Último valor no nulo de choices[0].finish_reason que llegó ("stop",
    /// "length", "content_filter", ...), o vacío si nunca llegó.
    std::string finish_reason;
    /// Las llamadas a herramientas que pidió el modelo, en orden de
    /// aparición; solo si la petición mandó herramientas. Mandan aunque
    /// finish_reason no sea "tool_calls". arguments sin validar ("{}" si
    /// llegó vacío); name puede ir vacío si nunca llegó.
    std::vector<ToolCall> tool_calls{};
    /// Llave del primer pensamiento que llegó; ReasoningContent si no llegó ninguno.
    ReasoningKey reasoning_key{ReasoningKey::ReasoningContent};
};

} // namespace chatbot

#endif // CHATBOT_COMPLETION_INFO_H
