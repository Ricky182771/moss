#include "screen_input.h"

#include "clipboard.h"
#include "input_edit.h"
#include "key_events.h"

#include "chatbot/platform.h"

#include <ftxui/component/mouse.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <utility>

namespace chatbot::cli {

bool is_paste_shortcut(const ftxui::Event& event) {
    return event == ftxui::Event::CtrlV || event == ftxui::Event::Special(kShiftInsert);
}

RequestRunner::Post screen_post(ftxui::App& screen) {
    return [&screen](std::function<void()> task) {
        screen.Post(std::move(task));
        // Post de una tarea no redibuja por sí solo; un evento sí.
        screen.PostEvent(ftxui::Event::Custom);
    };
}

void ScreenControl::quit() {
    (void)write_to_terminal(kBracketedPasteOff);
    screen_.Exit();
}

void ScreenControl::resume() {
    if (paste_mode_lost_) {
        // De vuelta de Ctrl+Z (fg): el shell pudo haberlo desactivado.
        (void)write_to_terminal(kBracketedPasteOn);
        paste_mode_lost_ = false;
    }
}

bool ScreenControl::suspend(const ftxui::Event& event) {
    if (event == ftxui::Event::CtrlZ && current_os() == Os::Posix) {
        // FTXUI suspende el proceso (SIGTSTP): el shell no debe recibir
        // las marcas. Al volver se activa otra vez (resume).
        (void)write_to_terminal(kBracketedPasteOff);
        paste_mode_lost_ = true;
        return true;
    }
    return false;
}

void ScreenControl::run(const ftxui::Component& root) {
    // Ctrl+C se maneja en el CatchEvent (quit): con el manejo por defecto,
    // FTXUI restaura la terminal y relanza SIGINT dentro de Loop(), y el
    // proceso muere sin destruir el runner (que cancela y hace join). Así,
    // Loop() regresa.
    screen_.ForceHandleCtrlC(false);
    screen_.Post([] { (void)write_to_terminal(kBracketedPasteOn); });
    screen_.Loop(root);
}

ScreenInput::ScreenInput(SettingsScreen& settings, TextArea& input, std::string& flash,
                         std::function<bool()> sidebar_focused)
    : settings_(settings), input_(input), flash_(flash),
      sidebar_focused_(std::move(sidebar_focused)) {}

PasteTarget ScreenInput::paste_target() const {
    return chatbot::cli::paste_target(sidebar_focused(), settings_.is_open(),
                                      settings_.is_open() ? settings_.paste_target()
                                                          : PasteTarget::None);
}

void ScreenInput::notify(std::string_view notice) {
    if (settings_.is_open()) {
        settings_.show_notice(std::string{notice});
    } else {
        flash_ = std::string{notice};
    }
}

bool ScreenInput::deliver_paste(std::string_view raw, bool truncated) {
    const std::optional<SanitizedPaste> paste = prepare_paste(raw, paste_target());
    if (!paste.has_value()) {
        return false;
    }
    const ftxui::Event event = ftxui::Event::Character(paste->text);
    if (settings_.is_open()) {
        (void)settings_.component()->OnEvent(event);
    } else if (event == ftxui::Event::Return) {
        // Event compara solo el texto: un "\n" pegado solo sería Enter
        // para la caja y enviaría el mensaje.
        input_.insert(paste->text);
    } else {
        (void)input_.OnEvent(event);
    }
    if (truncated || paste->truncated) {
        notify(kPasteTruncated);
    }
    return true;
}

bool ScreenInput::feed_paste(const ftxui::Event& event) {
    // Sin 201~ y tras más de kPasteIdleLimit con cuadros y sin entrada, el
    // pegado se entrega y este evento sigue normal (así Ctrl+C siempre puede
    // salir).
    const BracketedPaste::Step pasted = bracketed_paste_.feed(
        paste_key(event), event.is_character() ? event.character() : std::string{});
    if (pasted.text.has_value()) {
        flash_.clear(); // Como cualquier tecla.
        deliver_paste(*pasted.text, pasted.truncated);
    }
    return pasted.consumed;
}

bool ScreenInput::paste_shortcut(const ftxui::Event& event, KeyLog& key_log) {
    if (current_os() != Os::Windows || !is_paste_shortcut(event)) {
        return false;
    }
    const PasteTarget target = paste_target();
    if (target == PasteTarget::None) {
        // Una lista, un botón o una pregunta: aviso. La barra lateral: nada.
        if (const auto notice = paste_shortcut_notice(sidebar_focused(), target)) {
            notify(*notice);
        }
        key_log.write(key_log_time(std::chrono::system_clock::now()) +
                      " paste-shortcut ignored target=none");
        return true;
    }
    const std::optional<std::string> text = read_native_clipboard();
    const bool delivered = text.has_value() && deliver_paste(*text, false);
    if (!text.has_value()) {
        notify(kClipboardNoText);
    }
    key_log.write(format_paste_shortcut(
        key_log_time(std::chrono::system_clock::now()),
        text.has_value() ? std::optional<std::size_t>{text->size()} : std::nullopt, delivered));
    return true;
}

void ScreenInput::log_event(KeyLog& key_log, ftxui::Event& event) {
    KeyLogState state;
    state.settings_open = settings_.is_open();
    state.sidebar_focused = sidebar_focused();
    state.paste_target = paste_target();
    state.bracketed_paste_open = bracketed_paste_.active();
    if (settings_.is_open()) {
        state.focused_fields = settings_.debug_focus();
    }
    key_log.write(
        format_key_event(key_log_time(std::chrono::system_clock::now()), key_log_event(event), state));
}

bool input_key(const ftxui::Event& event, TextArea& input) {
    // "\" + Enter: salto de línea en lugar de enviar.
    if (event == ftxui::Event::Return && input.backslash_newline()) {
        return true;
    }
    // Alt+Enter (donde llega: en Windows es pantalla completa).
    if (event == ftxui::Event::Special(kAltEnter) || event == ftxui::Event::Special(kAltEnterLf)) {
        input.insert("\n");
        return true;
    }
    // ←/→, ↑/↓ y Tab son de la caja: si llegaran al contenedor del
    // split, moverían el foco a la barra.
    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight ||
        event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown ||
        event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
        input.OnEvent(event);
        return true;
    }
    return false;
}

bool scroll_key(const ftxui::Event& event, Scroll& scroll, const TextArea& input) {
    if (event == ftxui::Event::PageUp) {
        scroll.page_up();
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        scroll.page_down();
        return true;
    }
    // Home/End: con texto en la caja mueven el cursor (los maneja la caja);
    // con la caja vacía mueven el historial.
    if (event == ftxui::Event::Home && input.text().empty()) {
        scroll.to_top();
        return true;
    }
    if (event == ftxui::Event::End && input.text().empty()) {
        scroll.to_bottom();
        return true;
    }
    return false;
}

bool wheel_event(ftxui::Event& event, Scroll& scroll, TextArea& input) {
    const ftxui::Mouse& mouse = event.mouse();
    // La rueda sobre la caja la desplaza si su texto no cabe; si no,
    // mueve el historial.
    if ((mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) &&
        input.scrollable() && input.contains(mouse.x, mouse.y)) {
        (void)input.OnEvent(event);
        return true;
    }
    if (mouse.button == ftxui::Mouse::WheelUp) {
        scroll.by(-kWheelStep);
        return true;
    }
    if (mouse.button == ftxui::Mouse::WheelDown) {
        scroll.by(kWheelStep);
        return true;
    }
    return false;
}

} // namespace chatbot::cli
