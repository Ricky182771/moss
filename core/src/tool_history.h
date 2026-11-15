#ifndef CHATBOT_TOOL_HISTORY_H
#define CHATBOT_TOOL_HISTORY_H

#include "chatbot/error.h"
#include "chatbot/types.h"

#include <optional>
#include <vector>

namespace chatbot {

/// Revisa las llamadas a herramientas del historial antes de mandarlo
/// (guias/18-moss-code.md, 3.1): tool_calls solo en Assistant; cada llamada
/// con id, sin ids repetidos en todo el historial, y con exactamente un Tool
/// con su id justo después de su asistente: con llamadas pendientes, cualquier
/// mensaje que no sea Tool (también un System) o el final es error; ningún
/// Tool sin tool_call_id, sin llamada pendiente o repetido. Los System sin
/// llamadas pendientes pasan. Error Config que dice la regla y el id; nullopt
/// si todo cuadra. Pura.
[[nodiscard]] std::optional<ChatError> validate_tool_history(const std::vector<Message>& messages);

} // namespace chatbot

#endif // CHATBOT_TOOL_HISTORY_H
