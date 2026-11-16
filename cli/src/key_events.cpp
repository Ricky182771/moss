#include "key_events.h"

#include <ftxui/component/mouse.hpp>

#include <iterator>
#include <string_view>
#include <utility>

namespace chatbot::cli {

/// Traduce una tecla de FTXUI a la de la barra de conversaciones.
chatbot::cli::ListKey list_key(const ftxui::Event& event) {
    using chatbot::cli::ListKey;
    const std::pair<const ftxui::Event*, ListKey> keys[] = {
        {&ftxui::Event::ArrowUp, ListKey::Up},     {&ftxui::Event::ArrowDown, ListKey::Down},
        {&ftxui::Event::PageUp, ListKey::PageUp},  {&ftxui::Event::PageDown, ListKey::PageDown},
        {&ftxui::Event::Home, ListKey::Home},      {&ftxui::Event::End, ListKey::End},
        {&ftxui::Event::Return, ListKey::Enter},   {&ftxui::Event::Escape, ListKey::Escape},
        {&ftxui::Event::Delete, ListKey::Delete},  {&ftxui::Event::Tab, ListKey::Tab},
    };
    for (const auto& [ftxui_event, key] : keys) {
        if (event == *ftxui_event) {
            return key;
        }
    }
    return ListKey::Other;
}

/// Traduce un evento de FTXUI para el acumulador del pegado entre corchetes.
chatbot::cli::PasteKey paste_key(const ftxui::Event& event) {
    using chatbot::cli::PasteKey;
    // Event::Custom es Special("\0"): también un NUL pegado, que se descarta.
    if (event.is_mouse() || event == ftxui::Event::Custom) {
        return PasteKey::Passthrough;
    }
    if (event == ftxui::Event::Special(chatbot::cli::kPasteStartMark)) {
        return PasteKey::Start;
    }
    if (event == ftxui::Event::Special(chatbot::cli::kPasteEndMark)) {
        return PasteKey::End;
    }
    if (event.is_character()) {
        return PasteKey::Character;
    }
    if (event == ftxui::Event::Return) {
        return PasteKey::Return;
    }
    if (event == ftxui::Event::Tab) {
        return PasteKey::Tab;
    }
    return PasteKey::Other;
}

/// Traduce un evento de FTXUI para el registro de teclas (key_log.h). La
/// vista apunta a event: debe vivir mientras se use.
chatbot::cli::KeyLogEvent key_log_event(ftxui::Event& event) { // mouse() no es const.
    using Kind = chatbot::cli::KeyLogEvent::Kind;
    chatbot::cli::KeyLogEvent out;
    if (event == ftxui::Event::Custom) {
        out.kind = Kind::Custom;
    } else if (event.is_mouse()) {
        static constexpr std::string_view kButtons[] = {
            "left", "middle", "right", "none", "wheel-up", "wheel-down", "wheel-left", "wheel-right"};
        static constexpr std::string_view kMotions[] = {"released", "pressed", "moved"};
        const ftxui::Mouse& mouse = event.mouse();
        out.kind = Kind::Mouse;
        out.button = mouse.button < std::size(kButtons) ? kButtons[mouse.button] : "?";
        out.motion = mouse.motion < std::size(kMotions) ? kMotions[mouse.motion] : "?";
        out.x = mouse.x;
        out.y = mouse.y;
    } else {
        out.kind = event.is_character() ? Kind::Character : Kind::Special;
        out.input = event.input();
    }
    return out;
}

} // namespace chatbot::cli
