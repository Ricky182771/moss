#include "tool_history.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace chatbot {
namespace {

ChatError history_error(std::string message) {
    return ChatError{ErrorKind::Config, 0, std::move(message), std::nullopt};
}

std::string quote_id(std::string_view id) { return "«" + std::string{id} + "»"; }

} // namespace

std::optional<ChatError> validate_tool_history(const std::vector<Message>& messages) {
    std::set<std::string_view> seen;       // Ids de todas las llamadas del historial.
    std::vector<std::string_view> pending; // Llamadas del último asistente sin resultado.
    for (const Message& message : messages) {
        if (message.role != Role::Assistant && !message.tool_calls.empty()) {
            return history_error("Solo un mensaje del asistente puede pedir herramientas.");
        }
        if (message.role == Role::Tool) {
            const std::string_view id = message.tool_call_id;
            if (id.empty()) {
                return history_error("Hay un resultado de herramienta sin el id de su llamada.");
            }
            const auto it = std::find(pending.begin(), pending.end(), id);
            if (it != pending.end()) {
                pending.erase(it);
                continue;
            }
            if (seen.count(id) > 0) {
                return history_error("El resultado de herramienta " + quote_id(id) +
                                     " responde una llamada que ya tenía resultado.");
            }
            return history_error("El resultado de herramienta " + quote_id(id) +
                                 " no corresponde a ninguna llamada pendiente del asistente.");
        }
        if (message.role == Role::System) {
            continue; // Solo User y Assistant cierran las llamadas pendientes.
        }
        if (!pending.empty()) {
            return history_error("La llamada a herramienta " + quote_id(pending.front()) +
                                 " no tiene su resultado antes del siguiente mensaje.");
        }
        for (const ToolCall& call : message.tool_calls) {
            if (call.id.empty()) {
                return history_error("Hay una llamada a herramienta sin id.");
            }
            if (!seen.insert(std::string_view{call.id}).second) {
                return history_error("El id de llamada " + quote_id(call.id) +
                                     " está repetido en el historial.");
            }
            pending.push_back(call.id);
        }
    }
    if (!pending.empty()) {
        return history_error("La llamada a herramienta " + quote_id(pending.front()) +
                             " no tiene su resultado al final del historial.");
    }
    return std::nullopt;
}

} // namespace chatbot
