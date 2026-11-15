#ifndef CHATBOT_CODE_SRC_TOOL_INTERNAL_H
#define CHATBOT_CODE_SRC_TOOL_INTERNAL_H

#include "chatbot/code/project_root.h"
#include "chatbot/code/tool.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace chatbot::code {

/// Una herramienta del registro. summarize y run reciben los argumentos ya validados contra
/// schema (y normalizados: "100" ya es 100).
struct Tool {
    std::string name;        ///< En inglés, para el modelo.
    std::string description; ///< En inglés, para el modelo.
    nlohmann::json schema;   ///< Subconjunto de JSON Schema que entiende validate_arguments.
    std::string label;       ///< En español, para la pantalla ("Leer").
    /// Lo que sigue a la etiqueta en el resumen ("cli/src/main.cpp (1–200)"); vacío: solo etiqueta.
    std::function<std::string(const nlohmann::json& arguments)> summarize;
    std::function<ToolResult(const nlohmann::json& arguments, ToolContext& context)> run;
};

[[nodiscard]] std::shared_ptr<const Tool> make_read_tool();
[[nodiscard]] std::shared_ptr<const Tool> make_glob_tool();
[[nodiscard]] std::shared_ptr<const Tool> make_todo_write_tool();

/// Texto del modelo para mostrar: como mucho max_bytes (sin partir un carácter), con "…" si se
/// corta.
[[nodiscard]] std::string shorten(std::string_view text, std::size_t max_bytes);

/// Error de ruta como resultado: el mensaje (inglés) tal cual y un display en español.
[[nodiscard]] ToolResult path_error_result(const PathError& error);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_TOOL_INTERNAL_H
