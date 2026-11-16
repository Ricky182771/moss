#ifndef CHATBOT_CLI_PROMPT_MENUS_H
#define CHATBOT_CLI_PROMPT_MENUS_H

#include "command_menu.h"
#include "command_mode.h"
#include "model_picker.h"
#include "scroll.h"
#include "session.h"
#include "text_area.h"

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace chatbot::cli {

// El menú de "/" y el selector de /model sobre la caja, para la pantalla del
// chat y la del modo código: qué tecla y qué clic es de quién. Usa
// ftxui::component: solo en el ejecutable moss.

/// Lo que usan los menús. Todo lo referido debe vivir más que el uso.
struct PromptMenus {
    CommandMenu& command_menu;
    ModelPicker& model_picker;
    Session& session; ///< Por donde cambia el modelo (switch_model).
    TextArea& input;
    Scroll& scroll;
    std::string& flash; ///< El aviso de la línea de estado.
    /// Envía la caja (Submit del menú de "/"). Obligatorio.
    std::function<void()> send;
    CommandMode mode = CommandMode::Chat; ///< Los comandos del menú de "/".
    /// Tras Session::switch_model del selector, antes de su finish; vacío: nada.
    std::function<void(const ModelSwitch&)> on_switch;
};

/// Las cajas de los menús en el último cuadro (para el ratón). Las escribe
/// el renderer y las lee menus_mouse.
struct MenuBoxes {
    ftxui::Box command_menu;
    std::vector<ftxui::Box> command_menu_rows;
    ftxui::Box model_picker;
    std::vector<ftxui::Box> model_picker_rows;
    std::size_t model_picker_first = 0; ///< first() del cuadro de model_picker_rows.
};

/// Aplica lo que pidió el menú de comandos. true si el menú usó el evento.
bool apply_command_menu(const PromptMenus& menus, const CommandMenuAction& action);

/// Aplica lo que pidió el selector de /model. true si el selector usó el evento.
bool apply_model_picker(const PromptMenus& menus, const ModelPickerAction& action);

/// Teclas con la caja: primero el selector de /model (si está abierto y la
/// caja tiene el foco), luego el menú de "/"; ↑/↓, Tab, Enter y Esc son
/// suyos. true si alguno usó la tecla.
[[nodiscard]] bool menus_key(const PromptMenus& menus, const ftxui::Event& event);

/// El ratón sobre un menú abierto: hover mueve el cursor, clic elige (el
/// selector, como Enter) o completa (el menú de "/", como Tab) y la rueda
/// mueve el cursor del selector. true si cayó sobre un menú: nada de eso
/// llega al historial.
[[nodiscard]] bool menus_mouse(const PromptMenus& menus, const MenuBoxes& boxes,
                               const ftxui::Mouse& mouse);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_PROMPT_MENUS_H
