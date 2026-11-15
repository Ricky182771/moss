#ifndef CHATBOT_CODE_SRC_TOOL_INTERNAL_H
#define CHATBOT_CODE_SRC_TOOL_INTERNAL_H

#include "chatbot/code/project_root.h"
#include "chatbot/code/tool.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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
[[nodiscard]] std::shared_ptr<const Tool> make_grep_tool();
[[nodiscard]] std::shared_ptr<const Tool> make_todo_write_tool();

/// glob y grep con los programas en estas rutas absolutas (nullopt: sin git, se recorre la
/// carpeta; sin rg, solo la búsqueda propia). Las herramientas los llaman con find_program; las
/// pruebas, con lo que necesiten. arguments ya validados.
[[nodiscard]] ToolResult run_glob(const nlohmann::json& arguments, ToolContext& context,
                                  const std::optional<std::string>& git_path);
[[nodiscard]] ToolResult run_grep(const nlohmann::json& arguments, ToolContext& context,
                                  const std::optional<std::string>& rg_path,
                                  const std::optional<std::string>& git_path);

/// process::find_in_path, pero solo si da una ruta absoluta: nunca un programa de la carpeta
/// actual (el proyecto) por una entrada relativa del PATH. En Windows, nullopt.
[[nodiscard]] std::optional<std::string> find_program(std::string_view name);

/// Filtro de list_project_files: la ruta relativa a la carpeta base, con '/'.
using RelativeFilter = std::function<bool(std::string_view relative)>;
using ProjectFiles = std::variant<std::vector<WalkFile>, PathError>;

/// Los archivos normales bajo base (carpeta ya resuelta) que acepta keep, para glob y grep: los
/// de `git ls-files --cached --others --exclude-standard` (respeta .gitignore) o, si git no se
/// puede usar, recorriendo la carpeta sin .git, node_modules ni build*. Error solo si base no se
/// puede recorrer. git_path: ruta absoluta de git (nullopt: se recorre directamente). Si se
/// cancela, la lista puede quedar incompleta: quien llama lo revisa.
[[nodiscard]] ProjectFiles list_project_files(ToolContext& context,
                                              const std::filesystem::path& base,
                                              const RelativeFilter& keep,
                                              const std::optional<std::string>& git_path);

/// La ruta en UTF-8 (en POSIX, los bytes tal cual), con separadores nativos o con '/'.
[[nodiscard]] std::string utf8_text(const std::filesystem::path& path);
[[nodiscard]] std::string generic_text(const std::filesystem::path& path);

/// "Cancelled by the user." (Error).
[[nodiscard]] ToolResult cancelled_result();

/// Texto del modelo para mostrar: como mucho max_bytes (sin partir un carácter), con "…" si se
/// corta.
[[nodiscard]] std::string shorten(std::string_view text, std::size_t max_bytes);

/// Error de ruta como resultado: el mensaje (inglés) tal cual y un display en español.
[[nodiscard]] ToolResult path_error_result(const PathError& error);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_TOOL_INTERNAL_H
