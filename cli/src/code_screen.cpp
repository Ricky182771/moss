#include "code_screen.h"

#include "app_startup.h"
#include "appearance_startup.h"
#include "clipboard.h"
#include "code_command.h"
#include "input_edit.h"
#include "status_line.h"
#include "task_list_view.h"
#include "terminal_reply.h"

#include "chatbot/config.h"
#include "chatbot/curl_transport.h"
#include "chatbot/error.h"
#include "chatbot/markdown/markdown.h"
#include "chatbot/platform.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include <utility>

namespace chatbot::cli {

CodeScreen::CodeScreen(CodeScreenContext context)
    : mode_(context.mode), palette_(context.palette), size_(std::move(context.size)),
      app_(context.app),
      input_area_(make_text_area(
          {.palette = &context.palette,
           .min_rows = 1,
           .max_rows = kMaxInputLines,
           .enter = EnterKey::Submit,
           .on_enter =
               [this] {
                   send();
                   if (app_ != nullptr) {
                       app_->PostEvent(ftxui::Event::Custom);
                   }
               },
           .on_change = {},
           .placeholder =
               [this](int columns) {
                   return mode_.configured() ? std::string{input_placeholder(columns)}
                                             : mode_.setup_problem() + " (F2)";
               }})),
      settings_context_{.session = context.mode.session(),
                        .appearance = context.appearance,
                        .palette = context.palette,
                        .detected_symbols = context.detected_symbols,
                        // El proveedor y el modelo aplican desde el siguiente turno.
                        .on_saved =
                            [this] {
                                (void)mode_.config_changed();
                                scroll_.to_bottom();
                            },
                        .on_open = [this] { right_tab_ = 1; },
                        .on_close =
                            [this] {
                                right_tab_ = 0;
                                input_area_->TakeFocus();
                            }},
      settings_(make_settings_screen(context.models_loader, settings_context_)),
      screen_input_(settings_, *input_area_, flash_),
      key_log_(environment_value("CHAT_DEBUG_KEYS")) {
    if (app_ != nullptr) {
        control_.emplace(*app_);
    }
    history_.set_style(HistoryStyle::Code);
    history_.set_buttons_visible(false);
    bind_settings(settings_, settings_context_, std::move(context.post));

    // A la vista, el modo código o la configuración (F2).
    const ftxui::Component code = ftxui::Renderer(input_area_, [this] { return render(); });
    const ftxui::Component tabs =
        ftxui::Container::Tab({code, settings_.component()}, &right_tab_);
    const ftxui::Component layout = ftxui::Renderer(tabs, [this, tabs] {
        // Con un pegado abierto, cada cuadro cuenta como procesado y pide el siguiente
        // (como en el chat, main.cpp).
        if (screen_input_.bracketed_paste().active()) {
            screen_input_.bracketed_paste().frame();
            if (app_ != nullptr) {
                app_->RequestAnimationFrame();
            }
        }
        return tabs->Render() | palette_.base();
    });
    root_ = ftxui::CatchEvent(layout,
                              [this](ftxui::Event event) { return handle_event(std::move(event)); });
}

void CodeScreen::start(const std::string& warning) {
    flash_ = warning;
    if (!mode_.configured()) {
        // Como el chat: F2 abierto con el aviso (y el de la apariencia, si hay).
        const std::string notice =
            warning.empty() ? mode_.setup_problem() : mode_.setup_problem() + " " + warning;
        open_settings(notice);
        flash_ = notice; // Si no se pudo abrir, al menos el aviso.
    } else {
        input_area_->TakeFocus();
    }
}

void CodeScreen::finished(std::optional<std::string> restored) {
    if (restored.has_value() && input_area_->text().empty()) {
        input_area_->set_text(std::move(*restored)); // Reenviar es solo Enter.
    }
}

void CodeScreen::run() {
    if (control_.has_value()) {
        control_->run(root_);
    }
}

void CodeScreen::send() {
    // shortcut: /model, /clear y /ayuda todavía no existen aquí (un "/" se envía como texto);
    // llegan con PromptMenus{..., mode = CommandMode::Code} en la tarea siguiente.
    if (!mode_.configured()) {
        flash_ = mode_.setup_problem(); // El texto se queda en la caja.
        return;
    }
    if (!mode_.send(input_area_->text())) {
        return; // Turno en curso o caja vacía: el texto se queda.
    }
    input_area_->set_text("");
    scroll_.to_bottom();
}

void CodeScreen::open_settings(std::string notice) {
    // La Session de apoyo nunca está ocupada: quien tiene el turno es CodeMode.
    if (mode_.busy()) {
        flash_ = std::string{kWaitOrCancel};
        return;
    }
    if (const std::optional<std::string> problem =
            chatbot::cli::open_settings(settings_, settings_context_, std::move(notice))) {
        flash_ = *problem;
    }
}

ftxui::Element CodeScreen::render() {
    const ftxui::Dimensions size = size_();
    const int width = size.dimx;
    ftxui::Element body = scroll_.apply(history_.render(mode_.entries(), width, palette_));

    const CodeStatus status = mode_.status();
    StatusZones zones;
    zones.left = {status.mode, palette_.ink(&Theme::notice)};
    zones.activity = status.activity;
    zones.hint = std::string{kCancelHint};
    if (!flash_.empty()) {
        zones.center.push_back({flash_, ftxui::bold});
    }
    if (scroll_.has_more_below()) {
        // Con texto en la caja, End mueve el cursor: el aviso sugiere PgDn.
        zones.center.push_back({input_area_->text().empty() ? "↓ Hay más abajo (End)"
                                                            : "↓ Hay más abajo (PgDn)",
                                ftxui::bold, true});
    }
    zones.right = {md::sanitize(status.model), palette_.ink(&Theme::notice)};

    input_area_->set_max_rows(input_max_rows(size.dimy));
    return ftxui::vbox({
        title_bar(mode_.title(), width, palette_, ExportButton{.shown = false}, kCodeTitleHelp,
                  export_box_),
        std::move(body),
        ftxui::separator() | palette_.ink(&Theme::border),
        zoned_status_line(zones, width),
        render_task_list(mode_.tasks(), width, palette_),
        ftxui::hbox({ftxui::text("> ") | ftxui::bold | palette_.ink(&Theme::user_label),
                     input_area_->Render()}),
    });
}

bool CodeScreen::handle_mouse(ftxui::Event event) {
    if (settings_.is_open()) {
        return false;
    }
    const ftxui::Mouse& mouse = event.mouse();
    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        // La línea "●" de una herramienta o la del pensamiento (del último cuadro).
        if (const std::optional<ButtonHit> hit = history_.hit_test(mouse.x, mouse.y)) {
            if (hit->tool.has_value()) {
                (void)mode_.toggle_tool(*hit->tool);
            } else if (hit->reasoning.has_value()) {
                mode_.toggle_reasoning(*hit->reasoning);
            }
            return true;
        }
    }
    // La rueda: la caja o el historial (screen_input.h).
    return wheel_event(event, scroll_, *input_area_);
}

bool CodeScreen::handle_event(ftxui::Event event) {
    if (key_log_.enabled()) {
        screen_input_.log_event(key_log_, event);
    }
    if (control_.has_value()) {
        control_->resume();
    }
    // Una respuesta de la terminal que FTXUI no entendió no es una tecla.
    if (!event.is_character() && !event.is_mouse() && is_terminal_reply(event.input())) {
        return true;
    }
    // Lo que llega entre las marcas del pegado es texto, aunque sea Enter o Ctrl+C.
    if (screen_input_.feed_paste(event)) {
        return true;
    }
    if (event == ftxui::Event::CtrlC) {
        // Sale del loop; al destruirse, CodeMode cancela el turno y une su hilo.
        if (control_.has_value()) {
            control_->quit();
        }
        return true;
    }
    if (event == ftxui::Event::Custom) {
        return false; // Solo pide redibujar.
    }
    // Con una pregunta (s/n) de F2 abierta, todo evento es de ella (yes_no.h).
    if (settings_.is_open() && settings_.asking()) {
        (void)settings_.component()->OnEvent(event);
        return true;
    }
    if (event.is_mouse()) {
        return handle_mouse(std::move(event));
    }
    flash_.clear(); // El aviso dura hasta la siguiente tecla.
    if (control_.has_value() && control_->suspend(event)) {
        return false; // POSIX, Ctrl+Z: FTXUI suspende el proceso.
    }
    if (screen_input_.paste_shortcut(event, key_log_)) {
        return true; // Windows: Ctrl+V o Shift+Insert.
    }
    if (event == ftxui::Event::F2) {
        if (settings_.is_open()) {
            (void)settings_.request_close();
        } else {
            open_settings({});
        }
        return true;
    }
    if (event == ftxui::Event::CtrlT) {
        mode_.toggle_all_reasoning();
        return true;
    }
    if (event == ftxui::Event::CtrlO) {
        (void)mode_.toggle_last_tool(); // Sin herramientas con salida, nada.
        return true;
    }
    if (settings_.is_open()) {
        (void)settings_.component()->OnEvent(event);
        return true;
    }
    if (event == ftxui::Event::TabReverse) {
        return true; // Shift+Tab cambiará el modo de permiso (C3); en C2 no hay otro.
    }
    // "\" + Enter, Alt+Enter, flechas y Tab (screen_input.h).
    if (input_key(event, *input_area_)) {
        return true;
    }
    if (event == ftxui::Event::Escape) {
        mode_.cancel(); // Sin turno en curso no hace nada (no borra la caja).
        return true;
    }
    // PgUp/PgDn y, con la caja vacía, Home/End (screen_input.h).
    return scroll_key(event, scroll_, *input_area_);
}

int run_code_screen(const std::string& folder) {
    // Se destruye al último: CodeMode (su hilo usa post) y lo de F2 se destruyen antes. Construir
    // la App no toca la terminal; Loop sí.
    ftxui::App app = ftxui::App::Fullscreen();
    const RequestRunner::Post post = screen_post(app);
    CodeScreen* shown = nullptr; ///< La pantalla, para el OnFinished de CodeMode.
    OpenedCodeMode opened =
        open_code_mode(folder, post, [&shown](std::optional<std::string> restored) {
            if (shown != nullptr) {
                shown->finished(std::move(restored));
            }
        });
    // Por stderr, antes de abrir la pantalla completa (app_startup.h).
    if (opened.error.has_value()) {
        return report_startup_error(*opened.error);
    }
    CodeMode& mode = *opened.mode;
    // Sin terminal no hay F2 que abrir: que falte la key o el modelo es error, como en el chat.
    if (const std::optional<ChatError> incomplete = validate_config(mode.session().config());
        incomplete.has_value() && !stdio_is_terminal()) {
        return report_startup_error(*incomplete);
    }

    const StartupAppearance startup = load_startup_appearance();
    Appearance appearance = startup.appearance;
    Palette palette{appearance};
    ModelsLoader models_loader([] { return std::make_unique<CurlTransport>(); }, post);
    CodeScreen screen({.mode = mode,
                       .models_loader = models_loader,
                       .appearance = appearance,
                       .palette = palette,
                       .detected_symbols = startup.detected_symbols,
                       .post = post,
                       .app = &app,
                       .size = ftxui::Terminal::Size});
    shown = &screen;
    screen.start(startup.warning);
    screen.run();
    shown = nullptr;
    return 0;
}

} // namespace chatbot::cli
