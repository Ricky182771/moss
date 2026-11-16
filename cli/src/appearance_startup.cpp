#include "appearance_startup.h"

#include "chatbot/config.h"
#include "chatbot/platform.h"
#include "chatbot/result.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace chatbot::cli {

namespace {

/// Valor de una variable de entorno, o nullopt si no existe.
std::optional<std::string> env_value(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
}

} // namespace

StartupAppearance load_startup_appearance() {
    // Tema, fondo y símbolos de config.json ("appearance"). Un valor
    // desconocido usa el de por defecto y deja un aviso en la línea de
    // estado. Símbolos "Automático": Sencillos solo en la consola de Windows 10.
    const chatbot::cli::Symbols detected_symbols = chatbot::cli::detect_symbols(
        chatbot::current_os(), chatbot::windows_build(), env_value("WT_SESSION").has_value(),
        env_value("TERM_PROGRAM").has_value());
    std::string appearance_warning;
    chatbot::cli::Appearance appearance =
        chatbot::cli::resolve_appearance(chatbot::AppearanceValues{}, detected_symbols);
    if (const std::optional<std::string> path = chatbot::default_config_path()) {
        const chatbot::Result<chatbot::AppearanceValues> saved =
            chatbot::load_appearance_values(*path);
        if (saved.is_ok()) {
            appearance = chatbot::cli::resolve_appearance(saved.value(), detected_symbols,
                                                          &appearance_warning);
        } else {
            appearance_warning = saved.error().message;
        }
    }
    return {appearance, detected_symbols, std::move(appearance_warning)};
}

} // namespace chatbot::cli
