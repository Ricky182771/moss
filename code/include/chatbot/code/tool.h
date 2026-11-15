#ifndef CHATBOT_CODE_TOOL_H
#define CHATBOT_CODE_TOOL_H

#include "chatbot/cancel_token.h"
#include "chatbot/chat_client.h"
#include "chatbot/child_process.h"
#include "chatbot/code/project_root.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot::code {

/// Cómo terminó una llamada. El ciclo del agente cuenta las InvalidArguments seguidas
/// (guias/18-moss-code.md, sección 2).
enum class ToolStatus {
    Ok,
    Error,            ///< La herramienta falló (ruta fuera, binario, no existe…).
    InvalidArguments, ///< JSON roto, no es objeto, falta un campo o un tipo equivocado.
    UnknownTool,      ///< El modelo pidió una herramienta que no está en el registro.
};

/// Resultado de una herramienta (sección 4).
struct ToolResult {
    std::string text;    ///< Lo que recibe el modelo: en inglés, nunca vacío.
    std::string display; ///< Para la pantalla: español, una línea ("180 líneas"). Puede traer
                         ///< texto del modelo (una ruta): quien dibuja lo pasa por md::sanitize.
    ToolStatus status = ToolStatus::Ok;

    [[nodiscard]] bool is_error() const { return status != ToolStatus::Ok; }
};

/// Lo que anota read de un archivo leído bien: "leer antes de editar" (C3) lo compara con un
/// read_file nuevo.
struct ReadRecord {
    std::uint64_t size = 0;
    std::int64_t modified = 0;
};

enum class TodoStatus { Pending, InProgress, Completed };

/// Una tarea de la lista de todo_write.
struct TodoItem {
    std::string content;
    TodoStatus status = TodoStatus::Pending;
    std::string active_form;
};

/// Estado de la sesión que las herramientas leen o cambian. Lo toca solo quien ejecuta la
/// herramienta en ese momento (nunca dos llamadas a la vez).
struct ToolState {
    std::map<std::filesystem::path, ReadRecord> reads; ///< Por ruta canónica.
    std::vector<TodoItem> todos;
};

/// Lanzador de procesos con la forma de chatbot::process::run_process; las pruebas lo cambian.
using ProcessLauncher = std::function<process::ProcessResult(
    const std::vector<std::string>& argv, const process::ProcessOptions& options)>;

/// Lo que recibe cada herramienta al ejecutarse. Quien lo arma mantiene vivos root, cancel y state.
struct ToolContext {
    const ProjectRoot& root;
    const CancelToken& cancel;
    ToolState& state;
    /// Se ocultan con hide_secret en todo resultado: la key del modelo y las que se agreguen.
    std::vector<std::string> secrets{};
    /// Para glob y grep (git, rg).
    ProcessLauncher run_process = process::run_process;
};

/// Una herramienta; se define en src/ (usa nlohmann::json, dependencia privada).
struct Tool;

/// Registro de herramientas: inmutable, se puede usar desde varios hilos. Nada lanza
/// excepciones hacia fuera.
class ToolRegistry {
public:
    /// Las herramientas del modo código: read (C1, las demás se agregan aquí).
    [[nodiscard]] static ToolRegistry standard();

    explicit ToolRegistry(std::vector<std::shared_ptr<const Tool>> tools)
        : tools_(std::move(tools)) {}

    /// Nombres, descripciones y esquemas (en inglés) para RequestOptions::tools.
    [[nodiscard]] std::vector<ToolSpec> specs() const;

    /// Nombre en pantalla ("Leer"); nullopt si no existe.
    [[nodiscard]] std::optional<std::string> label(std::string_view name) const;

    /// Resumen de una línea, en español, antes de ejecutar: "Leer cli/src/main.cpp (1–200)".
    /// Con argumentos inválidos, solo la etiqueta; herramienta desconocida, "Herramienta
    /// desconocida: <nombre>". Trae texto del modelo: quien dibuja lo pasa por md::sanitize.
    [[nodiscard]] std::string summarize(std::string_view name,
                                        std::string_view raw_arguments) const;

    /// Parsea raw_arguments, lo valida contra el esquema, ejecuta, y al resultado le aplica
    /// hide_secret (context.secrets) y cap_tool_output.
    [[nodiscard]] ToolResult execute(std::string_view name, std::string_view raw_arguments,
                                     ToolContext& context) const;

private:
    [[nodiscard]] const Tool* find(std::string_view name) const;

    std::vector<std::shared_ptr<const Tool>> tools_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_TOOL_H
