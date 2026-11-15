#include "chatbot/history.h"

#include <iterator>

namespace chatbot {

std::size_t message_bytes(const Message& message) {
    std::size_t size = message.content.size() + message.reasoning.size() +
                       message.tool_call_id.size();
    for (const ToolCall& call : message.tool_calls) {
        size += call.id.size() + call.name.size() + call.arguments.size();
    }
    return size;
}

TrimResult trim_history(const std::vector<Message>& messages, std::size_t limit_bytes) {
    std::size_t total = 0;
    for (const Message& message : messages) {
        total += message_bytes(message);
    }
    if (limit_bytes == 0 || total <= limit_bytes || messages.size() < 2) {
        return TrimResult{messages, 0};
    }

    // Bloque de sistema inicial: nunca se quita.
    std::size_t system_end = 0;
    while (system_end < messages.size() && messages[system_end].role == Role::System) {
        ++system_end;
    }

    // El último grupo (desde el último User, o todo tras el sistema si no hay
    // User) nunca se quita ni se parte, aunque solo él exceda.
    std::size_t last_group = messages.size() - 1;
    while (last_group > system_end && messages[last_group].role != Role::User) {
        --last_group;
    }

    // Se quitan grupos completos, los más viejos primero: el mensaje del
    // frente y todo lo que no sea User. Así un Tool nunca queda sin su
    // llamada ni una llamada sin sus Tool, y tras el sistema sigue un User.
    std::size_t start = system_end;
    while (total > limit_bytes && start < last_group) {
        total -= message_bytes(messages[start]);
        ++start;
        while (start < last_group && messages[start].role != Role::User) {
            total -= message_bytes(messages[start]);
            ++start;
        }
    }

    TrimResult result;
    result.dropped = start - system_end;
    result.messages.reserve(messages.size() - result.dropped);
    result.messages.insert(result.messages.end(), messages.begin(),
                           std::next(messages.begin(), static_cast<std::ptrdiff_t>(system_end)));
    result.messages.insert(result.messages.end(),
                           std::next(messages.begin(), static_cast<std::ptrdiff_t>(start)),
                           messages.end());
    return result;
}

} // namespace chatbot
