#include "settings_screen.h"

#include "chatbot/system_prompt.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"
#include "no_color_guard.hpp"
#include "session_fakes.hpp"
#include "symbols_check.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Position {
    int x = -1;
    int y = -1;
};

Position find(const ftxui::Screen& screen, const std::string& needle) {
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            std::string text;
            for (int k = x; k < screen.dimx() && text.size() < needle.size(); ++k) {
                const auto& character = screen.CellAt(k, y).character;
                text += character.empty() ? " " : character;
            }
            if (text == needle) {
                return {x, y};
            }
        }
    }
    return {};
}

struct Harness {
    chatbot_test::TaskQueue queue;
    chatbot::cli::Palette palette{chatbot::cli::resolve_appearance("", "")};
    std::optional<std::string> saved_model;
    std::string saved_provider; ///< provider_id() del último guardado.
    std::vector<chatbot::Endpoint> more_endpoints; ///< Después de "lab", en open().
    int appearance_saves = 0;
    std::optional<chatbot::cli::Appearance> saved_appearance; ///< La del último guardado.
    int prompt_saves = 0;
    std::optional<std::string> saved_prompt; ///< Lo que recibió el último guardado.
    std::optional<std::string> prompt_error; ///< Error que devuelve el guardado.
    int search_key_saves = 0;
    std::string saved_search_key; ///< Lo que recibió el último guardado de la key de búsqueda.
    int closes = 0;
    /// Configuración del modelo: por defecto, sin modelo (kNoModelForParams).
    chatbot::cli::ModelParamsPage model_page;
    int params_saves = 0;
    std::optional<chatbot::ModelParams> saved_params; ///< Lo que recibió el último guardado.
    bool wait_models = true; ///< open() espera la lista de modelos.
    std::string base_url = "http://localhost:8080/v1"; ///< La de open(), en el endpoint "lab".
    chatbot::Config base; ///< El Config de la sesión que recibe open().
    /// La URL de cada lista de modelos pedida (del hilo del loader; se lee
    /// después de vaciar la cola).
    std::vector<std::string> model_urls;
    chatbot::cli::ModelsLoader loader{
        [this] {
            auto transport = std::make_unique<chatbot_test::FakeTransport>();
            transport->on_send = [this, raw = transport.get()] {
                model_urls.push_back(raw->requests.back().url);
            };
            chatbot::HttpResponse response;
            response.status = 200;
            response.body = "{\"data\":[";
            for (int i = 0; i < 50; ++i) {
                if (i != 0) {
                    response.body += ',';
                }
                response.body += "{\"id\":\"modelo-" + std::to_string(i) + "\"}";
            }
            response.body += "]}";
            transport->responses.push_back(std::move(response));
            return transport;
        },
        // Como main.cpp: la tarea y después un Event::Custom para redibujar.
        // Sin auto: Clang 18 truena con la captura task = std::move(task).
        [this](std::function<void()> task) {
            queue.post([this, task = std::move(task)] {
                task();
                (void)settings.component()->OnEvent(ftxui::Event::Custom);
            });
        }};
    chatbot::cli::SettingsScreen settings{
        loader, palette,
        [this](const chatbot::cli::ProviderSettings& provider) -> std::optional<std::string> {
            saved_model = provider.model();
            saved_provider = provider.provider_id();
            return std::nullopt;
        },
        [this](const chatbot::cli::Appearance& appearance) -> std::optional<std::string> {
            ++appearance_saves;
            saved_appearance = appearance;
            return std::nullopt;
        },
        [this](const std::optional<std::string>& prompt) -> std::optional<std::string> {
            ++prompt_saves;
            saved_prompt = prompt;
            return prompt_error;
        },
        [this](const std::string& key) -> std::optional<std::string> {
            ++search_key_saves;
            saved_search_key = key;
            return std::nullopt;
        },
        [this] { ++closes; }};

    void open(std::string model, std::string key = {},
              std::string system_prompt = std::string{chatbot::kDefaultSystemPrompt},
              std::size_t history_limit = 32000,
              chatbot::cli::SearchSettings search = {}) {
        chatbot::Config config = base;
        config.base_url = base_url;
        config.model = model;
        config.history_limit_bytes = history_limit;
        chatbot::ConfigFileValues values;
        values.provider = "custom:lab";
        values.base_url = config.base_url;
        values.model = std::move(model);
        values.endpoints = {{"lab", "Servidor del lab", config.base_url}};
        values.endpoints.insert(values.endpoints.end(), more_endpoints.begin(),
                                more_endpoints.end());
        chatbot::cli::ProviderSettings provider(values, {}, {});
        (void)provider.set_key(std::move(key));
        // Como Session: validate_model_params y la vista previa con 2 mensajes.
        settings.set_model_params_handlers(
            [this](const chatbot::ModelParams& draft) {
                chatbot::cli::ModelParamsCheck check;
                check.problems = chatbot::validate_model_params(draft, model_page.provider);
                if (check.problems.empty()) {
                    check.preview = chatbot::request_preview(model_page.model, model_page.provider,
                                                             draft, 2);
                }
                return check;
            },
            [this](const chatbot::ModelParams& draft) -> std::optional<std::string> {
                ++params_saves;
                saved_params = draft;
                return std::nullopt;
            });
        settings.open(std::move(provider), config,
                      chatbot::cli::resolve_appearance("catppuccin-mocha", "theme"),
                      std::move(system_prompt), std::move(search), {}, model_page);
        if (wait_models) {
            REQUIRE(queue.run_until([this] { return !loader.busy(); }));
        }
    }

    ftxui::Screen draw(int width = 120, int height = 24) {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                            ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, settings.component()->Render());
        return screen;
    }

    bool mouse(Position position, ftxui::Mouse::Button button,
               ftxui::Mouse::Motion motion = ftxui::Mouse::Pressed) {
        ftxui::Mouse mouse;
        mouse.x = position.x;
        mouse.y = position.y;
        mouse.button = button;
        mouse.motion = motion;
        return settings.component()->OnEvent(ftxui::Event::Mouse("", mouse));
    }

    void save() {
        const auto screen = draw();
        const Position button = find(screen, "[ Guardar ]");
        REQUIRE(button.x >= 0);
        REQUIRE(mouse(button, ftxui::Mouse::Left));
    }

    bool key(const ftxui::Event& event) { return settings.component()->OnEvent(event); }

    /// Elige "Configuración del modelo" (pestaña Instrucciones) y pasa al
    /// campo de texto con Tab: la barra de pestañas y luego el campo.
    void go_to_prompt() {
        go_to_model();
        REQUIRE(key(ftxui::Event::Tab));
        REQUIRE(key(ftxui::Event::Tab));
    }

    /// Elige "Configuración del modelo" (sin pasar a la página).
    void go_to_model() {
        REQUIRE(key(ftxui::Event::ArrowDown));
        REQUIRE(key(ftxui::Event::ArrowDown));
        REQUIRE(find(draw(), "● Configuración del modelo").x >= 0);
    }

    /// Elige "Búsqueda web" (sin pasar al campo).
    void go_to_search() {
        for (int i = 0; i < 3; ++i) {
            REQUIRE(key(ftxui::Event::ArrowDown));
        }
        REQUIRE(find(draw(), "● Búsqueda web").x >= 0);
    }
};

} // namespace

TEST_CASE("configuración: clic elige el modelo con y sin desplazamiento", "[ajustes][raton]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    const int initial = GENERATE(0, 35);
    Harness h;
    h.open("modelo-" + std::to_string(initial));
    auto screen = h.draw();
    const std::string target = "modelo-" + std::to_string(initial + 1);
    const Position row = find(screen, target);
    REQUIRE(row.x >= 0);
    REQUIRE(h.mouse(row, ftxui::Mouse::Left));
    screen = h.draw();
    CHECK(find(screen, "● " + target).x >= 0);
    h.save();
    REQUIRE(h.saved_model.has_value());
    CHECK(*h.saved_model == target);
}

TEST_CASE("configuración: rueda solo mueve el cursor dentro de la lista", "[ajustes][raton]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-35");
    auto screen = h.draw();
    Position row = find(screen, "modelo-35");
    REQUIRE(row.x >= 0);
    REQUIRE(h.mouse(row, ftxui::Mouse::WheelDown));
    screen = h.draw();
    row = find(screen, "modelo-36");
    REQUIRE(row.x >= 0);
    const auto selection = screen.CellAt(row.x, row.y).background_color;
    CHECK(selection == chatbot::cli::to_ftxui(h.palette.theme().selection_bg));
    (void)h.mouse({119, 23}, ftxui::Mouse::WheelDown);
    screen = h.draw();
    row = find(screen, "modelo-36");
    REQUIRE(row.x >= 0);
    CHECK(screen.CellAt(row.x, row.y).background_color == selection);
    REQUIRE(h.settings.component()->OnEvent(ftxui::Event::Return));
    h.save();
    REQUIRE(h.saved_model.has_value());
    CHECK(*h.saved_model == "modelo-36");
}

TEST_CASE("configuración: aviso de URL sin ruta bajo el campo, sin mover los campos",
          "[ajustes][url]") {
    for (const int width : {120, 100, 60, 40}) {
        INFO("ancho " << width);
        Harness with_path;
        with_path.open("modelo-1");
        const auto reference = with_path.draw(width);
        CHECK(find(reference, "Casi todos").x < 0);

        Harness root;
        root.base_url = "http://localhost:8080";
        root.open("modelo-1");
        const auto screen = root.draw(width);
        const Position url = find(screen, "URL base");
        REQUIRE(url.x >= 0);
        if (width >= 60) {
            const Position shown = find(screen, "Casi todos");
            REQUIRE(shown.x >= 0);
            CHECK(shown.y == url.y + 1);
            CHECK(shown.x == url.x + 12); // Alineado con el campo.
        }
        // Las filas están también sin aviso: los campos quedan donde estaban.
        for (const char* field : {"URL base", "API key", "Modelos", "[ Guardar ]"}) {
            INFO(field);
            if (width >= 60) {
                CHECK(find(reference, field).x >= 0);
            }
            CHECK(find(screen, field).y == find(reference, field).y);
            CHECK(find(screen, field).x == find(reference, field).x);
        }
        // Completo en dos filas desde 100 columnas; más angosta, se recorta.
        if (width >= 100) {
            CHECK(find(screen, "https://servidor/v1).").y == url.y + 2);
        }
    }
}

TEST_CASE("configuración: el aviso de URL usa la tinta de los avisos, también con NO_COLOR",
          "[ajustes][url][tema]") {
    const char* no_color = GENERATE(static_cast<const char*>(nullptr), "1");
    const chatbot_test::NoColorGuard environment(no_color);
    Harness h;
    h.base_url = "http://localhost:8080";
    h.open("modelo-1");
    const auto screen = h.draw();
    const Position hint = find(screen, "Casi todos");
    const Position other = find(screen, "opcional en local"); // Otro aviso (key_status).
    REQUIRE(hint.x >= 0);
    REQUIRE(other.x >= 0);
    const auto& hint_cell = screen.CellAt(hint.x, hint.y);
    const auto& other_cell = screen.CellAt(other.x, other.y);
    CHECK(hint_cell.foreground_color == other_cell.foreground_color);
    CHECK(hint_cell.dim == other_cell.dim);
    if (no_color != nullptr) {
        CHECK(hint_cell.dim);
    }
}

TEST_CASE("configuración: NO_COLOR bloquea tema y fondo y no los guarda", "[ajustes][tema]") {
    const chatbot_test::NoColorGuard environment("1");
    Harness h;
    h.open("modelo-0");
    REQUIRE(h.settings.component()->OnEvent(ftxui::Event::ArrowDown));
    auto screen = h.draw();
    CHECK(find(screen, "Desactivado por NO_COLOR").x >= 0);
    CHECK(find(screen, "De la terminal").x >= 0);
    CHECK(find(screen, "Transparente").x >= 0);
    // Tab: los símbolos (se eligen también con NO_COLOR) y luego Guardar.
    REQUIRE(h.settings.component()->OnEvent(ftxui::Event::Tab));
    screen = h.draw();
    const Position automatic = find(screen, "● Automático");
    REQUIRE(automatic.x >= 0);
    CHECK(screen.CellAt(automatic.x, automatic.y).inverted);
    REQUIRE(h.settings.component()->OnEvent(ftxui::Event::Tab));
    screen = h.draw();
    const Position save = find(screen, "[ Guardar ]");
    REQUIRE(save.x >= 0);
    CHECK(screen.CellAt(save.x, save.y).inverted);
    REQUIRE(h.settings.component()->OnEvent(ftxui::Event::Return));
    CHECK(h.appearance_saves == 0);
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: Símbolos se elige y se guarda, también con NO_COLOR",
          "[ajustes][simbolos]") {
    const char* no_color = GENERATE(static_cast<const char*>(nullptr), "1");
    const chatbot_test::NoColorGuard environment(no_color);
    Harness h;
    h.open("modelo-0");
    REQUIRE(h.key(ftxui::Event::ArrowDown)); // Colores y Accesibilidad.
    auto screen = h.draw();
    CHECK(find(screen, "Símbolos").x >= 0);
    CHECK(find(screen, "● Automático").x >= 0);
    CHECK(find(screen, "  Unicode").x >= 0);
    CHECK(find(screen, "  Sencillos").x >= 0);

    // Tab hasta la lista de símbolos (con colores: tema y fondo antes).
    const int tabs = no_color == nullptr ? 3 : 1;
    for (int i = 0; i < tabs; ++i) {
        REQUIRE(h.key(ftxui::Event::Tab));
    }
    REQUIRE(h.key(ftxui::Event::ArrowDown));
    REQUIRE(h.key(ftxui::Event::ArrowDown));
    REQUIRE(h.key(ftxui::Event::Return));
    screen = h.draw();
    CHECK(find(screen, "● Sencillos").x >= 0);
    h.save();
    CHECK(h.appearance_saves == 1);
    REQUIRE(h.saved_appearance.has_value());
    CHECK(h.saved_appearance->symbol_mode == chatbot::cli::SymbolMode::Simple);
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: Guardar rechaza keys incompletas sin llamar al guardado", "[ajustes][key]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    const std::string key = GENERATE(std::string{"$NIMKEY"}, std::string{"  "},
                                      std::string{"abc"}, std::string(30, 'a') + "\n");
    Harness h;
    h.open("modelo-0", key);
    h.save();
    CHECK_FALSE(h.saved_model.has_value());
    CHECK(h.settings.is_open());
    const auto screen = h.draw();
    CHECK(find(screen, "Eso parece el nombre de una variable").x >= 0);
    CHECK(find(screen, "pega la key completa.").x >= 0);
    if (key == "$NIMKEY" || key == "abc") {
        CHECK(find(screen, key).x < 0);
    }
}

TEST_CASE("configuración: Guardar acepta una key sintética completa", "[ajustes][key]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", std::string(70, 'x'));
    h.save();
    CHECK(h.saved_model == "modelo-0");
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: escribir instrucciones y guardarlas", "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "");
    h.go_to_prompt();
    auto screen = h.draw();
    CHECK(find(screen, "0 / 8000 bytes").x >= 0);
    CHECK(find(screen, "Vacío: sin instrucciones de sistema").x >= 0);
    CHECK(find(screen, "[ Restaurar predeterminado ]").x >= 0);

    REQUIRE(h.key(ftxui::Event::Character("Responde solo")));
    REQUIRE(h.key(ftxui::Event::Return)); // Enter: salto de línea, no guarda.
    REQUIRE(h.key(ftxui::Event::Character("con PIÑA")));
    CHECK(h.prompt_saves == 0);
    CHECK(h.settings.is_open());
    screen = h.draw();
    const Position first = find(screen, "Responde solo");
    const Position second = find(screen, "con PIÑA");
    REQUIRE(first.x >= 0);
    REQUIRE(second.x >= 0);
    CHECK(second.y == first.y + 1);
    // "Responde solo\ncon PIÑA": 13 + 1 + 9 bytes (la Ñ son dos).
    CHECK(find(screen, "23 / 8000 bytes").x >= 0);

    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK(h.saved_prompt == std::string{"Responde solo\ncon PIÑA"});
    CHECK_FALSE(h.saved_model.has_value()); // Solo cambiaron las instrucciones.
    CHECK(h.appearance_saves == 0);
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: vaciar las instrucciones guarda la cadena vacía",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "abc");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::End));
    for (int i = 0; i < 3; ++i) {
        REQUIRE(h.key(ftxui::Event::Backspace));
    }
    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK(h.saved_prompt == std::string{});
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: Restaurar predeterminado borra la llave al guardar",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "Mis instrucciones");
    h.go_to_prompt();
    CHECK(find(h.draw(), "Mis instrucciones").x >= 0);
    REQUIRE(h.key(ftxui::Event::Tab)); // Al botón.
    REQUIRE(h.key(ftxui::Event::Return));
    const auto screen = h.draw();
    CHECK(find(screen, "Mis instrucciones").x < 0);
    CHECK(find(screen, "Eres un asistente útil.").x >= 0);
    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK_FALSE(h.saved_prompt.has_value()); // nullopt: se borra la llave.
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: sin cambios en las instrucciones no se guardan",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::Tab));
    REQUIRE(h.key(ftxui::Event::Return)); // Restaurar con el predeterminado ya puesto.
    h.save();
    CHECK(h.prompt_saves == 0);
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: descartar cambios de las instrucciones", "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "Original");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::Character("x")));
    REQUIRE(h.key(ftxui::Event::Escape));
    CHECK(find(h.draw(), "¿Descartar los cambios? (s/n)").x >= 0);
    REQUIRE(h.key(ftxui::Event::Character("n")));
    CHECK(h.settings.is_open());
    CHECK(find(h.draw(), "¿Descartar los cambios? (s/n)").x < 0);
    CHECK(find(h.draw(), "xOriginal").x >= 0); // El cambio sigue ahí.

    REQUIRE(h.key(ftxui::Event::Escape));
    REQUIRE(h.key(ftxui::Event::Character("s")));
    CHECK_FALSE(h.settings.is_open());
    CHECK(h.prompt_saves == 0);
    CHECK(h.closes == 1);

    // Al abrir de nuevo, el campo tiene las vigentes.
    h.open("modelo-0", {}, "Original");
    h.go_to_prompt();
    const auto screen = h.draw();
    CHECK(find(screen, "Original").x >= 0);
    CHECK(find(screen, "xOriginal").x < 0);
}

TEST_CASE("configuración: Tab y Shift+Tab recorren las instrucciones", "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::Character("a"))); // El campo tiene el foco.
    REQUIRE(h.key(ftxui::Event::Tab));             // Restaurar.
    REQUIRE(h.key(ftxui::Event::Tab));             // Guardar.
    auto screen = h.draw();
    Position save = find(screen, "[ Guardar ]");
    REQUIRE(save.x >= 0);
    CHECK(screen.CellAt(save.x, save.y).background_color ==
          chatbot::cli::to_ftxui(h.palette.theme().selection_bg));
    REQUIRE(h.key(ftxui::Event::TabReverse)); // Restaurar.
    REQUIRE(h.key(ftxui::Event::TabReverse)); // El campo.
    REQUIRE(h.key(ftxui::Event::Character("b")));
    CHECK(find(h.draw(), "ab").x >= 0);
}

TEST_CASE("configuración: instrucciones inválidas no se guardan y el error se ve",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    SECTION("control") {
        // Así pudo quedar en config.json editado a mano.
        h.open("modelo-0", {}, "uno\x1b[31mdos");
        h.go_to_prompt();
        // El aviso aparece antes de guardar: el campo no dibuja el control.
        CHECK(find(h.draw(), "carácter de control no permitido").x >= 0);
        REQUIRE(h.key(ftxui::Event::Character("a")));
    }
    SECTION("largo") {
        h.open("modelo-0", {}, std::string(chatbot::kMaxSystemPromptBytes, 'a'));
        h.go_to_prompt();
        CHECK(find(h.draw(), "8000 / 8000 bytes").x >= 0);
        REQUIRE(h.key(ftxui::Event::Character("a")));
        CHECK(find(h.draw(), "8001 / 8000 bytes").x >= 0);
        CHECK(find(h.draw(), "el máximo es 8000").x >= 0);
    }
    h.save();
    CHECK(h.prompt_saves == 0);
    CHECK(h.settings.is_open());
    CHECK_FALSE(h.saved_model.has_value()); // Nada se guarda a medias.
    const auto screen = h.draw();
    CHECK((find(screen, "carácter de control no permitido").x >= 0 ||
           find(screen, "el máximo es 8000").x >= 0));
}

TEST_CASE("configuración: aviso si las instrucciones llenan el límite del historial",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "", 10);
    h.go_to_prompt();
    CHECK(find(h.draw(), "límite del historial").x < 0);
    REQUIRE(h.key(ftxui::Event::Character("0123456789")));
    CHECK(find(h.draw(), "límite del historial").x >= 0);
    h.save(); // Se guarda igual.
    CHECK(h.prompt_saves == 1);
    CHECK(h.saved_prompt == std::string{"0123456789"});
}

TEST_CASE("configuración: el error del guardado de instrucciones se muestra",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.prompt_error = "No se guardó la configuración: disco lleno";
    h.open("modelo-0", {}, "");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::Character("hola")));
    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK(h.settings.is_open());
    CHECK(find(h.draw(), "disco lleno").x >= 0);
}

TEST_CASE("configuración: guardar la key de búsqueda", "[ajustes][busqueda]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"", std::nullopt});
    h.go_to_search();
    auto screen = h.draw();
    CHECK(find(screen, "Búsqueda web").x >= 0);
    CHECK(find(screen, "sin configurar").x >= 0);
    CHECK(find(screen, "Tavily: 1,000 búsquedas gratis al mes.").x >= 0);
    CHECK(find(screen, "nunca la conversación.").x >= 0);

    REQUIRE(h.key(ftxui::Event::Tab)); // El campo de la key.
    const std::string key = "tvly-clave-ficticia-de-prueba-WXYZ";
    REQUIRE(h.key(ftxui::Event::Character(key)));
    screen = h.draw();
    CHECK(find(screen, "tvly").x < 0); // Nunca en claro.

    h.save();
    CHECK(h.search_key_saves == 1);
    CHECK(h.saved_search_key == key);
    CHECK_FALSE(h.saved_model.has_value()); // Solo cambió la key de búsqueda.
    CHECK(h.prompt_saves == 0);
    CHECK(h.appearance_saves == 0);
    CHECK_FALSE(h.settings.is_open());
}

TEST_CASE("configuración: key de búsqueda guardada se ve enmascarada", "[ajustes][busqueda]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"tvly-guardada-secreta-0123456789abcd", std::nullopt});
    h.go_to_search();
    const auto screen = h.draw();
    CHECK(find(screen, "guardada: …abcd").x >= 0);
    CHECK(find(screen, "secreta").x < 0);
    CHECK(find(screen, "0123456789").x < 0);
}

TEST_CASE("configuración: CHAT_SEARCH_API_KEY bloquea el campo de búsqueda",
          "[ajustes][busqueda]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"tvly-guardada-0000000000000000",
                                        std::string{"tvly-del-entorno-0000000000009876"}});
    h.go_to_search();
    auto screen = h.draw();
    CHECK(find(screen, "(definido por CHAT_SEARCH_API_KEY) …9876").x >= 0);
    CHECK(find(screen, "escribe la key de Tavily").x < 0); // Sin campo.
    // Tab pasa de las categorías directo a Guardar.
    REQUIRE(h.key(ftxui::Event::Tab));
    screen = h.draw();
    const Position save = find(screen, "[ Guardar ]");
    REQUIRE(save.x >= 0);
    CHECK(screen.CellAt(save.x, save.y).background_color ==
          chatbot::cli::to_ftxui(h.palette.theme().selection_bg));
    // Escribir no cambia nada: no hay cambios que guardar ni que descartar.
    (void)h.key(ftxui::Event::Character("x"));
    REQUIRE(h.key(ftxui::Event::Escape));
    CHECK_FALSE(h.settings.is_open());
    CHECK(h.search_key_saves == 0);
}

TEST_CASE("configuración: key de búsqueda incompleta no se guarda", "[ajustes][busqueda]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"", std::nullopt});
    h.go_to_search();
    REQUIRE(h.key(ftxui::Event::Tab));
    REQUIRE(h.key(ftxui::Event::Character("$TAVILY_KEY")));
    h.save();
    CHECK(h.search_key_saves == 0);
    CHECK_FALSE(h.saved_model.has_value());
    CHECK(h.settings.is_open());
    const auto screen = h.draw();
    CHECK(find(screen, "key de búsqueda incompleta").x >= 0);
    CHECK(find(screen, "$TAVILY_KEY").x < 0);
}

TEST_CASE("configuración: solo s o n contestan si se descartan los cambios",
          "[ajustes][sistema]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    constexpr std::string_view question = "¿Descartar los cambios? (s/n)";
    Harness h;
    h.wait_models = false;
    h.open("modelo-0", {}, "Original");
    h.go_to_prompt();
    REQUIRE(h.key(ftxui::Event::Character("x")));
    REQUIRE(h.key(ftxui::Event::Escape));
    REQUIRE(find(h.draw(), std::string{question}).x >= 0);

    // La lista de modelos llega con la pregunta abierta: la tarea y su
    // Event::Custom, como en main.cpp. Antes contestaba "no".
    REQUIRE(h.loader.busy());
    REQUIRE(h.queue.run_until([&] { return !h.loader.busy(); }));
    CHECK(find(h.draw(), std::string{question}).x >= 0);

    // Ni otra tecla, ni Enter, ni el ratón (tampoco sobre [ Guardar ]).
    for (const ftxui::Event& event :
         {ftxui::Event::Custom, ftxui::Event::Return, ftxui::Event::Tab, ftxui::Event::Delete,
          ftxui::Event::ArrowDown, ftxui::Event::Character("x"), ftxui::Event::Character(" ")}) {
        CHECK(h.key(event));
        CHECK(find(h.draw(), std::string{question}).x >= 0);
    }
    const Position save = find(h.draw(), "[ Guardar ]");
    REQUIRE(save.x >= 0);
    CHECK(h.mouse(save, ftxui::Mouse::Left));
    CHECK(h.mouse(save, ftxui::Mouse::Left, ftxui::Mouse::Released));
    CHECK(find(h.draw(), std::string{question}).x >= 0);
    CHECK(h.prompt_saves == 0);
    CHECK(h.closes == 0);

    // Esc cancela y lo dice; n y N contestan que no; el cambio sigue.
    REQUIRE(h.key(ftxui::Event::Escape));
    CHECK(find(h.draw(), std::string{question}).x < 0);
    CHECK(find(h.draw(), std::string{chatbot::cli::kDiscardCancelled}).x >= 0);
    for (const char* no : {"n", "N"}) {
        REQUIRE(h.key(ftxui::Event::Escape));
        REQUIRE(find(h.draw(), std::string{question}).x >= 0);
        REQUIRE(h.key(ftxui::Event::Character(no)));
        CHECK(find(h.draw(), std::string{question}).x < 0);
        CHECK(h.settings.is_open());
    }
    CHECK(find(h.draw(), "xOriginal").x >= 0);

    REQUIRE(h.key(ftxui::Event::Escape));
    REQUIRE(h.key(ftxui::Event::Character("S")));
    CHECK_FALSE(h.settings.is_open());
    CHECK(h.prompt_saves == 0);
    CHECK(h.closes == 1);
}

TEST_CASE("configuración: descartar una key de búsqueda escrita pregunta",
          "[ajustes][busqueda]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"", std::nullopt});
    h.go_to_search();
    REQUIRE(h.key(ftxui::Event::Tab));
    REQUIRE(h.key(ftxui::Event::Character("tvly-clave-ficticia-de-prueba-0000")));
    REQUIRE(h.key(ftxui::Event::Escape));
    CHECK(h.settings.is_open());
    CHECK(find(h.draw(), "¿Descartar los cambios? (s/n)").x >= 0);
    REQUIRE(h.key(ftxui::Event::Character("s")));
    CHECK_FALSE(h.settings.is_open());
    CHECK(h.search_key_saves == 0);
}

TEST_CASE("configuración: a dónde va un pegado según el foco", "[ajustes][pegar]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    using chatbot::cli::PasteTarget;
    Harness h;
    h.open("modelo-0", {}, "");
    // Al abrir, el foco está en las categorías: el pegado se ignora.
    CHECK(h.settings.paste_target() == PasteTarget::None);

    h.go_to_prompt();
    REQUIRE(h.settings.paste_target() == PasteTarget::MultiLine);
    // Como lo hace main.cpp: un solo Event::Character con todo el texto.
    const auto paste = chatbot::cli::prepare_paste("Primera\r\n\tSegunda\n", PasteTarget::MultiLine);
    REQUIRE(paste.has_value());
    REQUIRE(h.key(ftxui::Event::Character(paste->text)));
    auto screen = h.draw();
    const Position first = find(screen, "Primera");
    const Position second = find(screen, "    Segunda");
    REQUIRE(first.x >= 0);
    REQUIRE(second.y == first.y + 1);

    REQUIRE(h.key(ftxui::Event::Tab)); // Restaurar: un botón.
    CHECK(h.settings.paste_target() == PasteTarget::None);
    REQUIRE(h.key(ftxui::Event::TabReverse));
    CHECK(h.settings.paste_target() == PasteTarget::MultiLine);

    // Con la pregunta de descartar, un pegado no la contesta.
    REQUIRE(h.key(ftxui::Event::Escape));
    REQUIRE(find(h.draw(), "¿Descartar los cambios? (s/n)").x >= 0);
    CHECK(h.settings.paste_target() == PasteTarget::None);
}

TEST_CASE("configuración: pegar una key con salto de línea, sin mostrarla", "[ajustes][pegar]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    using chatbot::cli::PasteTarget;
    Harness h;
    h.open("modelo-0", {}, std::string{chatbot::kDefaultSystemPrompt}, 32000,
           chatbot::cli::SearchSettings{"", std::nullopt});
    h.go_to_search();
    REQUIRE(h.key(ftxui::Event::Tab)); // El campo de la key.
    REQUIRE(h.settings.paste_target() == PasteTarget::SingleLine);
    const std::string key = "tvly-clave-ficticia-de-prueba-WXYZ";
    const auto paste = chatbot::cli::prepare_paste(key + "\r\n", PasteTarget::SingleLine);
    REQUIRE(paste.has_value());
    REQUIRE(h.key(ftxui::Event::Character(paste->text)));
    // El aviso de un pegado recortado no lleva el texto.
    h.settings.show_notice(std::string{chatbot::cli::kPasteTruncated});
    const auto screen = h.draw();
    CHECK(find(screen, "tvly").x < 0);
    CHECK(find(screen, "WXYZ").x < 0);
    CHECK(find(screen, "se pegaron los primeros 256 KiB.").x >= 0);

    h.save();
    CHECK(h.search_key_saves == 1);
    CHECK(h.saved_search_key == key);
}

TEST_CASE("configuración: pegar solo un salto de línea en las instrucciones", "[ajustes][pegar]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    using chatbot::cli::PasteTarget;
    Harness h;
    h.open("modelo-0", {}, "ab");
    h.go_to_prompt();
    REQUIRE(h.settings.paste_target() == PasteTarget::MultiLine);
    REQUIRE(h.key(ftxui::Event::End));
    REQUIRE(h.key(ftxui::Event::ArrowLeft)); // El cursor entre "a" y "b".
    const auto paste = chatbot::cli::prepare_paste("\r\n", PasteTarget::MultiLine);
    REQUIRE(paste.has_value());
    REQUIRE(paste->text == "\n");
    // Como lo hace main.cpp. Event compara solo el texto: para el campo es
    // Enter, que con multiline inserta "\n" y llama a on_enter (vacío).
    REQUIRE(h.key(ftxui::Event::Character(paste->text)));
    const auto screen = h.draw();
    CHECK(find(screen, "3 / 8000 bytes").x >= 0); // "a\nb".
    // Nada más: no guarda, no cierra, no mueve el foco ni pregunta.
    CHECK(h.prompt_saves == 0);
    CHECK(h.closes == 0);
    CHECK(h.settings.is_open());
    CHECK(h.settings.paste_target() == PasteTarget::MultiLine);
    CHECK(find(screen, "¿Descartar los cambios? (s/n)").x < 0);
    // Lo siguiente que se escribe va después del salto.
    REQUIRE(h.key(ftxui::Event::Character("x")));
    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK(h.saved_prompt == std::string{"a\nxb"});
}

TEST_CASE("configuración: símbolos sencillos con la lista de proveedores abierta",
          "[ajustes][simbolos]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    const std::vector<chatbot::cli::Palette> unicode =
        chatbot_test::palettes_with(chatbot::cli::Symbols::Unicode);
    const std::vector<chatbot::cli::Palette> simple =
        chatbot_test::palettes_with(chatbot::cli::Symbols::Simple);
    // La misma pantalla con cada juego de símbolos.
    const auto compare = [](Harness& h, const chatbot::cli::Palette& a,
                            const chatbot::cli::Palette& b) {
        h.palette = a;
        const ftxui::Screen first = h.draw(100);
        h.palette = b;
        ftxui::Screen second = h.draw(100);
        chatbot_test::check_simple_matches(first, second);
        return std::pair{chatbot_test::table_glyphs_in(first), std::move(second)};
    };
    for (std::size_t i = 0; i < unicode.size(); ++i) {
        INFO("paleta " << unicode[i].key());
        {
            // Proveedor, con la lista de proveedores abierta: barra y ▴.
            Harness h;
            h.open("modelo-0");
            REQUIRE(h.key(ftxui::Event::Tab));
            REQUIRE(h.key(ftxui::Event::Return));
            const auto [glyphs, screen] = compare(h, unicode[i], simple[i]);
            CHECK(glyphs > 0);
            CHECK(find(screen, " ^]").x >= 0);
            CHECK(find(screen, "█").x >= 0); // La barra de la lista.
        }
        {
            // Cerrada (▾), con la lista de modelos y su barra.
            Harness h;
            h.open("modelo-0");
            const auto [glyphs, screen] = compare(h, unicode[i], simple[i]);
            CHECK(glyphs > 0);
            CHECK(find(screen, " v]").x >= 0);
        }
        {
            // Colores y Accesibilidad.
            Harness h;
            h.open("modelo-0");
            REQUIRE(h.key(ftxui::Event::ArrowDown));
            const auto [glyphs, screen] = compare(h, unicode[i], simple[i]);
            CHECK(find(screen, "Símbolos").x >= 0);
            (void)glyphs;
        }
    }
}

TEST_CASE("configuración: ningún placeholder es más ancho que su campo", "[ajustes]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.base_url = ""; // Sin URL, para que el campo muestre su placeholder.
    h.open("", {}, "", 32000, chatbot::cli::SearchSettings{"", std::nullopt});
    // Cada placeholder completo: uno recortado (los campos de la key miden
    // kKeyWidth columnas) no se encontraría.
    auto screen = h.draw();
    CHECK(find(screen, "https://mi-servidor/v1").x >= 0);
    CHECK(find(screen, "escribe la API key").x >= 0);
    CHECK(find(screen, "escribe el modelo").x >= 0);
    h.go_to_prompt();
    CHECK(find(h.draw(), "escribe las instrucciones").x >= 0);
    REQUIRE(h.key(ftxui::Event::TabReverse)); // La barra de pestañas.
    REQUIRE(h.key(ftxui::Event::TabReverse)); // De vuelta a las categorías.
    REQUIRE(h.key(ftxui::Event::ArrowDown));
    screen = h.draw();
    REQUIRE(find(screen, "● Búsqueda web").x >= 0);
    CHECK(find(screen, "escribe la key de Tavily").x >= 0);
}

TEST_CASE("configuración: el modo portable se ve al pie de Proveedor de IA",
          "[ajustes][portable]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0");
    CHECK(find(h.draw(), "Modo portable").x < 0); // Sin modo portable, nada nuevo.

    h.settings.set_portable_dir(std::string{"E:\\moss\\datos"});
    auto screen = h.draw();
    const Position line = find(screen, "Modo portable: datos en E:\\moss\\datos");
    REQUIRE(line.x >= 0);
    // Atenuada: con la tinta de los avisos.
    CHECK(screen.CellAt(line.x, line.y).foreground_color ==
          chatbot::cli::to_ftxui(h.palette.theme().notice));
    // Solo en "Proveedor de IA".
    h.go_to_search();
    CHECK(find(h.draw(), "Modo portable").x < 0);
}

namespace {

/// Ancho de la configuración con la barra lateral abierta en una terminal de
/// 120 columnas: la barra (28, su borde derecho es el divisor) y un espacio.
constexpr int kSettingsWidthWithSidebar = 120 - 28 - 1;

/// "fila 01\nfila 02\n…": instrucciones largas, una línea por fila.
std::string long_prompt(int lines) {
    std::string text;
    for (int i = 1; i <= lines; ++i) {
        text += (i > 1 ? "\n" : "") + std::string{i < 10 ? "fila 0" : "fila "} + std::to_string(i);
    }
    return text;
}

} // namespace

TEST_CASE("configuración: las instrucciones predeterminadas se leen completas con la barra "
          "lateral a 120 columnas",
          "[ajustes][sistema][ajuste]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0");
    h.go_to_prompt();
    const auto screen = h.draw(kSettingsWidthWithSidebar);
    // El campo mide unas 47 columnas: el texto (71) se ajusta en dos filas,
    // que juntas tienen todas sus palabras, en orden.
    const Position first = find(screen, "Eres un asistente");
    REQUIRE(first.x >= 0);
    const auto words = [](const std::string& text) {
        std::vector<std::string> out;
        std::istringstream stream(text);
        for (std::string word; stream >> word;) {
            out.push_back(word);
        }
        return out;
    };
    std::string shown;
    for (int y = first.y; y < first.y + 2; ++y) {
        for (int x = first.x; x < screen.dimx() - 1; ++x) { // Sin el borde.
            shown += screen.CellAt(x, y).character;
        }
        shown += ' ';
    }
    CHECK(words(shown) == words(std::string{chatbot::kDefaultSystemPrompt}));
}

TEST_CASE("configuración: el campo de las instrucciones mide al menos 5 filas",
          "[ajustes][sistema][ajuste]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "");
    h.go_to_prompt();
    const auto screen = h.draw(kSettingsWidthWithSidebar);
    const Position field = find(screen, "Texto");
    const Position counter = find(screen, "0 / 8000 bytes");
    REQUIRE(field.x >= 0);
    REQUIRE(counter.x >= 0);
    CHECK(counter.y - field.y >= 5);
}

TEST_CASE("configuración: con instrucciones largas los botones siempre se ven",
          "[ajustes][sistema][ajuste]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, long_prompt(60));
    h.go_to_prompt();
    for (int height = 14; height <= 40; height += 2) {
        INFO("alto " << height);
        const auto screen = h.draw(kSettingsWidthWithSidebar, height);
        CHECK(find(screen, "[ Guardar ]").x >= 0);
        CHECK(find(screen, "[ Cancelar ]").x >= 0);
        CHECK(find(screen, "[ Restaurar predeterminado ]").x >= 0);
        CHECK(find(screen, "fila 01").x >= 0); // El cursor está al principio.
    }
}

TEST_CASE("configuración: PgUp/PgDn y la rueda desplazan las instrucciones",
          "[ajustes][sistema][ajuste]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, long_prompt(60));
    h.go_to_prompt();
    auto screen = h.draw(kSettingsWidthWithSidebar);
    const Position top = find(screen, "fila 01");
    REQUIRE(top.x >= 0);

    SECTION("PgDn baja una altura del campo y el foco se queda en él") {
        REQUIRE(h.key(ftxui::Event::PageDown));
        screen = h.draw(kSettingsWidthWithSidebar);
        CHECK(find(screen, "fila 01").x < 0);
        CHECK(h.settings.paste_target() == chatbot::cli::PasteTarget::MultiLine);
        // Escribir no salta de regreso: el cursor bajó con la vista.
        REQUIRE(h.key(ftxui::Event::Character("x")));
        screen = h.draw(kSettingsWidthWithSidebar);
        CHECK(find(screen, "fila 01").x < 0);
        REQUIRE(h.key(ftxui::Event::PageUp));
        screen = h.draw(kSettingsWidthWithSidebar);
        CHECK(find(screen, "fila 01").x == top.x);
        CHECK(find(screen, "fila 01").y == top.y);
    }
    SECTION("La rueda sobre el campo mueve la vista") {
        REQUIRE(h.mouse(top, ftxui::Mouse::WheelDown));
        screen = h.draw(kSettingsWidthWithSidebar);
        CHECK(find(screen, "fila 01").x < 0);
        CHECK(find(screen, "fila 04").y == top.y);
    }
    SECTION("Clic en el campo coloca el cursor") {
        const Position third = find(screen, "fila 03");
        REQUIRE(third.x >= 0);
        REQUIRE(h.mouse({third.x + 4, third.y}, ftxui::Mouse::Left));
        REQUIRE(h.key(ftxui::Event::Character("·")));
        CHECK(find(h.draw(kSettingsWidthWithSidebar), "fila· 03").x >= 0); // Antes del espacio.
    }
}

TEST_CASE("configuración: un \\t de config.json se ve como 4 espacios y se guarda intacto",
          "[ajustes][sistema][ajuste]") {
    // resolve_system_prompt no lo cambia y validate_system_prompt acepta "\t":
    // así llega al campo un system_prompt escrito a mano en config.json.
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-0", {}, "uno\tdos");
    h.go_to_prompt();
    auto screen = h.draw(kSettingsWidthWithSidebar);
    CHECK(find(screen, "uno    dos").x >= 0);       // Un glifo de 4 celdas.
    CHECK(find(screen, "\xEF\xBF\xBD").x < 0);       // Nunca U+FFFD.
    CHECK(find(screen, "carácter de control").x < 0); // Sin aviso.
    CHECK(find(screen, "7 / 8000 bytes").x >= 0);
    // ← y → lo saltan entero.
    REQUIRE(h.key(ftxui::Event::End));
    REQUIRE(h.key(ftxui::Event::ArrowLeft));
    REQUIRE(h.key(ftxui::Event::ArrowLeft));
    REQUIRE(h.key(ftxui::Event::ArrowLeft));
    REQUIRE(h.key(ftxui::Event::ArrowLeft)); // Antes del "\t".
    REQUIRE(h.key(ftxui::Event::Character("!")));
    h.save();
    CHECK(h.prompt_saves == 1);
    CHECK(h.saved_prompt == std::string{"uno!\tdos"});
}

namespace {

/// Abre la lista de proveedores, va a la fila up antes de la última
/// ("Agregar endpoint…") y elige con Enter.
void choose_provider(Harness& h, int up) {
    REQUIRE(h.key(ftxui::Event::Tab)); // Al campo Proveedor.
    REQUIRE(h.key(ftxui::Event::Return));
    REQUIRE(h.key(ftxui::Event::End));
    for (int i = 0; i < up; ++i) {
        REQUIRE(h.key(ftxui::Event::ArrowUp));
    }
    REQUIRE(h.key(ftxui::Event::Return));
}

bool focused(const Harness& h, std::string_view field) {
    for (const auto& [name, value] : h.settings.debug_focus()) {
        if (name == field) {
            return value;
        }
    }
    return false;
}

} // namespace

TEST_CASE("configuración: un endpoint con Nombre y [Borrar endpoint] cabe en 80×24 y angosta",
          "[ajustes][endpoints]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    for (const int width : {80, 60}) {
        INFO("ancho " << width);
        Harness h;
        h.open("modelo-1");
        const ftxui::Screen screen = h.draw(width, 24);
        const Position name = find(screen, "Nombre");
        REQUIRE(name.x >= 0);
        CHECK(find(screen, "Proveedor ").y < name.y);
        CHECK(find(screen, "URL base").y == name.y + 1);
        CHECK(find(screen, "Servidor del lab").x >= 0);
        CHECK(find(screen, "API key").x >= 0);
        CHECK(find(screen, "Modelo ").x >= 0);
        CHECK(find(screen, "[ Guardar ]").x >= 0);
        {
            const Position button = find(screen, "[ Borrar endpoi"); // A 60, recortado.
            REQUIRE(button.x >= 0);
            CHECK(button.y == find(screen, "API key").y + 1);
            // El que está en uso, atenuado.
            CHECK(screen.CellAt(button.x + 2, button.y).foreground_color ==
                  chatbot::cli::to_ftxui(h.palette.theme().notice));
        }
    }
}

TEST_CASE("configuración: la lista de proveedores trae los endpoints y Agregar endpoint…",
          "[ajustes][endpoints]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.more_endpoints = {{"falso-b", "Falso B", "https://b.example/v1"}};
    h.open("modelo-1");
    REQUIRE(h.key(ftxui::Event::Tab));
    REQUIRE(h.key(ftxui::Event::Return)); // Abierta.
    REQUIRE(h.key(ftxui::Event::End));    // La lista se desplaza hasta el final.
    const ftxui::Screen screen = h.draw(100, 40);
    const Position lab = find(screen, "Servidor del lab");
    const Position b = find(screen, "Falso B  falso-b"); // Con su id atenuado.
    const Position add = find(screen, "Agregar endpoint…");
    REQUIRE(lab.x >= 0);
    REQUIRE(b.x >= 0);
    REQUIRE(add.x >= 0);
    CHECK(find(screen, "Ollama (local)").y < b.y);
    CHECK(b.y < add.y);
    CHECK(screen.CellAt(b.x + 9, b.y).foreground_color ==
          chatbot::cli::to_ftxui(h.palette.theme().notice));
}

TEST_CASE("configuración: Agregar endpoint… deja los campos vacíos con el foco en Nombre",
          "[ajustes][endpoints]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.open("modelo-1");
    choose_provider(h, 0); // "Agregar endpoint…".
    CHECK(focused(h, "name"));
    ftxui::Screen screen = h.draw(80, 24);
    CHECK(find(screen, "nombre del endpoint").x >= 0); // El placeholder: vacío.
    CHECK(find(screen, "https://mi-servidor/v1").x >= 0);
    CHECK(find(screen, "[ Borrar endpoint ]").x < 0);
    CHECK(find(screen, "[ Guardar ]").x >= 0);
    for (const char* c : {"M", "i", " ", "G", "W"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // A la URL.
    for (const char c : std::string{"https://gw.example/v1"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // A la key.
    for (const char c : std::string{"sk-clave-del-gateway-0000"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // Filtro.
    REQUIRE(h.key(ftxui::Event::Tab)); // Actualizar.
    REQUIRE(h.key(ftxui::Event::Tab)); // Modelo.
    for (const char c : std::string{"qwen3"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    h.save();
    CHECK(h.saved_provider == "custom:mi-gw");
    CHECK(h.saved_model == "qwen3");
}

TEST_CASE("configuración: Agregar endpoint… pide modelos con un proveedor desconocido en "
          "config.json",
          "[ajustes][endpoints]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    const std::string provider = GENERATE("custom", "custom:no-existe");
    CAPTURE(provider);
    const chatbot_test::SessionFakes fakes;
    Harness h;
    h.base = fakes.load_with_provider(provider); // Como main: session.config().
    h.open("modelo-1");
    h.model_urls.clear();
    choose_provider(h, 0); // "Agregar endpoint…".
    for (const char* c : {"M", "i", " ", "G", "W"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // A la URL.
    for (const char c : std::string{"https://gw.example/v1"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // A la key.
    for (const char c : std::string{"sk-clave-del-gateway-0000"}) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
    REQUIRE(h.key(ftxui::Event::Tab)); // Filtro.
    REQUIRE(h.key(ftxui::Event::Tab)); // Actualizar.
    REQUIRE(h.key(ftxui::Event::Return));
    REQUIRE(h.queue.run_until([&h] { return !h.loader.busy(); }));
    CHECK(h.model_urls == std::vector<std::string>{"https://gw.example/v1/models"});
    const ftxui::Screen screen = h.draw(100, 40);
    CHECK(find(screen, "modelo-0").x >= 0);
    CHECK(find(screen, h.base.provider_error.substr(0, 20)).x < 0); // Ni el error de config.json.
}

TEST_CASE("configuración: [Borrar endpoint] pregunta con s/n", "[ajustes][endpoints]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.more_endpoints = {{"falso-b", "Falso B", "https://b.example/v1"}};
    std::vector<std::string> deleted;
    h.settings.set_on_delete_endpoint([&](const std::string& id) {
        deleted.push_back(id);
        chatbot::cli::SettingsScreen::EndpointDeleted result;
        if (id == "custom:lab") {
            result.message = "Cambia de proveedor antes de borrar este endpoint.";
            return result;
        }
        result.message = "Se borró el endpoint Falso B.";
        chatbot::ConfigFileValues values;
        values.provider = "custom:lab";
        values.model = "modelo-1";
        values.endpoints = {{"lab", "Servidor del lab", h.base_url}};
        result.form.emplace(values, chatbot::Credentials{}, chatbot::cli::SettingsEnv{});
        return result;
    });
    h.open("modelo-1");
    const auto press_delete = [&h] {
        REQUIRE(h.key(ftxui::Event::Tab)); // Nombre.
        REQUIRE(h.key(ftxui::Event::Tab)); // URL base.
        REQUIRE(h.key(ftxui::Event::Tab)); // API key.
        REQUIRE(h.key(ftxui::Event::Tab)); // [Borrar endpoint].
        REQUIRE(h.key(ftxui::Event::Return));
    };

    SECTION("el que está en uso: el error, sin preguntar") {
        REQUIRE(h.key(ftxui::Event::Tab));
        press_delete();
        CHECK_FALSE(h.settings.asking());
        CHECK(deleted == std::vector<std::string>{"custom:lab"});
        CHECK(find(h.draw(100), "Cambia de proveedor antes de borrar este endpoint.").x >= 0);
    }
    SECTION("otro: pregunta; Esc cancela con aviso, s lo borra") {
        choose_provider(h, 1);
        press_delete();
        REQUIRE(h.settings.asking());
        CHECK(find(h.draw(100), "¿Borrar el endpoint Falso B? (s/n)").x >= 0);
        CHECK(h.key(ftxui::Event::Character("x"))); // Se ignora.
        CHECK(h.key(ftxui::Event::Return));          // También.
        REQUIRE(h.settings.asking());
        REQUIRE(h.key(ftxui::Event::Escape));
        CHECK_FALSE(h.settings.asking());
        CHECK(deleted.empty());
        CHECK(find(h.draw(100), "Cancelado: no se borró el endpoint.").x >= 0);
        CHECK(h.settings.is_open());

        REQUIRE(h.key(ftxui::Event::Return)); // El foco sigue en el botón.
        REQUIRE(h.settings.asking());
        REQUIRE(h.key(ftxui::Event::Character("n")));
        CHECK_FALSE(h.settings.asking());
        CHECK(deleted.empty());

        REQUIRE(h.key(ftxui::Event::Return));
        REQUIRE(h.key(ftxui::Event::Character("s")));
        CHECK(deleted == std::vector<std::string>{"custom:falso-b"});
        const ftxui::Screen screen = h.draw(100);
        CHECK(find(screen, "Se borró el endpoint Falso B.").x >= 0);
        CHECK(find(screen, "Servidor del lab").x >= 0); // De regreso al guardado.
    }
}

namespace {

/// El modelo "modelo-0" del endpoint "lab", con estos ajustes guardados.
chatbot::cli::ModelParamsPage lab_model(chatbot::ModelParams saved = {}) {
    chatbot::cli::ModelParamsPage page;
    page.problem.reset();
    page.model = "modelo-0";
    page.provider = "custom:lab";
    page.provider_label = "Servidor del lab";
    page.saved = std::move(saved);
    return page;
}

/// En Configuración del modelo, pasa a la pestaña tab (0, 1 o 2) con ←/→
/// desde la barra; el foco se queda en la barra.
void go_to_tab(Harness& h, int tab) {
    h.go_to_model();
    REQUIRE(h.key(ftxui::Event::Tab)); // La barra de pestañas.
    for (int i = 0; i < tab; ++i) {
        REQUIRE(h.key(ftxui::Event::ArrowRight));
    }
}

void type(Harness& h, const std::string& text) {
    for (const char c : text) {
        REQUIRE(h.key(ftxui::Event::Character(c)));
    }
}

} // namespace

TEST_CASE("configuración del modelo: las tres pestañas en 80×24", "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    chatbot::ModelParams saved;
    saved.temperature = 0.6;
    saved.reasoning = chatbot::Reasoning::High;
    saved.extra = R"({"chat_template_kwargs":{"thinking":true}})";
    h.model_page = lab_model(saved);
    h.open("modelo-0");
    go_to_tab(h, 0);
    auto screen = h.draw(80, 24);
    CHECK(find(screen, "Configuración del modelo — modelo-0 (Servidor del").x >= 0);
    CHECK(find(h.draw(), "Configuración del modelo — modelo-0 (Servidor del lab)").x >= 0);
    CHECK(find(screen, "[Instrucciones]").x >= 0);
    CHECK(find(screen, " Parámetros ").x >= 0);
    CHECK(find(screen, "Para todos los modelos").x >= 0);
    CHECK(find(screen, "[ Restaurar predeterminado ]").x >= 0);
    CHECK(find(screen, "[ Guardar ]").x >= 0);

    REQUIRE(h.key(ftxui::Event::ArrowRight));
    screen = h.draw(80, 24);
    CHECK(find(screen, "[Parámetros]").x >= 0);
    for (const char* text : {"Razonamiento", "Alto", "Temperatura", "0.6", "Top P", "Top K",
                             "1 a 1000", "No todos los proveedores lo", "Máx. tokens",
                             "Pen. frecuencia", "Pen. presencia", "Semilla", "0 a 2147483647",
                             "[ Restaurar todo ]", "[ Guardar ]"}) {
        INFO(text);
        CHECK(find(screen, text).x >= 0);
    }

    REQUIRE(h.key(ftxui::Event::ArrowRight));
    screen = h.draw(80, 24);
    CHECK(find(screen, "[Avanzado]").x >= 0);
    CHECK(find(screen, "\"chat_template_kwargs\": {").x >= 0);
    CHECK(find(screen, "\"model\": \"modelo-0\",").x >= 0);
    CHECK(find(screen, "\"messages\": \"[…2 mensajes…]\",").x >= 0);
    CHECK(find(screen, "\"temperature\": 0.6,").x >= 0);
    CHECK(find(screen, "[ Guardar ]").x >= 0);

    // Angosta: lo de arriba no se pierde y los botones se ven.
    screen = h.draw(60, 24);
    CHECK(find(screen, "[Avanzado]").x >= 0);
    CHECK(find(screen, "[ Guardar ]").x >= 0);
}

TEST_CASE("configuración del modelo: clic en una pestaña", "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.model_page = lab_model();
    h.open("modelo-0");
    h.go_to_model();
    const Position tab = find(h.draw(), " Avanzado ");
    REQUIRE(tab.x >= 0);
    REQUIRE(h.mouse({tab.x + 1, tab.y}, ftxui::Mouse::Left));
    CHECK(find(h.draw(), "[Avanzado]").x >= 0);
}

TEST_CASE("configuración del modelo: sin modelo, Parámetros y Avanzado lo dicen",
          "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h; // model_page sin modelo.
    h.open("modelo-0");
    go_to_tab(h, 1);
    auto screen = h.draw();
    CHECK(find(screen, "Elige un modelo en Proveedor de IA (o con /model) para ajustarlo.").x >= 0);
    CHECK(find(screen, "Temperatura").x < 0);
    REQUIRE(h.key(ftxui::Event::ArrowRight));
    CHECK(find(h.draw(), "Elige un modelo en Proveedor de IA (o con /model) para ajustarlo.").x >=
          0);
    // Las instrucciones siguen funcionando.
    REQUIRE(h.key(ftxui::Event::ArrowLeft));
    REQUIRE(h.key(ftxui::Event::ArrowLeft));
    CHECK(find(h.draw(), "[Instrucciones]").x >= 0);
}

TEST_CASE("configuración del modelo: errores junto al campo y Guardar", "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.model_page = lab_model();
    h.open("modelo-0");
    go_to_tab(h, 1);
    REQUIRE(h.key(ftxui::Event::Tab)); // Razonamiento.
    REQUIRE(h.key(ftxui::Event::Tab)); // Temperatura.
    type(h, "abc");
    CHECK(find(h.draw(), "Escribe un número.").x >= 0);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(h.key(ftxui::Event::Backspace));
    }
    type(h, "5");
    CHECK(find(h.draw(), "Debe ser un número entre 0 y 2.").x >= 0);
    h.save();
    CHECK(h.params_saves == 0);
    CHECK(find(h.draw(), "Revisa los ajustes marcados en Configuración del modelo.").x >= 0);
    CHECK(h.closes == 0);

    // Guardar se llevó el foco: de vuelta al campo con un clic.
    const Position field = find(h.draw(), "5 ");
    REQUIRE(field.x >= 0);
    REQUIRE(h.mouse(field, ftxui::Mouse::Left));
    REQUIRE(h.key(ftxui::Event::End));
    REQUIRE(h.key(ftxui::Event::Backspace));
    type(h, "0,3"); // También con coma.
    REQUIRE(h.key(ftxui::Event::Tab)); // Top P.
    type(h, "0.9");
    CHECK(find(h.draw(), "Debe ser").x < 0);
    h.save();
    REQUIRE(h.params_saves == 1);
    CHECK(h.saved_params->temperature == 0.3);
    CHECK(h.saved_params->top_p == 0.9);
    CHECK_FALSE(h.saved_params->top_k.has_value());
    CHECK(h.closes == 1);
}

TEST_CASE("configuración del modelo: JSON avanzado con vista previa y error", "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    h.model_page = lab_model();
    h.open("modelo-0");
    go_to_tab(h, 2);
    REQUIRE(h.key(ftxui::Event::Tab)); // El JSON.
    REQUIRE(h.settings.paste_target() == chatbot::cli::PasteTarget::MultiLine);
    REQUIRE(h.key(ftxui::Event::Character(R"({"model": "x"})")));
    auto screen = h.draw();
    CHECK(find(screen, "«model» la pone Moss; no se puede cambiar desde el JSON avanzado.").x >= 0);
    CHECK(find(screen, "Corrige los errores para ver el cuerpo.").x >= 0);
    h.save();
    CHECK(h.params_saves == 0);

    const Position field = find(h.draw(), R"({"model": "x"})");
    REQUIRE(field.x >= 0);
    REQUIRE(h.mouse({field.x + 14, field.y}, ftxui::Mouse::Left));
    REQUIRE(h.key(ftxui::Event::End));
    for (int i = 0; i < 14; ++i) {
        REQUIRE(h.key(ftxui::Event::Backspace));
    }
    REQUIRE(h.key(ftxui::Event::Character(R"({"repetition_penalty": 1.1})")));
    screen = h.draw();
    CHECK(find(screen, "\"repetition_penalty\": 1.1").x >= 0);
    h.save();
    REQUIRE(h.params_saves == 1);
    CHECK(h.saved_params->extra == R"({"repetition_penalty": 1.1})");
}

TEST_CASE("configuración del modelo: Restaurar todo deja el modelo sin ajustes",
          "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    chatbot::ModelParams saved;
    saved.seed = 7;
    saved.reasoning = chatbot::Reasoning::Off;
    saved.extra = R"({"a":1})";
    h.model_page = lab_model(saved);
    h.open("modelo-0");
    go_to_tab(h, 1);
    CHECK(find(h.draw(), "[ Apagado").x >= 0);
    const Position restore = find(h.draw(), "[ Restaurar todo ]");
    REQUIRE(restore.x >= 0);
    REQUIRE(h.mouse({restore.x + 2, restore.y}, ftxui::Mouse::Left));
    auto screen = h.draw();
    CHECK(find(screen, "[ Apagado").x < 0);
    CHECK(find(screen, "[ Predeterminado").x >= 0);
    h.save();
    REQUIRE(h.params_saves == 1);
    CHECK(h.saved_params->empty());
}

TEST_CASE("configuración del modelo: sin cambios no se guarda y descartar pregunta",
          "[ajustes][modelo]") {
    const chatbot_test::NoColorGuard environment(nullptr);
    Harness h;
    chatbot::ModelParams saved;
    saved.top_k = 40;
    h.model_page = lab_model(saved);
    h.open("modelo-0");
    h.save();
    CHECK(h.params_saves == 0);
    CHECK(h.closes == 1);

    h.open("modelo-0");
    go_to_tab(h, 1);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(h.key(ftxui::Event::Tab)); // Razonamiento, Temperatura, Top P, Top K.
    }
    type(h, "1");
    CHECK_FALSE(h.settings.request_close());
    CHECK(find(h.draw(), "¿Descartar los cambios? (s/n)").x >= 0);
    REQUIRE(h.key(ftxui::Event::Character("s")));
    CHECK(h.closes == 2);
    CHECK(h.params_saves == 0);
}
