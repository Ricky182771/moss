#ifndef CHATBOT_CODE_CODE_LIMITS_H
#define CHATBOT_CODE_CODE_LIMITS_H

#include <cstddef>
#include <string>
#include <vector>

namespace chatbot::code {

/// Límites de un turno del modo código (guias/18-moss-code.md, sección 2). Se leen de
/// config.json → "code" con load_code_limits; entre paréntesis, el rango aceptado.
struct CodeLimits {
    std::size_t max_steps = 50;         ///< Peticiones al modelo por turno (1–1000).
    std::size_t max_repeated_calls = 3; ///< La misma llamada (nombre y argumentos), seguidas (2–100).
    /// Llamadas seguidas con argumentos inválidos o a una herramienta desconocida (1–100).
    std::size_t max_invalid_calls = 3;
    /// Recorte del historial (trim_history), propio del modo código: el del chat (32 000 bytes)
    /// se llena con una sola lectura. 0: sin límite (0–100 000 000).
    std::size_t history_limit_bytes = 300000;
};

/// Lo que dio load_code_limits.
struct LoadedCodeLimits {
    CodeLimits limits;
    std::vector<std::string> warnings; ///< En español, para mostrar al arrancar.
};

/// Lee la sección "code" de config.json (load_config_section). Archivo o sección ausentes: los
/// valores por defecto, sin aviso. Un valor ausente usa el suyo por defecto; uno que no es un
/// entero o está fuera de rango, también, con un aviso. Si config.json no es JSON válido o
/// "code" no es un objeto, todos los de defecto con un aviso. Las demás llaves de "code" se
/// ignoran (son de otros hitos). No lanza excepciones.
[[nodiscard]] LoadedCodeLimits load_code_limits(const std::string& config_path);

} // namespace chatbot::code

#endif // CHATBOT_CODE_CODE_LIMITS_H
