#ifndef CHATBOT_CODE_SRC_TOOL_SCHEMA_H
#define CHATBOT_CODE_SRC_TOOL_SCHEMA_H

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace chatbot::code {

/// Valida arguments contra schema, el subconjunto de JSON Schema de nuestras herramientas:
/// type (object con properties y required, string, integer, boolean, array con items), enum y
/// minimum. nullopt si cumple; si no, qué corregir, en inglés para el modelo ("Missing required
/// argument: path"). Tolerante con lo que mandan los modelos, y normaliza en sitio: propiedades
/// de más se ignoran, null en una opcional cuenta como ausente (se quita), "100" y 100.0 valen
/// como entero y "true"/"false" como booleano. No lanza.
[[nodiscard]] std::optional<std::string> validate_arguments(const nlohmann::json& schema,
                                                            nlohmann::json& arguments);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_TOOL_SCHEMA_H
