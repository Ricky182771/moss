#ifndef CHATBOT_CODE_SYSTEM_PROMPT_H
#define CHATBOT_CODE_SYSTEM_PROMPT_H

#include "chatbot/cancel_token.h"
#include "chatbot/code/project_root.h"
#include "chatbot/code/tool.h"

#include <cstddef>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace chatbot::code {

/// Tope de las instrucciones del usuario y del proyecto juntas, en bytes (guias/18-moss-code.md,
/// sección 6). Reparto: cada archivo tiene garantizada la mitad, y lo que uno no usa lo puede
/// usar el otro.
inline constexpr std::size_t kMaxInstructionsBytes = 40 * 1024;

/// Tope de `git status --short` en el bloque de entorno.
inline constexpr std::size_t kMaxGitStatusLines = 50;
inline constexpr std::size_t kMaxGitStatusBytes = 4 * 1024;

/// Las instrucciones fijas del modo código (en inglés, D7): code/src/system_prompt.md, embebido
/// al compilar.
[[nodiscard]] std::string_view fixed_system_prompt();

/// Qué archivo de instrucciones del proyecto se cargó (D5).
enum class ProjectInstructions { None, AgentsMd, ClaudeMd };

struct SystemPromptOptions {
    /// Fecha local del bloque de entorno.
    std::function<std::time_t()> now;
    /// <carpeta de configuración de Moss>/AGENTS.md (UTF-8), la calcula quien llama; nullopt:
    /// sin instrucciones personales.
    std::optional<std::string> personal_instructions;
    /// Si el modelo tiene la herramienta bash (en C1, nunca).
    bool commands_available = false;
    /// Para git status, sin shell.
    ProcessLauncher run_process = process::run_process;
    /// Ruta absoluta de git; nullopt: se busca en el PATH (solo rutas absolutas). Sin git, el
    /// bloque de entorno no trae esa parte.
    std::optional<std::string> git_path;
};

/// Las instrucciones de sistema de una sesión de código y lo que se cargó (para la pantalla:
/// "AGENTS.md cargado").
struct CodeSystemPrompt {
    std::string text;
    ProjectInstructions project = ProjectInstructions::None;
    bool personal_loaded = false;
    bool instructions_truncated = false; ///< Alguna de las dos se recortó a kMaxInstructionsBytes.
};

/// Arma las instrucciones de sistema (sección 6): las fijas, el bloque de entorno (carpeta,
/// sistema, fecha, git, comandos) y las instrucciones personales y del proyecto, cada una en su
/// bloque rotulado. AGENTS.md de la raíz (si no se puede cargar, CLAUDE.md) solo por
/// root.resolve y read_file: un enlace hacia fuera o a la carpeta de Moss no se carga. El
/// personal, solo si es un archivo normal (nunca una FIFO ni un dispositivo). Lo que no se puede
/// leer se omite. UTF-8 inválido se reemplaza y se quitan los controles. git corre con
/// options.run_process, tiempo límite corto y cancel. No lanza excepciones.
[[nodiscard]] CodeSystemPrompt build_code_system_prompt(const ProjectRoot& root,
                                                        const SystemPromptOptions& options,
                                                        const CancelToken& cancel);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SYSTEM_PROMPT_H
