#ifndef CHATBOT_CLI_CODE_COMMAND_H
#define CHATBOT_CLI_CODE_COMMAND_H

#include "addon_launcher.h"

#include "chatbot/chat_client.h"
#include "chatbot/config.h"
#include "chatbot/result.h"

#include <ctime>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// moss code (guias/18-moss-code.md): el modo código. Aquí, la forma sin pantalla,
/// moss code [carpeta] -p <texto>; la pantalla es code_screen.h. Sin FTXUI.
namespace chatbot::cli {

/// Códigos de salida de moss code -p (los de sysexits.h donde aplica).
inline constexpr int kExitCodeError = 1;      ///< Modelo, configuración o un límite del turno.
inline constexpr int kExitInterrupted = 130;  ///< Ctrl+C (128 + SIGINT).

/// Lo que moss code -p necesita del mundo, inyectable para las pruebas. Los flujos y lo que se
/// use mientras corre el turno viven más que la sesión de código (run_code la destruye antes).
struct CodeEnvironment {
    std::ostream& out; ///< Solo la respuesta final.
    std::ostream& err; ///< Una línea por herramienta, avisos y errores.
    /// Modo portable y configuración (load_startup_config en la app).
    std::function<Result<Config>()> load_config;
    std::optional<std::string> config_path;      ///< default_config_path().
    std::optional<std::string> credentials_path; ///< default_credentials_path().
    std::optional<std::string> data_dir;         ///< default_data_dir() (resolve_data_dir).
    /// La carpeta de datos de la app por defecto (<datos>/moss, el padre de las conversaciones
    /// por defecto), aunque CHAT_DATA_DIR mande data_dir a otra parte: ninguna se puede abrir.
    std::optional<std::string> app_data_dir;
    std::optional<std::string> portable_dir;     ///< process_portable_data_dir().
    std::string home;                            ///< La carpeta personal; vacía si no se sabe.
    /// Secretos del entorno (CHAT_SEARCH_API_KEY, CHAT_TELEGRAM_TOKEN): se suman a la key del
    /// modelo y a las de credentials.json para taparlos en lo que va al modelo. Nunca se imprimen.
    std::vector<std::string> env_secrets;
    std::function<std::unique_ptr<ChatClient>(const Config&)> make_client;
    /// true cuando el usuario pidió interrumpir (Ctrl+C). Se consulta desde el hilo principal.
    std::function<bool()> interrupted;
    std::function<std::time_t()> now;
};

/// Corre moss code -p (line.kind == Code, con line.prompt) y devuelve el código de salida:
/// 0 respuesta final; 1 error del modelo, de configuración o un límite del turno (también sin
/// prompt, que no debe llegar aquí); 130 interrumpido. Carpeta: line.folder o la actual. Sin Moss
/// Code en la compilación (CHATBOT_BUILD_CODE=OFF) esta función no existe: solo run_code_command.
[[nodiscard]] int run_code(const CommandLine& line, CodeEnvironment& environment);

/// run_code con el sistema real (flujos estándar, configuración de la app y Ctrl+C). Con
/// CHATBOT_BUILD_CODE=OFF dice "Moss Code no está incluido en esta compilación." y devuelve 69.
[[nodiscard]] int run_code_command(const CommandLine& line);

/// moss code [carpeta] sin -p: la pantalla de Moss Code (code_screen.cpp, en el ejecutable moss).
/// Errores de arranque por stderr antes de abrirla (salida 1); al cerrarla, 0. Con
/// CHATBOT_BUILD_CODE=OFF, lo mismo que run_code_command (69).
[[nodiscard]] int run_code_screen(const std::string& folder);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_CODE_COMMAND_H
