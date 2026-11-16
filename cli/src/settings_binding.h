#ifndef CHATBOT_CLI_SETTINGS_BINDING_H
#define CHATBOT_CLI_SETTINGS_BINDING_H

#include "glyphs.h"
#include "models_loader.h"
#include "session.h"
#include "settings_screen.h"
#include "theme.h"

#include <functional>
#include <optional>
#include <string>

namespace chatbot::cli {

// La pantalla de configuración (F2) cableada contra una Session: lo que
// comparten la pantalla del chat y la del modo código. Usa SettingsScreen
// (ftxui::component): solo en el ejecutable moss.

/// Lo que F2 lee y guarda. Todo lo referido debe vivir más que la
/// SettingsScreen; los callbacks (los tres obligatorios) se copian a ella.
struct SettingsContext {
    Session& session;       ///< De donde se lee y por donde se guarda todo salvo la apariencia.
    Appearance& appearance; ///< La vigente; cambia al guardar la apariencia.
    Palette& palette;       ///< Con lo que se dibuja todo; cambia al guardar la apariencia.
    /// Símbolos de "Automático" (StartupAppearance::detected_symbols).
    Symbols detected_symbols = Symbols::Unicode;
    /// Se guardó algo por la sesión (proveedor, instrucciones, búsqueda o
    /// ajustes del modelo); no con la apariencia.
    std::function<void()> on_saved;
    /// Justo antes de abrir la pantalla (open_settings).
    std::function<void()> on_open;
    /// La pantalla se cerró (guardada o descartada).
    std::function<void()> on_close;
};

/// La SettingsScreen con los guardados de context (se construye en el lugar:
/// SettingsScreen no se mueve).
[[nodiscard]] SettingsScreen make_settings_screen(ModelsLoader& loader,
                                                  const SettingsContext& context);

/// Lo demás de settings: la carpeta portable, los ajustes del modelo, borrar
/// un endpoint y el sistema para "Addons" (con post).
void bind_settings(SettingsScreen& settings, const SettingsContext& context, Session::Post post);

/// F2 o "⚙ Configuración": abre con lo vigente de la sesión y un aviso
/// opcional; si ya estaba abierta, solo le da el foco. El aviso para la línea
/// de estado si no se pudo abrir (respuesta en curso o error al leer); si no,
/// nullopt.
[[nodiscard]] std::optional<std::string> open_settings(SettingsScreen& settings,
                                                       const SettingsContext& context,
                                                       std::string notice);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_SETTINGS_BINDING_H
