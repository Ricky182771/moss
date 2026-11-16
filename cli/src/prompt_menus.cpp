#include "prompt_menus.h"

#include "key_events.h"

#include <optional>

namespace chatbot::cli {

bool apply_command_menu(const PromptMenus& menus, const CommandMenuAction& action) {
    using Type = CommandMenuAction::Type;
    switch (action.type) {
    case Type::Pass:
        return false;
    case Type::Handled:
        return true;
    case Type::Complete:
    case Type::Submit:
        menus.input.set_text(action.text); // El cursor al final.
        if (action.type == Type::Submit) {
            menus.send();
        }
        return true;
    }
    return false;
}

bool apply_model_picker(const PromptMenus& menus, const ModelPickerAction& action) {
    using Type = ModelPickerAction::Type;
    switch (action.type) {
    case Type::Pass:
        return false;
    case Type::Handled:
    case Type::Closed: // El filtro se queda en la caja, como con el menú de "/".
        return true;
    case Type::ClearInput:
        menus.input.set_text("");
        return true;
    case Type::Status:
        menus.flash = action.text;
        return true;
    case Type::Switch: {
        const ModelSwitch change = menus.session.switch_model(action.provider, action.text);
        if (menus.on_switch) {
            menus.on_switch(change);
        }
        if (const std::optional<std::string> notice = menus.model_picker.finish(change)) {
            menus.flash = *notice;
        }
        if (!menus.model_picker.is_open()) {
            menus.input.set_text("");
            menus.scroll.to_bottom();
        }
        return true;
    }
    }
    return false;
}

bool menus_key(const PromptMenus& menus, const ftxui::Event& event) {
    // Selector de /model abierto: ↑/↓, Tab, Enter y Esc son suyos (Enter
    // nunca envía la caja mientras está abierto).
    menus.model_picker.update(menus.input.text());
    if (menus.model_picker.is_open() && menus.input.Focused()) {
        const ListKey key = list_key(event);
        if ((key == ListKey::Up || key == ListKey::Down || key == ListKey::Tab ||
             key == ListKey::Enter || key == ListKey::Escape) &&
            apply_model_picker(menus, menus.model_picker.handle(key))) {
            return true;
        }
    }
    // Menú de comandos abierto: ↑/↓, Tab, Enter y Esc son primero suyos
    // (el primer Esc lo cierra; el segundo ya cancela la respuesta).
    menus.command_menu.update(menus.input.text(),
                              menus.input.Focused() && !menus.model_picker.is_open(), menus.mode);
    if (menus.command_menu.is_open()) {
        const ListKey key = list_key(event);
        if ((key == ListKey::Up || key == ListKey::Down || key == ListKey::Tab ||
             key == ListKey::Enter || key == ListKey::Escape) &&
            apply_command_menu(menus, menus.command_menu.handle(key))) {
            return true;
        }
    }
    return false;
}

bool menus_mouse(const PromptMenus& menus, const MenuBoxes& boxes, const ftxui::Mouse& mouse) {
    const bool click =
        mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed;
    // Selector de /model: hover mueve el cursor, clic elige (como Enter)
    // y la rueda mueve el cursor. Nada llega al historial.
    if (menus.model_picker.is_open() && boxes.model_picker.Contain(mouse.x, mouse.y)) {
        if (mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) {
            menus.model_picker.scroll(mouse.button == ftxui::Mouse::WheelUp ? -1 : 1);
            return true;
        }
        // Las cajas son del cuadro anterior: su first, no el de ahora.
        const std::size_t first = boxes.model_picker_first;
        for (std::size_t row = 0; row < boxes.model_picker_rows.size(); ++row) {
            if (!boxes.model_picker_rows[row].Contain(mouse.x, mouse.y)) {
                continue;
            }
            if (mouse.motion == ftxui::Mouse::Moved) {
                menus.model_picker.select(first + row);
            } else if (click) {
                (void)apply_model_picker(menus, menus.model_picker.choose(first + row));
            }
            break;
        }
        return true;
    }
    // Menú de comandos: hover mueve el cursor y clic completa (como Tab).
    // Nada del ratón sobre el menú llega al historial (tampoco la rueda).
    if (menus.command_menu.is_open() && boxes.command_menu.Contain(mouse.x, mouse.y)) {
        for (std::size_t row = 0; row < boxes.command_menu_rows.size(); ++row) {
            if (!boxes.command_menu_rows[row].Contain(mouse.x, mouse.y)) {
                continue;
            }
            if (mouse.motion == ftxui::Mouse::Moved) {
                menus.command_menu.select(row);
            } else if (click) {
                (void)apply_command_menu(menus, menus.command_menu.choose(row));
            }
        }
        return true;
    }
    return false;
}

} // namespace chatbot::cli
