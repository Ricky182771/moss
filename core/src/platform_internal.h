#ifndef CHATBOT_PLATFORM_INTERNAL_H
#define CHATBOT_PLATFORM_INTERNAL_H

#include <functional>
#include <string>

namespace chatbot {

/// El destino de /proc/self/exe sin el " (deleted)" que le agrega Linux
/// cuando el archivo se reemplazó con el proceso abierto (un dnf upgrade con
/// moss abierto): si target no existe y la ruta sin el sufijo sí, esa es la
/// del binario nuevo, en el mismo lugar. Si no, target tal cual.
/// Header interno (src/); no forma parte de la API pública.
[[nodiscard]] std::string without_deleted_suffix(
    std::string target, const std::function<bool(const std::string& path)>& exists);

} // namespace chatbot

#endif // CHATBOT_PLATFORM_INTERNAL_H
