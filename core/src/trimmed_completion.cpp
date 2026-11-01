#include "chatbot/trimmed_completion.h"

#include "chatbot/error.h"
#include "chatbot/history.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot {
namespace {

std::string lowercase(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return out;
}

bool word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/// key aparece en text (los dos en minúsculas) como palabra completa: antes y
/// después, el borde del texto o algo que no es [a-z0-9_].
bool contains_word(std::string_view text, std::string_view key) {
    if (key.empty()) {
        return false;
    }
    for (std::size_t at = text.find(key); at != std::string_view::npos;
         at = text.find(key, at + 1)) {
        const std::size_t end = at + key.size();
        if ((at == 0 || !word_char(text[at - 1])) && (end == text.size() || !word_char(text[end]))) {
            return true;
        }
    }
    return false;
}

/// El error nombra una de las llaves que pusieron los ajustes del modelo
/// ("Unsupported parameter: temperature"): recortar el historial no lo arregla.
bool names_param_key(const ChatError& error, const std::vector<std::string>& keys) {
    const std::string message = lowercase(error.message);
    return std::any_of(keys.begin(), keys.end(), [&message](const std::string& key) {
        return contains_word(message, lowercase(key));
    });
}

std::size_t total_bytes(const std::vector<Message>& messages) {
    std::size_t total = 0;
    for (const Message& message : messages) {
        total += message.content.size();
    }
    return total;
}

} // namespace

TrimmedCompletion complete_trimmed(ChatClient& client, const std::vector<Message>& history,
                                   std::size_t limit_bytes, const StreamDeltaCallback& on_delta,
                                   const CancelToken* cancel) {
    bool delivered = false;
    // Cualquier delta cuenta, también el pensamiento: ya se mostró algo.
    const StreamDeltaCallback deliver = [&](const StreamDelta& delta) {
        delivered = true;
        return on_delta(delta);
    };

    const TrimResult first = trim_history(history, limit_bytes);
    TrimmedCompletion done{client.complete_stream(first.messages, deliver, cancel), first.dropped};

    // Reintento por contexto: una sola vez, si el servidor rechazó la
    // petición antes de mandar texto y recortar a la mitad quita algo más.
    if (done.result.is_error() && done.result.error().kind == ErrorKind::InvalidRequest &&
        !delivered && (cancel == nullptr || !cancel->is_cancelled()) &&
        !names_param_key(done.result.error(), client.param_keys())) {
        // Mitad de min(límite, tamaño real): si el historial ya es menor
        // que el límite, recortar a la mitad del límite no quitaría nada.
        const std::size_t current = total_bytes(first.messages);
        const std::size_t base = limit_bytes > 0 ? std::min(limit_bytes, current) : current;
        // trim_history interpreta 0 como "sin límite": el mínimo es 1.
        const TrimResult second = trim_history(history, std::max<std::size_t>(base / 2, 1));
        if (second.dropped > first.dropped) {
            TrimmedCompletion retry{client.complete_stream(second.messages, deliver, cancel),
                                    second.dropped};
            // Si el reintento también falla (sin cancelación ni texto
            // entregado), el error que sirve es el del primero: sin el aviso
            // de mensajes omitidos que no arreglaron nada.
            const bool retry_useless = retry.result.is_error() && !delivered &&
                                       retry.result.error().kind != ErrorKind::Cancelled;
            if (!retry_useless) {
                done = std::move(retry);
            }
        }
    }
    return done;
}

} // namespace chatbot
