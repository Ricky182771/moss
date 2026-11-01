#ifndef CHATBOT_SRC_HIDE_SECRET_H
#define CHATBOT_SRC_HIDE_SECRET_H

#include <cstddef>
#include <string>
#include <string_view>

namespace chatbot {

/// Lo que queda en lugar de la key.
inline constexpr std::string_view kHiddenKey = "[key oculta]";

/// text con cada aparición de secret cambiada por kHiddenKey (sección 9):
/// un servidor puede repetir la key en su mensaje de error. Se usa antes de
/// truncar, para que no quede un pedazo. Una key de menos de 8 bytes (solo
/// en localhost) no se busca: cambiaría letras sueltas del mensaje.
/// Header interno (src/); no forma parte de la API pública.
[[nodiscard]] inline std::string hide_secret(std::string text, std::string_view secret) {
    if (secret.size() < 8) {
        return text;
    }
    for (std::size_t at = text.find(secret); at != std::string::npos;
         at = text.find(secret, at + kHiddenKey.size())) {
        text.replace(at, secret.size(), kHiddenKey);
    }
    return text;
}

} // namespace chatbot

#endif // CHATBOT_SRC_HIDE_SECRET_H
