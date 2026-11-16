// Lo que comparten las pantallas del chat y del modo código (screen_input.h):
// el pegado, las teclas de la caja y las del historial, sin terminal.

#include "screen_input.h"

#include "input_edit.h"

#include "fake_transport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace {

using chatbot::cli::Palette;

struct Harness {
    Palette palette{chatbot::cli::default_theme(), chatbot::cli::BackgroundMode::FromTheme,
                    chatbot::cli::Symbols::Unicode};
    chatbot::cli::ModelsLoader loader{
        [] { return std::make_unique<chatbot_test::FakeTransport>(); },
        [](std::function<void()>) {}};
    // Cerrada en todas las pruebas: el pegado va a la caja.
    chatbot::cli::SettingsScreen settings{
        loader,
        palette,
        [](const chatbot::cli::ProviderSettings&) -> std::optional<std::string> {
            return std::nullopt;
        },
        [](const chatbot::cli::Appearance&) -> std::optional<std::string> { return std::nullopt; },
        [](const std::optional<std::string>&) -> std::optional<std::string> {
            return std::nullopt;
        },
        [](const std::string&) -> std::optional<std::string> { return std::nullopt; },
        [] {}};
    int sent = 0; ///< Veces que la caja "envió" (on_enter).
    std::shared_ptr<chatbot::cli::TextArea> input = make_input();
    std::string flash;
    bool sidebar = false; ///< La barra de conversaciones tiene el foco.
    chatbot::cli::ScreenInput screen_input{settings, *input, flash, [this] { return sidebar; }};

    std::shared_ptr<chatbot::cli::TextArea> make_input() {
        chatbot::cli::TextAreaOption option;
        option.palette = &palette;
        option.max_rows = 8;
        option.enter = chatbot::cli::EnterKey::Submit;
        option.on_enter = [this] { ++sent; };
        std::shared_ptr<chatbot::cli::TextArea> area =
            chatbot::cli::make_text_area(std::move(option));
        area->TakeFocus();
        return area;
    }
};

} // namespace

TEST_CASE("ScreenInput: el pegado entra a la caja sin enviar", "[screen_input]") {
    Harness h;
    CHECK(h.screen_input.paste_target() == chatbot::cli::PasteTarget::MultiLine);
    CHECK(h.screen_input.deliver_paste("hola\nmundo", false));
    CHECK(h.input->text() == "hola\nmundo");

    // Un "\n" solo sería Enter para la caja: se inserta a mano.
    h.input->set_text("");
    CHECK(h.screen_input.deliver_paste("\n", false));
    CHECK(h.input->text() == "\n");
    CHECK(h.sent == 0);
    CHECK(h.flash.empty());

    // Recortado: el aviso va a la línea de estado.
    CHECK(h.screen_input.deliver_paste("x", true));
    CHECK(h.flash == chatbot::cli::kPasteTruncated);
}

TEST_CASE("ScreenInput: con la barra enfocada, el pegado se ignora", "[screen_input]") {
    Harness h;
    h.sidebar = true;
    CHECK(h.screen_input.paste_target() == chatbot::cli::PasteTarget::None);
    CHECK_FALSE(h.screen_input.deliver_paste("hola", false));
    CHECK(h.input->text().empty());
}

TEST_CASE("ScreenInput: el pegado entre corchetes es texto, también Enter", "[screen_input]") {
    Harness h;
    h.flash = "aviso";
    CHECK(h.screen_input.feed_paste(ftxui::Event::Special(std::string{chatbot::cli::kPasteStartMark})));
    CHECK(h.screen_input.bracketed_paste().active());
    CHECK(h.screen_input.feed_paste(ftxui::Event::Character("a")));
    CHECK(h.screen_input.feed_paste(ftxui::Event::Return));
    CHECK(h.screen_input.feed_paste(ftxui::Event::Character("b")));
    CHECK(h.flash == "aviso"); // Todavía no se entrega nada.
    CHECK(h.screen_input.feed_paste(ftxui::Event::Special(std::string{chatbot::cli::kPasteEndMark})));
    CHECK(h.input->text() == "a\nb");
    CHECK(h.sent == 0);
    CHECK(h.flash.empty()); // Como cualquier tecla.
    // Fuera de un pegado, nada se consume.
    CHECK_FALSE(h.screen_input.feed_paste(ftxui::Event::Return));
}

TEST_CASE("input_key: saltos, flechas y Tab son de la caja", "[screen_input]") {
    Harness h;
    h.input->set_text("abc\\");
    CHECK(chatbot::cli::input_key(ftxui::Event::Return, *h.input));
    CHECK(h.input->text() == "abc\n");
    CHECK(chatbot::cli::input_key(ftxui::Event::Special(std::string{chatbot::cli::kAltEnter}),
                                  *h.input));
    CHECK(h.input->text() == "abc\n\n");
    CHECK(chatbot::cli::input_key(ftxui::Event::ArrowLeft, *h.input));
    CHECK(chatbot::cli::input_key(ftxui::Event::Tab, *h.input));
    // Enter sin "\" y lo demás siguen su camino (Enter envía desde la caja).
    h.input->set_text("hola");
    CHECK_FALSE(chatbot::cli::input_key(ftxui::Event::Return, *h.input));
    CHECK_FALSE(chatbot::cli::input_key(ftxui::Event::Character("x"), *h.input));
    CHECK(h.input->text() == "hola");
    CHECK(h.sent == 0);
}

TEST_CASE("scroll_key: Home y End del historial solo con la caja vacía", "[screen_input]") {
    Harness h;
    chatbot::cli::Scroll scroll;
    CHECK(chatbot::cli::scroll_key(ftxui::Event::PageUp, scroll, *h.input));
    CHECK(chatbot::cli::scroll_key(ftxui::Event::PageDown, scroll, *h.input));
    CHECK(chatbot::cli::scroll_key(ftxui::Event::Home, scroll, *h.input));
    CHECK(chatbot::cli::scroll_key(ftxui::Event::End, scroll, *h.input));
    h.input->set_text("hola");
    CHECK_FALSE(chatbot::cli::scroll_key(ftxui::Event::Home, scroll, *h.input));
    CHECK_FALSE(chatbot::cli::scroll_key(ftxui::Event::End, scroll, *h.input));
    CHECK(chatbot::cli::scroll_key(ftxui::Event::PageUp, scroll, *h.input));
    CHECK_FALSE(chatbot::cli::scroll_key(ftxui::Event::ArrowUp, scroll, *h.input));
}
