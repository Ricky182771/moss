// La pantalla de Moss Code (cli/src/code_screen.h) sin terminal: CodeMode con clientes falsos
// (code_mode_fakes.hpp), dibujada en una ftxui::Screen y con eventos mandados a component().
// Sin red; la cola de la prueba hace de hilo de la interfaz.

#include "code_screen.h"

#include "blocking_transport.hpp"
#include "code_mode_fakes.hpp"
#include "fake_transport.hpp"

#include "chatbot/credentials.h"
#include "chatbot/platform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using chatbot::cli::EntryKind;
using chatbot_test::answer;
using chatbot_test::calls;
using chatbot_test::chunk;
using chatbot_test::kDone;
using chatbot_test::ok;

/// Un CodeMode con su pantalla, como run_code_screen pero sin la terminal.
struct ScreenHarness {
    chatbot_test::CodeModeHarness code;
    chatbot::cli::Appearance appearance = chatbot::cli::resolve_appearance("", "");
    chatbot::cli::Palette palette{appearance};
    std::unique_ptr<chatbot::cli::ModelsLoader> loader;
    std::unique_ptr<chatbot::cli::CodeScreen> screen;
    int width = 100;
    int height = 30;

    ScreenHarness() {
        // El título con "~/proy".
        code.display = chatbot_test::utf8(std::filesystem::canonical(code.projects.path()));
    }

    void open(const chatbot::Config& config) {
        code.on_finished = [this](std::optional<std::string> restored) {
            screen->finished(std::move(restored));
        };
        code.start(config);
        const chatbot::cli::RequestRunner::Post post = [this](std::function<void()> task) {
            code.queue.post(std::move(task));
        };
        loader = std::make_unique<chatbot::cli::ModelsLoader>(
            [] {
                auto transport = std::make_unique<chatbot_test::FakeTransport>();
                transport->responses.push_back(ok(R"({"data":[{"id":"modelo-de-prueba"}]})"));
                return transport;
            },
            post);
        screen = std::make_unique<chatbot::cli::CodeScreen>(chatbot::cli::CodeScreenContext{
            .mode = *code.mode,
            .models_loader = *loader,
            .appearance = appearance,
            .palette = palette,
            .detected_symbols = chatbot::cli::Symbols::Unicode,
            .post = post,
            // La lista del selector de /model: un modelo, sin red.
            .picker_transport =
                [] {
                    auto transport = std::make_unique<chatbot_test::FakeTransport>();
                    transport->responses.push_back(ok(R"({"data":[{"id":"modelo-elegido"}]})"));
                    return transport;
                },
            .app = nullptr,
            .size = [this] { return ftxui::Dimensions{width, height}; }});
        screen->start("");
    }

    void open() { open(chatbot_test::test_config()); }

    bool event(ftxui::Event event) { return screen->component()->OnEvent(std::move(event)); }

    /// Escribe text en la caja y Enter.
    void type_and_send(const std::string& text) {
        screen->input().set_text(text);
        (void)event(ftxui::Event::Return);
    }

    /// Las filas de la pantalla dibujada, sin espacios al final.
    std::vector<std::string> draw() {
        ftxui::Screen drawn(width, height);
        ftxui::Render(drawn, screen->component()->Render());
        std::vector<std::string> rows;
        for (int y = 0; y < height; ++y) {
            std::string row;
            for (int x = 0; x < width; ++x) {
                const std::string& character = drawn.CellAt(x, y).character;
                row += character.empty() ? std::string{" "} : character;
            }
            row.erase(row.find_last_not_of(' ') + 1);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    // Antes que el CodeMode (en code): la pantalla y el cargador de F2.
    ~ScreenHarness() {
        screen.reset();
        loader.reset();
    }
    ScreenHarness(const ScreenHarness&) = delete;
    ScreenHarness& operator=(const ScreenHarness&) = delete;
    ScreenHarness(ScreenHarness&&) = delete;
    ScreenHarness& operator=(ScreenHarness&&) = delete;
};

/// La primera fila que contiene needle, o -1.
int row_with(const std::vector<std::string>& rows, const std::string& needle) {
    for (std::size_t y = 0; y < rows.size(); ++y) {
        if (rows[y].find(needle) != std::string::npos) {
            return static_cast<int>(y);
        }
    }
    return -1;
}

bool shows(const std::vector<std::string>& rows, const std::string& needle) {
    return row_with(rows, needle) >= 0;
}

} // namespace

TEST_CASE("pantalla de Moss Code: tres pasos con tareas, a 100 y 80 columnas",
          "[code_screen]") {
    ScreenHarness h;
    h.width = GENERATE(100, 80);
    INFO(h.width << " columnas");
    h.code.fakes.responses = {
        calls({{"glob", R"({"pattern":"*.txt"})"}}, "Voy a buscar."),
        calls({{"read", R"({"path":"a.txt"})"}}),
        calls({{"todo_write",
                R"({"todos":[{"content":"Leer","status":"completed","active_form":"Leyendo"},)"
                R"({"content":"Responder","status":"in_progress","active_form":"Respondiendo"}]})"}},
              "Anoto."),
        answer("Hay dos archivos."),
    };
    h.open();
    h.type_and_send("hola");
    CHECK(h.screen->input().text().empty());
    REQUIRE(h.code.wait_turn(0));

    const std::vector<std::string> rows = h.draw();
    // Título sin [Exportar], con la ayuda del modo código.
    if (chatbot::current_os() == chatbot::Os::Windows) {
        // En Windows la carpeta va completa y, tan larga, puede no dejar lugar a la ayuda.
        CHECK(rows[0].starts_with("Moss Code — "));
    } else {
        CHECK(rows[0].starts_with("Moss Code — ~/proy — modelo-de-prueba"));
        CHECK(rows[0].ends_with("Ctrl+O salida completa"));
    }
    CHECK(rows[0].find("[Exportar]") == std::string::npos);
    // Historial: el arranque, el mensaje, las líneas de herramienta y la respuesta.
    CHECK(shows(rows, "Sin AGENTS.md en esta carpeta"));
    CHECK(shows(rows, "> hola"));
    CHECK(row_with(rows, "● Buscar") >= 0);
    const int read = row_with(rows, "● Leer a.txt");
    REQUIRE(read >= 0);
    CHECK(rows[static_cast<std::size_t>(read) + 1].starts_with("  ⎿ "));
    CHECK(shows(rows, "Hay dos archivos."));
    // Abajo: separador, estado, tareas y la caja.
    const auto bottom = static_cast<std::size_t>(h.height - 1);
    CHECK(rows[bottom].starts_with(">"));
    CHECK(rows[bottom - 1].find("☑ Leer") != std::string::npos);
    CHECK(rows[bottom - 1].find("◐ Respondiendo") != std::string::npos);
    CHECK(rows[bottom - 2].starts_with("Solo lectura"));
    CHECK(rows[bottom - 2].ends_with("modelo-de-prueba"));
    CHECK(rows[bottom - 2].find("Pensando") == std::string::npos);
    CHECK(rows[bottom - 3].starts_with("─"));
}

TEST_CASE("pantalla de Moss Code: sin tareas, la lista no ocupa filas", "[code_screen]") {
    ScreenHarness h;
    h.open();
    const std::vector<std::string> rows = h.draw();
    const auto bottom = static_cast<std::size_t>(h.height - 1);
    CHECK(rows[bottom].starts_with(">"));
    CHECK(rows[bottom - 1].starts_with("Solo lectura"));
}

TEST_CASE("pantalla de Moss Code: Ctrl+O, Ctrl+T y el clic en la línea ●", "[code_screen]") {
    ScreenHarness h;
    h.code.fakes.responses = {
        calls({{"read", R"({"path":"a.txt"})"}, {"read", R"({"path":"b.txt"})"}}),
        ok(chunk({{"reasoning_content", "pienso despacio"}}) + chunk({{"content", "Listo."}}) +
           kDone),
    };
    h.open();
    h.type_and_send("hola");
    REQUIRE(h.code.wait_turn(0));
    const auto& entries = h.code.entries();
    REQUIRE(entries[3].kind == EntryKind::Tool);
    CHECK_FALSE(shows(h.draw(), "gamma"));

    // Ctrl+O: la salida de la última herramienta (b.txt), y otra vez la cierra.
    CHECK(h.event(ftxui::Event::CtrlO));
    CHECK(entries[3].tool.expanded);
    CHECK(shows(h.draw(), "gamma"));
    CHECK(h.event(ftxui::Event::CtrlO));
    CHECK_FALSE(entries[3].tool.expanded);
    CHECK_FALSE(shows(h.draw(), "gamma"));

    // Ctrl+T: el pensamiento de todas las respuestas.
    CHECK_FALSE(shows(h.draw(), "pienso despacio"));
    CHECK(h.event(ftxui::Event::CtrlT));
    CHECK(shows(h.draw(), "pienso despacio"));
    CHECK(h.event(ftxui::Event::CtrlT));
    CHECK_FALSE(shows(h.draw(), "pienso despacio"));

    // Clic en la línea ● de a.txt: abre su salida.
    const int line = row_with(h.draw(), "● Leer a.txt");
    REQUIRE(line >= 0);
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x = 2;
    mouse.y = line;
    CHECK(h.event(ftxui::Event::Mouse("", mouse)));
    CHECK(entries[2].tool.expanded);
    CHECK(shows(h.draw(), "alfa"));
}

TEST_CASE("pantalla de Moss Code: Esc cancela el turno y el texto vuelve a la caja",
          "[code_screen][cancelar]") {
    ScreenHarness h;
    chatbot_test::BlockingTransport* blocking = nullptr;
    h.code.fakes.block(blocking);
    h.open();
    h.type_and_send("hola");
    CHECK(h.screen->input().text().empty());
    REQUIRE(h.code.queue.run_until(
        [&h] { return h.code.entries().back().kind == EntryKind::Assistant; }));
    const auto status = static_cast<std::size_t>(h.height - 2);
    CHECK(h.draw()[status].find("Pensando… (Esc para cancelar)") != std::string::npos);

    // Shift+Tab no hace nada (los modos son de C3).
    CHECK(h.event(ftxui::Event::TabReverse));
    CHECK(h.code.mode->busy());

    CHECK(h.event(ftxui::Event::Escape));
    REQUIRE(h.code.wait_turn(0));
    CHECK(h.screen->input().text() == "hola");
    CHECK(h.draw()[status].find("Pensando") == std::string::npos);
}

TEST_CASE("pantalla de Moss Code: sin proveedor abre F2 con el aviso", "[code_screen][f2]") {
    ScreenHarness h;
    h.open(h.code.fakes.load()); // Sin config.json ni credentials.json.
    REQUIRE(h.screen->settings().is_open());
    CHECK(shows(h.draw(), std::string{chatbot::cli::kSetupNotice}));

    // Al cerrarla, la caja lo dice y Enter no envía: el texto se queda.
    CHECK(h.event(ftxui::Event::F2));
    REQUIRE_FALSE(h.screen->settings().is_open());
    CHECK(shows(h.draw(), std::string{chatbot::cli::kSetupNotice} + " (F2)"));
    h.type_and_send("hola");
    CHECK(h.screen->input().text() == "hola");
    CHECK(h.screen->flash() == chatbot::cli::kSetupNotice);
    CHECK(h.code.kinds() == std::vector<EntryKind>{EntryKind::Notice});
}

TEST_CASE("pantalla de Moss Code: con un turno en curso F2 no se abre", "[code_screen][f2]") {
    ScreenHarness h;
    chatbot_test::BlockingTransport* blocking = nullptr;
    h.code.fakes.block(blocking);
    h.open();
    h.type_and_send("hola");
    REQUIRE(h.code.mode->busy());
    CHECK(h.event(ftxui::Event::F2));
    CHECK_FALSE(h.screen->settings().is_open());
    CHECK(h.screen->flash() == chatbot::cli::kWaitOrCancel);
    CHECK(shows(h.draw(), std::string{chatbot::cli::kWaitOrCancel}));

    CHECK(h.event(ftxui::Event::Escape));
    REQUIRE(h.code.wait_turn(0));
    // Sin turno, F2 sí abre.
    CHECK(h.event(ftxui::Event::F2));
    CHECK(h.screen->settings().is_open());
}

TEST_CASE("pantalla de Moss Code: cerrar F2 recalcula los secretos que se tapan",
          "[code_screen][f2]") {
    // F2 → Addons guarda el token en credentials.json sin on_saved: cerrar F2 lo recoge.
    const std::string token = "tg-token-secreto-de-prueba-5678";
    ScreenHarness h;
    chatbot_test::write_file(h.code.project / "s.txt", "token=" + token + "\n");
    h.code.fakes.responses = {calls({{"read", R"({"path":"s.txt"})"}}), answer("Listo.")};
    h.open();
    CHECK(h.event(ftxui::Event::F2));
    REQUIRE(h.screen->settings().is_open());
    chatbot::Credentials credentials;
    credentials.keys["telegram"] = token;
    REQUIRE_FALSE(
        chatbot::save_credentials(h.code.fakes.credentials_path(), credentials).has_value());
    CHECK(h.event(ftxui::Event::F2));
    REQUIRE_FALSE(h.screen->settings().is_open());

    h.type_and_send("lee s.txt");
    REQUIRE(h.code.wait_turn(0));
    CHECK(h.code.entries().back().text == "Listo.");
    REQUIRE(h.code.fakes.chat != nullptr);
    REQUIRE(h.code.fakes.chat->requests.size() == 2);
    const std::string& body = h.code.fakes.chat->requests.back().body;
    CHECK(body.find("token=") != std::string::npos); // El resultado de read sí va.
    CHECK(body.find(token) == std::string::npos);
}

TEST_CASE("pantalla de Moss Code: el menú de / muestra solo los comandos del modo código",
          "[code_screen][comandos]") {
    ScreenHarness h;
    h.open();
    h.screen->input().set_text("/");
    const std::vector<std::string> rows = h.draw();
    REQUIRE(h.screen->command_menu().is_open());
    CHECK(h.screen->command_menu().matches().size() == 3);
    const int model = row_with(rows, "/model [proveedor] [modelo]");
    REQUIRE(model >= 0);
    CHECK(rows[static_cast<std::size_t>(model) + 1].starts_with("  /clear"));
    CHECK(rows[static_cast<std::size_t>(model) + 2].starts_with("  /ayuda"));
    for (const char* chat : {"/buscar", "/copiar", "/guardar", "/exportar"}) {
        INFO(chat);
        CHECK_FALSE(shows(rows, chat));
    }
    // El menú va entre la línea de estado y la caja.
    CHECK(rows[static_cast<std::size_t>(h.height - 1)].starts_with("> /"));
    CHECK(rows[static_cast<std::size_t>(model) - 2].starts_with("Solo lectura"));
}

TEST_CASE("pantalla de Moss Code: Esc cierra el menú sin detener el turno; el segundo, sí",
          "[code_screen][comandos][cancelar]") {
    ScreenHarness h;
    chatbot_test::BlockingTransport* blocking = nullptr;
    h.code.fakes.block(blocking);
    h.open();
    h.type_and_send("hola");
    REQUIRE(h.code.mode->busy());
    h.screen->input().set_text("/");
    (void)h.draw();
    REQUIRE(h.screen->command_menu().is_open());
    CHECK(h.event(ftxui::Event::Escape));
    CHECK_FALSE(h.screen->command_menu().is_open());
    CHECK(h.code.mode->busy());
    CHECK(h.code.finished.empty());
    CHECK(h.event(ftxui::Event::Escape));
    REQUIRE(h.code.wait_turn(0));
    CHECK_FALSE(h.code.mode->busy());
}

TEST_CASE("pantalla de Moss Code: /ayuda y Enter deja la ayuda en el historial",
          "[code_screen][comandos]") {
    ScreenHarness h;
    h.open();
    h.type_and_send("/ayuda"); // Enter con el menú abierto lo envía (no lleva argumentos).
    CHECK(h.screen->input().text().empty());
    REQUIRE(h.code.entries().back().kind == EntryKind::Notice);
    CHECK(h.code.entries().back().text == chatbot::cli::code_help_text());
    CHECK(shows(h.draw(), "Ctrl+O · abre o cierra la salida completa de la última herramienta"));
}

TEST_CASE("pantalla de Moss Code: /model abre el selector y elegir cambia el modelo",
          "[code_screen][comandos]") {
    ScreenHarness h;
    h.code.save_nvidia();
    h.code.fakes.responses = {answer("Hecho.")};
    h.open(h.code.fakes.load());
    h.type_and_send("/model nvidia");
    REQUIRE(h.screen->model_picker().is_open());
    CHECK(h.screen->input().text().empty());
    REQUIRE(h.code.queue.run_until([&h] {
        return h.screen->model_picker().models_state() == chatbot::cli::ModelsState::Loaded;
    }));
    CHECK(shows(h.draw(), "modelo-elegido"));
    CHECK(h.event(ftxui::Event::Return));
    CHECK_FALSE(h.screen->model_picker().is_open());
    REQUIRE(h.code.entries().back().kind == EntryKind::Notice);
    CHECK(h.code.entries().back().text == "Ahora se usa modelo-elegido de NVIDIA NIM.");
    CHECK(h.code.mode->status().model == "modelo-elegido");
    // El siguiente turno usa el cliente nuevo.
    h.type_and_send("hola");
    REQUIRE(h.code.wait_turn(0));
    REQUIRE(h.code.fakes.chat != nullptr);
    CHECK(h.code.fakes.chat->requests.back().body.find("\"modelo-elegido\"") != std::string::npos);
}

TEST_CASE("pantalla de Moss Code: Esc cierra el selector de /model", "[code_screen][comandos]") {
    ScreenHarness h;
    h.code.save_nvidia();
    h.open(h.code.fakes.load());
    h.type_and_send("/model");
    REQUIRE(h.screen->model_picker().is_open());
    CHECK(h.event(ftxui::Event::Escape));
    CHECK_FALSE(h.screen->model_picker().is_open());
}
