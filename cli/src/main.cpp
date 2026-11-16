// main.cpp — interfaz de terminal de Moss.
//
// Pantalla completa con FTXUI: historial arriba, caja de entrada abajo. La
// lógica sin FTXUI está en Session (session.h) y la petición corre en el
// hilo de trabajo de RequestRunner; todo lo que toca la pantalla o la
// sesión se ejecuta en el hilo de la interfaz mediante App::Post.

#include "addon_launcher.h"
#include "addons_panel.h"
#include "appearance_startup.h"
#include "app_startup.h"
#include "chatbot/child_process.h"
#include "block_actions.h"
#include "clipboard.h"
#include "code_command.h"
#include "command_menu.h"
#include "command_menu_view.h"
#include "model_picker.h"
#include "model_picker_view.h"
#include "commands.h"
#include "conversation.h"
#include "conversation_list.h"
#include "downloads.h"
#include "glyphs.h"
#include "history_view.h"
#include "input_edit.h"
#include "input_style.h"
#include "key_events.h"
#include "key_log.h"
#include "models_loader.h"
#include "paste.h"
#include "prompt_menus.h"
#include "provider_settings.h"
#include "screen_input.h"
#include "scroll.h"
#include "search_settings.h"
#include "session.h"
#include "settings_binding.h"
#include "settings_screen.h"
#include "sidebar.h"
#include "sidebar_view.h"
#include "status_line.h"
#include "terminal_reply.h"
#include "text_area.h"
#include "theme.h"
#include "title_bar.h"

#include "chatbot/config.h"
#include "chatbot/curl_transport.h"
#include "chatbot/error.h"
#include "chatbot/markdown/markdown.h"
#include "chatbot/platform.h"
#include "chatbot/result.h"
#include "chatbot/types.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Los traductores de teclas (key_events.h) se llaman sin calificar en el loop.
using chatbot::cli::list_key;

/// Ayuda de teclas de la barra de título del chat: larga y, si no cabe, corta.
constexpr chatbot::cli::TitleHelp kChatTitleHelp{"Ctrl+B barra · Ctrl+O conversaciones",
                                                 "Ctrl+B · Ctrl+O"};

} // namespace

int main(int argc, char** argv) {
    // Windows: la consola en UTF-8 mientras corre el programa, y al salir
    // (también por un error) vuelve a la página de códigos que tenía. Se
    // declara primero para que se destruya al último, después de FTXUI.
    const chatbot::ConsoleUtf8Scope console_utf8;
    // moss --help y moss <addon> [argumentos] (este se reemplaza por
    // el addon): antes de tocar la consola, la configuración o la pantalla.
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (const std::optional<int> code = chatbot::cli::run_command_line(args)) {
        return *code;
    }
    // Windows: sin ENABLE_PROCESSED_INPUT, Ctrl+C llega como tecla y sale por
    // screen.Exit() (ver CatchEvent), en lugar de matar el proceso con
    // SIGINT dentro de Loop(). Se construye antes de Loop() y se destruye
    // después de que el runner cancela y hace join, así que Ctrl+C durante
    // ese join tampoco mata el proceso.
    const chatbot::ConsoleInputScope console_input;
    // moss code [carpeta] sin -p: la pantalla de Moss Code (code_command.h).
    if (const chatbot::cli::CommandLine line = chatbot::cli::parse_command_line(args);
        line.kind == chatbot::cli::CommandLine::Kind::Code) {
        return chatbot::cli::run_code_screen(line.folder);
    }

    // Modo portable y configuración (app_startup.h), antes de tocar la
    // terminal: si fallan, el error sale por stderr sin abrir la pantalla
    // completa (si no se puede escribir en datos/, nunca se usa en silencio
    // la carpeta del usuario). Si lo único que falta es la key o el modelo,
    // la app arranca con la pantalla de configuración abierta; sin terminal
    // (stdin o stdout redirigidos) no hay pantalla que abrir y también sale
    // con el error.
    const chatbot::Result<chatbot::Config> initial = chatbot::cli::load_startup_config();
    const std::optional<chatbot::ChatError> incomplete =
        initial.is_ok() ? chatbot::validate_config(initial.value()) : std::nullopt;
    const bool has_terminal = chatbot::stdio_is_terminal();
    if (initial.is_error() || (incomplete.has_value() && !has_terminal)) {
        // Por stderr, antes de abrir la pantalla completa (app_startup.h).
        return chatbot::cli::report_startup_error(initial.is_error() ? initial.error()
                                                                     : *incomplete);
    }

    // Tema, fondo y símbolos de config.json ("appearance"), con su aviso
    // (appearance_startup.h).
    chatbot::cli::StartupAppearance startup_appearance = chatbot::cli::load_startup_appearance();
    const chatbot::cli::Symbols detected_symbols = startup_appearance.detected_symbols;
    const std::string appearance_warning = std::move(startup_appearance.warning);
    chatbot::cli::Appearance appearance = startup_appearance.appearance;
    /// Con lo que se dibuja todo; cambia al guardar la apariencia.
    chatbot::cli::Palette palette{appearance};

    // El orden de declaración importa: se destruyen en orden inverso. La
    // sesión se destruye antes que screen: su runner cancela y hace join del
    // hilo, que usa post; igual el cargador de modelos, que une sus hilos. Lo
    // que un hilo encole durante el join apunta a la sesión, pero nunca se
    // ejecuta: después de Loop() FTXUI no corre tareas (App::Internal::PostMain
    // → App::Internal::Uninstall puede correr algunas todavía dentro de Loop(),
    // con todo vivo), y App::~App y TaskRunner::~TaskRunner solo descartan la cola.
    chatbot::cli::Scroll scroll;
    chatbot::cli::HistoryView history;
    chatbot::cli::Sidebar sidebar;
    // Barra lateral: visible al arrancar si la terminal es ancha. Si el ancho
    // la ocultó, el ancho la vuelve a mostrar; si la ocultó Ctrl+B, no.
    bool last_wide = ftxui::Terminal::Size().dimx >= chatbot::cli::kSidebarMinTerminal;
    bool sidebar_visible = last_wide;
    bool hidden_by_width = !last_wide;
    // Ancho para ResizableSplit (main_size): la barra sin su borde derecho,
    // que es el divisor. sidebar_size guarda el ancho mientras está oculta.
    int sidebar_size = chatbot::cli::kSidebarWidth - 1;
    int split_size = sidebar_visible ? sidebar_size : 0;
    int split_min = chatbot::cli::kSidebarMinWidth - 1;
    int split_max = chatbot::cli::kSidebarMaxWidth - 1;
    ftxui::Box sidebar_box; ///< Zona de la barra en el último cuadro (para el ratón).
    ftxui::Box export_box;  ///< [Exportar] de la barra de título en el último cuadro.
    // Menú de comandos al escribir "/" en la caja, y sus filas en el último
    // cuadro (para el ratón).
    chatbot::cli::CommandMenu command_menu;
    // También las del selector de /model.
    chatbot::cli::MenuBoxes menu_boxes;
    bool export_hover = false;
    bool dragging = false;  ///< Se arrastra el divisor: el ratón es de ResizableSplit.
    int right_tab = 0; ///< Lado derecho: 0 = conversación, 1 = configuración.
    auto screen = ftxui::App::Fullscreen();
    // screen.Post y un evento para redibujar (screen_input.h).
    const chatbot::cli::RequestRunner::Post post = chatbot::cli::screen_post(screen);
    // El loop, Ctrl+C, Ctrl+Z y el pegado entre corchetes de la terminal.
    chatbot::cli::ScreenControl control(screen);

    // La caja de entrada; se crea después de la sesión, que su placeholder consulta.
    std::shared_ptr<chatbot::cli::TextArea> input_area;

    // La lógica sin FTXUI (session.h), con el sistema real (app_startup.h):
    // configuración vigente, cliente y runner, búsqueda, la conversación
    // abierta y su almacén. Sin configuración completa no hay cliente ni
    // runner (la caja no envía).
    chatbot::cli::SessionEnvironment session_environment = chatbot::cli::app_session_environment();
    /// Abrevia rutas con "~" (en Windows, vacía: las rutas van completas).
    const std::string home = session_environment.home;
    chatbot::cli::Session session(
        std::move(session_environment), initial.value(), post,
        [&](chatbot::cli::Session::Finished finished) {
            if (finished.saved) {
                // Se guardó el par: la conversación sube a "Hoy" en la barra.
                sidebar.refresh(session.store().list(), session.conversation().id());
            } else if (finished.restored.has_value() && input_area->text().empty()) {
                // Regresa el texto a la caja si está vacía: reenviar es solo Enter.
                input_area->set_text(*finished.restored);
            }
        });
    chatbot::cli::ModelsLoader models_loader(
        [] { return std::make_unique<chatbot::CurlTransport>(); }, post);
    // Selector de /model, con su propio ModelsLoader: no cancela la lista de
    // F2. Se destruye antes que session (cancela y une su hilo).
    chatbot::cli::ModelPicker model_picker(
        [&session](std::string_view provider) { return session.models_config(provider); },
        [] { return std::make_unique<chatbot::CurlTransport>(); }, post);

    // Avisos de arranque: tema, instrucciones del sistema y búsqueda.
    std::string startup_warning = appearance_warning;
    if (!session.startup_warning().empty()) {
        startup_warning += (startup_warning.empty() ? "" : " ") + session.startup_warning();
    }
    std::string flash = startup_warning; ///< Aviso de la línea de estado hasta la siguiente tecla.
    sidebar.open(session.store().list(), session.conversation().id());

    // La caja de entrada (text_area.h): el texto, el cursor y la vista son
    // suyos. Enter envía (on_enter, abajo); "\" + Enter y Alt+Enter hacen el
    // salto (root). Sin configuración, el placeholder lo dice.
    input_area = chatbot::cli::make_text_area(
        {.palette = &palette, .min_rows = 1, .max_rows = chatbot::cli::kMaxInputLines,
         .enter = chatbot::cli::EnterKey::Submit, .on_enter = {}, .on_change = {},
         .placeholder = [&session](int columns) {
             return session.configured()
                        ? std::string{chatbot::cli::input_placeholder(columns)}
                        : session.setup_problem() + " (F2)";
         }});
    const ftxui::Component input = input_area;

    // Carpeta de descargas (sin moss/) para [Exportar], o nullopt con el
    // motivo en la línea de estado.
    const auto download_base = [&]() -> std::optional<std::string> {
        std::optional<std::string> base =
            chatbot::cli::default_download_dir(chatbot::cli::environment_value);
        if (!base.has_value()) {
            flash = std::string{chatbot::cli::no_download_dir_message(chatbot::current_os())};
        }
        return base;
    };

    // Envía el contenido de la caja (o corre el comando). Solo se llama
    // desde el hilo de la interfaz.
    const auto send = [&] {
        using Type = chatbot::cli::SubmitOutcome::Type;
        const chatbot::cli::SubmitOutcome outcome = session.submit(input_area->text());
        if (outcome.type == Type::Ignored) {
            return;
        }
        if (outcome.type == Type::Status) {
            flash = outcome.status; // El texto se queda en la caja.
            return;
        }
        if (outcome.type == Type::OpenModelPicker) {
            const auto choices = session.provider_choices();
            if (choices.is_error()) {
                flash = choices.error().message;
                return;
            }
            const chatbot::cli::ModelPickerAction opened =
                model_picker.open(choices.value(), outcome.picker_provider, session.model());
            if (opened.type == chatbot::cli::ModelPickerAction::Type::Status) {
                flash = opened.text;
                return;
            }
        }
        if (outcome.type == Type::Copy) {
            session.report(chatbot::cli::copy_block(outcome.block, chatbot::cli::real_clipboard()));
        }
        if (outcome.clear) {
            input_area->set_text("");
        }
        scroll.to_bottom();
    };

    // La barra con lo que hay en el almacén y la conversación abierta.
    const auto refresh_sidebar = [&] {
        sidebar.refresh(session.store().list(), session.conversation().id());
    };

    // Campo de renombrar de la barra: edita Sidebar::rename_text(). Enter y
    // Esc los decide Sidebar::handle; las demás teclas van a este Input, que
    // no está en el árbol de componentes (sin padre, siempre "enfocado").
    int rename_cursor = 0;
    std::string rename_shown; ///< Id del campo dibujado: al cambiar, el cursor va al final.
    ftxui::InputOption rename_option;
    rename_option.content = &sidebar.rename_text();
    rename_option.multiline = false;
    rename_option.cursor_position = &rename_cursor;
    rename_option.transform = [&palette](ftxui::InputState state) {
        return chatbot::cli::input_transform(std::move(state.element), state.hovered,
                                             state.focused, state.is_placeholder, palette);
    };
    const ftxui::Component rename_input = ftxui::Input(rename_option);

    input_area->set_on_enter([&] {
        send();
        screen.PostEvent(ftxui::Event::Custom);
    });

    // El menú de "/" y el selector de /model sobre la caja (prompt_menus.h).
    const chatbot::cli::PromptMenus menus{.command_menu = command_menu,
                                          .model_picker = model_picker,
                                          .session = session,
                                          .input = *input_area,
                                          .scroll = scroll,
                                          .flash = flash,
                                          .send = send,
                                          .mode = chatbot::cli::CommandMode::Chat,
                                          .on_switch = {}};

    // Pantalla de configuración (settings_binding.h): guarda por la sesión.
    // Al cerrarse, vuelve la conversación.
    const chatbot::cli::SettingsContext settings_context{
        .session = session,
        .appearance = appearance,
        .palette = palette,
        .detected_symbols = detected_symbols,
        .on_saved = [&] { scroll.to_bottom(); },
        .on_open = [&] { right_tab = 1; },
        .on_close =
            [&] {
                right_tab = 0;
                input->TakeFocus();
            }};
    chatbot::cli::SettingsScreen settings =
        chatbot::cli::make_settings_screen(models_loader, settings_context);
    chatbot::cli::bind_settings(settings, settings_context, post);

    // El foco del lado derecho: la configuración si está abierta; si no, la caja.
    const auto focus_main = [&] {
        if (settings.is_open()) {
            settings.focus();
        } else {
            input->TakeFocus();
        }
    };

    // Abre la configuración (F2 o "⚙ Configuración"), con un aviso opcional.
    const auto open_settings = [&](std::string notice) {
        if (const std::optional<std::string> problem =
                chatbot::cli::open_settings(settings, settings_context, std::move(notice))) {
            flash = *problem;
        }
    };

    // Ejecuta lo que pidió la barra de conversaciones.
    const auto apply_sidebar_action = [&](const chatbot::cli::ListAction& action) {
        using Type = chatbot::cli::ListAction::Type;
        if (action.type == Type::None) {
            return;
        }
        if (action.type == Type::Close) {
            focus_main();
            return;
        }
        if (action.type == Type::Cancelled) {
            flash = std::string{chatbot::cli::ConversationList::kDeleteCancelled};
            return;
        }
        if (action.type == Type::Settings) {
            open_settings({});
            return;
        }
        // Exportar y renombrar no tocan la conversación en curso.
        if (action.type == Type::Export) {
            flash = session.export_stored(action.id).result.message;
            return;
        }
        if (action.type == Type::Rename) {
            const chatbot::cli::RenameOutcome renamed =
                session.rename_conversation(action.id, action.text);
            if (renamed.status != chatbot::cli::RenameOutcome::Status::Renamed) {
                flash = renamed.message; // El campo sigue abierto.
                return;
            }
            sidebar.stop_rename();
            refresh_sidebar();
            return;
        }
        if (session.busy()) {
            // Abrir, borrar o crear cambiaría la conversación en curso.
            flash = std::string{chatbot::cli::kWaitOrCancel};
            return;
        }
        if ((action.type == Type::New || action.type == Type::Open) && settings.is_open() &&
            !settings.request_close()) {
            // Cambios sin guardar: la configuración pregunta antes de cerrarse.
            settings.focus();
            return;
        }
        switch (action.type) {
        case Type::None:
        case Type::Close:
        case Type::Settings:
        case Type::Cancelled:
        case Type::Export:
        case Type::Rename:
            break;
        case Type::New:
        case Type::Open:
            if (const std::optional<std::string> error = action.type == Type::New
                                                             ? session.new_conversation()
                                                             : session.open(action.id)) {
                flash = *error;
                break;
            }
            scroll.to_bottom();
            refresh_sidebar();
            input->TakeFocus();
            break;
        case Type::Delete: {
            const bool was_open = action.id == session.conversation().id();
            if (const std::optional<std::string> error = session.remove(action.id)) {
                flash = *error;
            } else if (was_open) {
                scroll.to_bottom(); // Se borró la abierta: queda una nueva.
            }
            refresh_sidebar();
            break;
        }
        }
    };

    // Día de hoy en hora local, para los grupos por fecha.
    const auto today = [] {
        return chatbot::cli::local_day(std::time(nullptr)).value_or(chatbot::cli::CalendarDay{});
    };
    // Filas de la lista que caben en la barra (la terminal menos los bordes).
    const auto sidebar_rows_visible = [] {
        return static_cast<std::size_t>(
            chatbot::cli::sidebar_view_height(ftxui::Terminal::Size().dimy));
    };

    // La barra lateral. Es enfocable (TakeFocus) para que la caja de entrada
    // pierda el cursor mientras se navega la lista; el ratón sobre ella lo
    // maneja root, que no le pasa esos eventos (si no, tomaría el foco con
    // solo pasar el puntero).
    const ftxui::Component sidebar_panel = ftxui::Renderer([&](bool focused) {
        if (!focused || !sidebar_visible) {
            // El menú y el campo de renombrar se cierran al perder el foco.
            sidebar.close_menu();
            sidebar.stop_rename();
        }
        if (sidebar.renaming() != rename_shown) {
            rename_shown = sidebar.renaming();
            rename_cursor = static_cast<int>(sidebar.rename_text().size());
        }
        if (!sidebar_visible) {
            sidebar_box = ftxui::Box{};
            return ftxui::emptyElement();
        }
        const std::vector<chatbot::cli::SidebarRow> rows = sidebar.rows(today());
        sidebar.fit(rows, sidebar_rows_visible());
        return chatbot::cli::render_sidebar(
                   sidebar, rows, split_size, ftxui::Terminal::Size().dimy, focused, palette,
                   sidebar.renaming().empty() ? nullptr : rename_input->Render()) |
               ftxui::reflect(sidebar_box);
    });

    const ftxui::Component chat = ftxui::Renderer(input, [&] {
        // El historial ocupa el ancho que deja la barra (más su divisor y un
        // espacio de separación).
        const int width = ftxui::Terminal::Size().dimx - (sidebar_visible ? split_size + 2 : 0);
        const chatbot::cli::Conversation& conversation = session.conversation();
        ftxui::Element body = scroll.apply(history.render(
            conversation.entries(), width, palette, session.code_blocks().first_numbers()));
        const ftxui::Decorator notice = palette.ink(&chatbot::cli::Theme::notice);

        // Los avisos usan lo que dejan los indicadores (reserved) y se
        // recortan con "…" (status_line.h).
        std::vector<chatbot::cli::StatusItem> status;
        if (sidebar_visible && sidebar_panel->Focused()) {
            if (const std::optional<std::string> question = sidebar.confirmation()) {
                status.push_back({*question, ftxui::bold});
            } else if (flash.empty() && !sidebar.renaming().empty()) {
                status.push_back({"Enter guarda · Esc cancela", notice});
            } else if (flash.empty() && sidebar.menu_open()) {
                status.push_back({"Enter elige · Esc cierra el menú", notice});
            } else if (flash.empty() && !session.busy()) {
                // La ayuda solo si no hay otro aviso: juntos no caben.
                const bool on_conversation = !sidebar.new_selected() &&
                                             !sidebar.settings_selected() &&
                                             sidebar.list().can_open(sidebar.list().selected());
                status.push_back({on_conversation ? "Enter abre · r renombrar · e exportar · "
                                                    "Supr borrar · Esc vuelve a la caja"
                                                  : "Enter abre · Supr borra · Esc vuelve a la caja",
                                  notice});
            }
        }
        if (!flash.empty()) {
            status.push_back({flash, ftxui::bold});
        }
        if (session.busy()) {
            status.push_back({session.searching() ? "Buscando en la web… (Esc para cancelar)"
                                        : "Pensando… (Esc para cancelar)",
                              ftxui::nothing, true});
        }
        if (scroll.has_more_below()) {
            // Con texto en la caja, End mueve el cursor: el aviso sugiere PgDn.
            status.push_back({input_area->text().empty() ? "↓ Hay más abajo (End)"
                                                 : "↓ Hay más abajo (PgDn)",
                              ftxui::bold, true});
        }
        if (session.last_dropped() > 0) {
            status.push_back({"Se omitieron " + std::to_string(session.last_dropped()) +
                                  " mensajes antiguos para no exceder el límite.",
                              notice});
        }

        // Menú de comandos: entre la línea de estado y la caja; empuja el
        // historial hacia arriba.
        // El selector de /model va en el mismo lugar; mientras está abierto
        // lo escrito es su filtro y el menú de "/" no se abre.
        if (settings.is_open()) {
            model_picker.close();
        }
        model_picker.update(input_area->text());
        if (model_picker.is_open() && !model_picker.models_error().empty()) {
            status.push_back({chatbot::md::sanitize(model_picker.models_error()), notice});
        }
        command_menu.update(input_area->text(), input->Focused() && !model_picker.is_open(),
                            chatbot::cli::CommandMode::Chat);
        ftxui::Element menu = ftxui::emptyElement();
        if (model_picker.is_open()) {
            menu_boxes.model_picker_first = model_picker.first();
            menu = chatbot::cli::render_model_picker(model_picker, width, palette,
                                                     &menu_boxes.model_picker_rows) |
                   ftxui::reflect(menu_boxes.model_picker);
        } else {
            menu_boxes.model_picker_rows.clear();
            menu_boxes.model_picker = ftxui::Box{};
        }
        if (command_menu.is_open()) {
            menu = chatbot::cli::render_command_menu(
                       command_menu, chatbot::cli::command_availability(session.command_context()),
                       width, palette, &menu_boxes.command_menu_rows) |
                   ftxui::reflect(menu_boxes.command_menu);
        } else {
            menu_boxes.command_menu_rows.clear();
            menu_boxes.command_menu = ftxui::Box{};
        }
        const bool hide_status =
            (command_menu.is_open() || model_picker.is_open()) && status.empty();
        input_area->set_max_rows(chatbot::cli::input_max_rows(ftxui::Terminal::Size().dimy));
        const std::string title =
            "Moss — " +
            (session.model().empty() ? std::string{"sin configurar"} : session.model()) + " — " +
            (conversation.title().empty() ? std::string{"Nueva conversación"}
                                          : chatbot::md::sanitize(conversation.title()));
        ftxui::Element content = ftxui::vbox({
            chatbot::cli::title_bar(title, width, palette,
                                    {conversation.has_turns(), export_hover}, kChatTitleHelp,
                                    export_box),
            std::move(body),
            // Con el menú abierto y sin avisos, el renglón de estado y su
            // línea se ocultan: la línea del menú lo separa del historial.
            hide_status ? ftxui::emptyElement()
                        : ftxui::separator() | palette.ink(&chatbot::cli::Theme::border),
            hide_status ? ftxui::emptyElement() : chatbot::cli::status_line(status, width),
            std::move(menu),
            // El prompt en negritas marca la caja sin un bloque de color. La
            // caja mide su alto ella misma: crece con sus filas en pantalla
            // hasta su tope y después se desplaza con su barra.
            ftxui::hbox({ftxui::text("> ") | ftxui::bold |
                             palette.ink(&chatbot::cli::Theme::user_label),
                         input->Render()}),
        });
        if (sidebar_visible) {
            // Un espacio entre el borde de la barra y la conversación.
            content = ftxui::hbox({ftxui::text(" "), std::move(content) | ftxui::flex});
        }
        return content;
    });

    // Barra a la izquierda, conversación a la derecha. El divisor es el borde
    // derecho de la barra y se arrastra con el ratón.
    ftxui::ResizableSplitOption split_option;
    split_option.main = sidebar_panel;
    // A la derecha, la conversación o la configuración.
    const ftxui::Component settings_panel = ftxui::Renderer(settings.component(), [&] {
        ftxui::Element element = settings.component()->Render();
        // Un espacio entre el borde de la barra y la ventana, como en la conversación.
        return sidebar_visible ? ftxui::hbox({ftxui::text(" "), std::move(element) | ftxui::flex})
                               : element;
    });
    split_option.back = ftxui::Container::Tab({chat, settings_panel}, &right_tab);
    split_option.direction = ftxui::Direction::Left;
    split_option.main_size = &split_size;
    split_option.separator_func = [&] {
        return sidebar_visible ? chatbot::cli::sidebar_divider(palette) : ftxui::emptyElement();
    };
    split_option.min = &split_min;
    split_option.max = &split_max;
    const ftxui::Component split = ftxui::ResizableSplit(split_option);

    // Oculta la barra; si tenía el foco, pasa al lado derecho.
    const auto hide_sidebar = [&] {
        if (sidebar_panel->Focused()) {
            focus_main();
        }
        sidebar_visible = false;
        dragging = false;
    };

    // El pegado (screen_input.h): a la configuración si está abierta; si no,
    // a la caja; con la barra enfocada, se ignora.
    chatbot::cli::ScreenInput screen_input(settings, *input_area, flash, [&] {
        return sidebar_visible && sidebar_panel->Focused();
    });
    // Pegado entre corchetes: acumula lo que llega entre 200~ y 201~ (ver root).
    chatbot::cli::BracketedPaste& bracketed_paste = screen_input.bracketed_paste();

    const ftxui::Component layout = ftxui::Renderer(split, [&] {
        // Con un pegado abierto, cada cuadro le dice a BracketedPaste que el
        // loop ya procesó lo que leyó: su salida de emergencia solo cuenta ese
        // tiempo, no lo que FTXUI tarda en parsear un lote grande. Y pide el
        // siguiente cuadro, para que los haya aunque la terminal no mande nada.
        if (bracketed_paste.active()) {
            bracketed_paste.frame();
            screen.RequestAnimationFrame();
        }
        const int width = ftxui::Terminal::Size().dimx;
        // Al cruzar los 100 columnas: por debajo se oculta sola; por encima
        // reaparece solo si la había ocultado el ancho (no Ctrl+B).
        const bool wide = width >= chatbot::cli::kSidebarMinTerminal;
        if (wide != last_wide) {
            if (!wide && sidebar_visible) {
                hide_sidebar();
                hidden_by_width = true;
            } else if (wide && hidden_by_width) {
                sidebar_visible = true;
                hidden_by_width = false;
            }
            last_wide = wide;
        }
        // Límites del arrastre: 18 a 60 columnas o la mitad de la terminal
        // (con los dos bordes; main_size no cuenta el divisor).
        split_min = chatbot::cli::kSidebarMinWidth - 1;
        split_max = std::max(split_min,
                             std::min(chatbot::cli::kSidebarMaxWidth, width / 2) - 1);
        if (sidebar_visible) {
            sidebar_size = std::clamp(split_size > 0 ? split_size : sidebar_size, split_min,
                                      split_max);
            split_size = sidebar_size;
        } else {
            split_size = 0;
        }
        // Fondo y texto del tema en toda la pantalla (Palette::base).
        return split->Render() | palette.base();
    });

    // Ratón: la barra (rueda y clic), el divisor (de ResizableSplit) y el
    // historial (rueda).
    const auto handle_mouse = [&](ftxui::Event event) { // mouse() no es const.
        const ftxui::Mouse& mouse = event.mouse();
        if (dragging) {
            if (mouse.motion == ftxui::Mouse::Released) {
                dragging = false;
            }
            return false; // ResizableSplit cambia el ancho.
        }
        if (!sidebar_visible) {
            sidebar_box = ftxui::Box{};
        }
        if (mouse.motion == ftxui::Mouse::Moved) {
            if (!settings.is_open() && scroll.contains(mouse.x, mouse.y)) {
                history.set_hover(mouse.x, mouse.y);
            } else {
                history.clear_hover();
            }
            export_hover = export_box.Contain(mouse.x, mouse.y);
        }
        const bool on_divider = sidebar_visible && mouse.x == sidebar_box.x_max + 1 &&
                                mouse.y >= sidebar_box.y_min && mouse.y <= sidebar_box.y_max;
        if (on_divider && mouse.button == ftxui::Mouse::Left &&
            mouse.motion == ftxui::Mouse::Pressed) {
            dragging = true;
            return false;
        }
        if (sidebar_visible && sidebar_box.Contain(mouse.x, mouse.y)) {
            const std::vector<chatbot::cli::SidebarRow> rows = sidebar.rows(today());
            if (mouse.button == ftxui::Mouse::WheelUp) {
                sidebar.scroll(-chatbot::cli::kWheelStep, rows, sidebar_rows_visible());
            } else if (mouse.button == ftxui::Mouse::WheelDown) {
                sidebar.scroll(chatbot::cli::kWheelStep, rows, sidebar_rows_visible());
            } else if ((mouse.button == ftxui::Mouse::Left ||
                        mouse.button == ftxui::Mouse::Right) &&
                       mouse.motion == ftxui::Mouse::Pressed) {
                // Línea 0: el borde de arriba; luego, las filas visibles.
                const int line = mouse.y - sidebar_box.y_min - 1;
                if (line >= 0 && static_cast<std::size_t>(line) < sidebar_rows_visible()) {
                    const auto at = static_cast<std::size_t>(line);
                    if (mouse.button == ftxui::Mouse::Left) {
                        apply_sidebar_action(sidebar.click(at, rows));
                    } else if (sidebar.open_menu(at, rows)) {
                        // El menú se maneja con el teclado: necesita el foco.
                        sidebar_panel->TakeFocus();
                    }
                }
            }
            return true; // Solo el clic derecho (el menú) cambia el foco.
        }
        if (settings.is_open()) {
            return false;
        }
        const bool click =
            mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed;
        // Selector de /model y menú de comandos (prompt_menus.h): nada del
        // ratón sobre ellos llega al historial.
        if (chatbot::cli::menus_mouse(menus, menu_boxes, mouse)) {
            return true;
        }
        // Botones de los bloques y [Exportar] (sin respuestas no hace nada):
        // también con una respuesta en curso. El resultado va a la línea de
        // estado; no tocan el historial, el scroll ni la caja.
        if (const std::optional<chatbot::cli::ButtonHit> hit = history.hit_test(mouse.x, mouse.y);
            click && hit.has_value() && hit->reasoning.has_value()) {
            // La línea "▸ Pensamiento": abre o cierra el de esa respuesta.
            session.toggle_reasoning(*hit->reasoning);
            return true;
        } else if (click && hit.has_value()) {
            const chatbot::cli::ActionResult result = chatbot::cli::run_block_action(
                session.code_blocks().blocks(), hit->block, hit->action, chatbot::cli::real_clipboard(),
                chatbot::cli::default_download_dir(chatbot::cli::environment_value), home,
                palette.symbols());
            flash = result.message;
            if (!result.error && hit->action == chatbot::cli::BlockAction::Copy) {
                history.show_copied(hit->block);
            }
            return true;
        }
        if (click && export_box.Contain(mouse.x, mouse.y) &&
            session.conversation().has_turns()) {
            if (const std::optional<std::string> base = download_base()) {
                flash = chatbot::cli::export_conversation(session.conversation(),
                                                          std::time(nullptr), *base, home)
                            .message;
            }
            return true;
        }
        // La rueda: la caja o el historial (screen_input.h).
        return chatbot::cli::wheel_event(event, scroll, *input_area);
    };

    // Registro de teclas para diagnosticar (CHAT_DEBUG_KEYS, key_log.h).
    chatbot::cli::KeyLog key_log{chatbot::cli::environment_value("CHAT_DEBUG_KEYS")};

    const ftxui::Component root = ftxui::CatchEvent(layout, [&](ftxui::Event event) {
        if (key_log.enabled()) {
            screen_input.log_event(key_log, event);
        }
        control.resume();
        // Una respuesta de la terminal que FTXUI no entendió (DCS, DA) no
        // es una tecla: no borra el aviso ni llega a los componentes.
        if (!event.is_character() && !event.is_mouse() &&
            chatbot::cli::is_terminal_reply(event.input())) {
            return true;
        }
        // Lo que llega entre las marcas del pegado es texto, aunque sea
        // Enter, Tab o Ctrl+C.
        if (screen_input.feed_paste(event)) {
            return true;
        }
        if (event == ftxui::Event::CtrlC) {
            control.quit();
            return true;
        }
        if (event == ftxui::Event::Custom) {
            return false; // Solo pide redibujar.
        }
        // Con una pregunta (s/n) abierta, todo evento es de ella (yes_no.h):
        // ni el ratón, ni F2, ni Ctrl+B/O/N la saltan.
        if (settings.is_open() && settings.asking()) {
            (void)settings.component()->OnEvent(event);
            return true;
        }
        if (sidebar_visible && sidebar_panel->Focused() && sidebar.confirmation().has_value()) {
            if (!event.is_mouse()) {
                flash.clear();
                apply_sidebar_action(sidebar.handle(
                    list_key(event), event.is_character() ? event.character() : std::string{},
                    sidebar_rows_visible()));
            }
            return true;
        }
        if (event.is_mouse()) {
            return handle_mouse(std::move(event));
        }
        flash.clear(); // El aviso dura hasta la siguiente tecla.
        if (control.suspend(event)) {
            return false; // POSIX, Ctrl+Z: FTXUI suspende el proceso.
        }
        if (screen_input.paste_shortcut(event, key_log)) {
            return true; // Windows: Ctrl+V o Shift+Insert.
        }
        if (event == ftxui::Event::F2) {
            if (settings.is_open()) {
                (void)settings.request_close();
            } else {
                open_settings({});
            }
            return true;
        }
        if (event == ftxui::Event::CtrlB) {
            if (sidebar_visible) {
                hide_sidebar();
            } else {
                sidebar_visible = true;
            }
            hidden_by_width = false;
            return true;
        }
        if (event == ftxui::Event::CtrlO) {
            sidebar_visible = true;
            hidden_by_width = false;
            refresh_sidebar();
            sidebar_panel->TakeFocus();
            return true;
        }
        if (event == ftxui::Event::CtrlN) {
            apply_sidebar_action({chatbot::cli::ListAction::Type::New, {}});
            return true;
        }
        if (event == ftxui::Event::CtrlT) {
            // Abre o cierra el pensamiento de todas las respuestas.
            session.toggle_all_reasoning();
            return true;
        }
        if (sidebar_visible && sidebar_panel->Focused() && !sidebar.renaming().empty()) {
            // Campo de renombrar: Enter y Esc son de la barra; lo demás, del Input.
            const chatbot::cli::ListKey key = list_key(event);
            if (key == chatbot::cli::ListKey::Enter || key == chatbot::cli::ListKey::Escape) {
                apply_sidebar_action(sidebar.handle(key, {}, sidebar_rows_visible()));
            } else {
                (void)rename_input->OnEvent(event);
            }
            return true;
        }
        if (sidebar_visible && sidebar_panel->Focused()) {
            // La barra recibe todas las teclas; la caja de entrada, ninguna.
            const chatbot::cli::ListKey key = list_key(event);
            if (key == chatbot::cli::ListKey::Delete && session.busy()) {
                flash = std::string{chatbot::cli::kWaitOrCancel};
                return true;
            }
            apply_sidebar_action(sidebar.handle(
                key, event.is_character() ? event.character() : std::string{},
                sidebar_rows_visible()));
            return true;
        }
        if (settings.is_open()) {
            // La configuración recibe todas las teclas: si alguna llegara al
            // contenedor del split, podría mover el foco a la barra.
            (void)settings.component()->OnEvent(event);
            return true;
        }
        // El selector de /model y el menú de "/" (prompt_menus.h): el primer
        // Esc cierra el menú; el segundo ya cancela la respuesta.
        if (chatbot::cli::menus_key(menus, event)) {
            return true;
        }
        // "\" + Enter, Alt+Enter, flechas y Tab (screen_input.h).
        if (chatbot::cli::input_key(event, *input_area)) {
            return true;
        }
        if (event == ftxui::Event::Escape) {
            session.cancel(); // Sin petición en curso no hace nada (no borra la caja).
            return true;
        }
        // PgUp/PgDn y, con la caja vacía, Home/End (screen_input.h).
        return chatbot::cli::scroll_key(event, scroll, *input_area);
    });

    if (incomplete.has_value()) {
        // Falta la key o el modelo (o el proveedor no existe). Los avisos
        // del tema, de las instrucciones y de los endpoints, si hay, van
        // también.
        const std::string notice = startup_warning.empty()
                                       ? session.setup_problem()
                                       : session.setup_problem() + " " + startup_warning;
        open_settings(notice);
        flash = notice; // Si no se pudo abrir, al menos el aviso.
    } else {
        input->TakeFocus();
    }
    // Ctrl+C como evento y el pegado entre corchetes (screen_input.h).
    control.run(root);

    // Al regresar de Loop(), la sesión (con su runner) se destruye antes que
    // screen (y el cargador de modelos une sus hilos): el runner
    // cancela la petición en curso y hace join (alrededor de 1 s como máximo,
    // aunque el modelo no esté mandando nada o haya una espera de reintento).
    return 0;
}
