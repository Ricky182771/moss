#ifndef CHATBOT_HISTORY_BYTES_H
#define CHATBOT_HISTORY_BYTES_H

#include "chatbot/types.h"

#include <cstddef>

namespace chatbot {

/// Bytes que cuenta el recorte: content y, si los hay, el pensamiento, el id
/// del resultado y cada llamada. Sin esos campos es solo content.size().
[[nodiscard]] std::size_t message_bytes(const Message& message);

} // namespace chatbot

#endif // CHATBOT_HISTORY_BYTES_H
