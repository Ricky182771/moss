#ifndef CHATBOT_CLI_APPEARANCE_STARTUP_H
#define CHATBOT_CLI_APPEARANCE_STARTUP_H

#include "glyphs.h"
#include "theme.h"

#include <string>

namespace chatbot::cli {

/// Lo que da el arranque de la apariencia.
struct StartupAppearance {
    Appearance appearance;
    /// Símbolos de "Automático" (detect_symbols): se vuelven a usar al
    /// guardar otra apariencia.
    Symbols detected_symbols = Symbols::Unicode;
    /// Aviso de la línea de estado (valor desconocido o config.json ilegible),
    /// o vacío.
    std::string warning;
};

/// Tema, fondo y símbolos de config.json ("appearance"). Un valor
/// desconocido usa el de por defecto y deja un aviso. Símbolos "Automático":
/// Sencillos solo en la consola de Windows 10.
[[nodiscard]] StartupAppearance load_startup_appearance();

} // namespace chatbot::cli

#endif // CHATBOT_CLI_APPEARANCE_STARTUP_H
