// Vista del selector de /model: filas, marcas, estados, recorte y cajas.

#include "model_picker.h"
#include "model_picker_view.h"
#include "theme.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using chatbot::cli::ListKey;
using chatbot::cli::ModelPicker;
using chatbot::cli::ProviderChoice;

std::vector<ProviderChoice> choices() {
    return {
        {"nvidia", "NVIDIA NIM", true, true, false, true, ""},
        {"openrouter", "OpenRouter", false, false, false, false, "sin key"},
        {"ollama", "Ollama (local)", false, false, true, true, ""},
        {"custom", "Personalizado…", false, false, false, false, "falta la URL"},
    };
}

struct Harness {
    chatbot_test::TaskQueue queue;
    std::string body = R"({"data":[{"id":"llama3"},{"id":"qwen3:8b"}]})";
    ModelPicker picker{[](std::string_view) -> chatbot::Result<chatbot::Config> {
                           chatbot::Config config;
                           config.base_url = "http://localhost:11434/v1";
                           return config;
                       },
                       [this] {
                           auto fake = std::make_unique<chatbot_test::FakeTransport>();
                           chatbot::HttpResponse response;
                           response.status = 200;
                           response.body = body;
                           fake->responses = {response};
                           return fake;
                       },
                       [this](chatbot::cli::ModelsLoader::Task task) {
                           queue.post(std::move(task));
                       }};
};

ftxui::Screen draw(const ModelPicker& picker, int width, const chatbot::cli::Palette& palette,
                   std::vector<ftxui::Box>* boxes = nullptr) {
    ftxui::Element element = chatbot::cli::render_model_picker(picker, width, palette, boxes);
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                                 ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    return screen;
}

std::vector<std::string> rows_of(const ftxui::Screen& screen) {
    std::vector<std::string> rows;
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string row;
        for (int x = 0; x < screen.dimx(); ++x) {
            const std::string& character = screen.CellAt(x, y).character;
            row += character.empty() ? std::string{" "} : character;
        }
        rows.push_back(row);
    }
    return rows;
}

bool contains(const std::string& row, const std::string& text) {
    return row.find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Selector de /model: proveedores con marca y estado", "[model_picker_view]") {
    Harness h;
    (void)h.picker.open(choices(), std::nullopt, "m1");
    const chatbot::cli::Palette palette{chatbot::cli::default_theme()};
    std::vector<ftxui::Box> boxes;
    const ftxui::Screen screen = draw(h.picker, 80, palette, &boxes);
    const std::vector<std::string> rows = rows_of(screen);
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].find("────") == 0);
    CHECK(contains(rows[1], "Elige un proveedor · Esc cierra"));
    CHECK(rows[2].find("  ● nvidia") == 0);
    CHECK(contains(rows[2], "NVIDIA NIM"));
    CHECK(contains(rows[2], "key guardada"));
    CHECK(rows[3].find("    openrouter") == 0);
    CHECK(contains(rows[3], "sin key"));
    CHECK(contains(rows[4], "local"));
    CHECK(contains(rows[5], "falta la URL"));
    // El cursor en el actual; los que no se eligen, atenuados.
    CHECK(screen.CellAt(2, 2).background_color ==
          chatbot::cli::to_ftxui(palette.theme().selection_bg));
    CHECK(screen.CellAt(4, 3).foreground_color == chatbot::cli::to_ftxui(palette.theme().notice));
    // Una caja por fila visible, para el ratón.
    REQUIRE(boxes.size() == 4);
    CHECK(boxes[0].y_min == 2);
    CHECK(boxes[3].y_min == 5);
}

TEST_CASE("Selector de /model: modelos, Usar «…» y símbolos sencillos", "[model_picker_view]") {
    Harness h;
    const chatbot::cli::Palette simple{chatbot::cli::default_theme(),
                                       chatbot::cli::BackgroundMode::FromTheme,
                                       chatbot::cli::Symbols::Simple};
    (void)h.picker.open(choices(), std::nullopt, "llama3");
    (void)h.picker.handle(ListKey::Enter); // nvidia, el actual.
    std::vector<std::string> rows = rows_of(draw(h.picker, 60, simple));
    CHECK(contains(rows[1], "Modelos de NVIDIA NIM · Esc regresa"));
    CHECK(contains(rows[2], std::string{ModelPicker::kLoading}));
    REQUIRE(h.queue.run_until(
        [&] { return h.picker.models_state() != chatbot::cli::ModelsState::Loading; }));
    rows = rows_of(draw(h.picker, 60, simple));
    REQUIRE(rows.size() == 4);
    CHECK(rows[2].find("  • llama3") == 0);
    CHECK(rows[3].find("    qwen3:8b") == 0);
    // Los ids vienen del servidor: sin controles de terminal.
    h.body = R"({"data":[{"id":"malo\u001b[31m"}]})";
    (void)h.picker.handle(ListKey::Escape);
    (void)h.picker.handle(ListKey::Down);
    (void)h.picker.handle(ListKey::Down); // ollama
    (void)h.picker.handle(ListKey::Enter);
    REQUIRE(h.queue.run_until(
        [&] { return h.picker.models_state() != chatbot::cli::ModelsState::Loading; }));
    rows = rows_of(draw(h.picker, 60, simple));
    CHECK(contains(rows[2], "malo"));
    CHECK_FALSE(contains(rows[2], "\x1b"));
    h.picker.update("mi\x1b[31mmodelo");
    rows = rows_of(draw(h.picker, 60, simple));
    REQUIRE(rows.size() == 3);
    CHECK(rows[2].find("    Usar «") == 0);
    CHECK_FALSE(contains(rows[2], "\x1b"));
}

TEST_CASE("Selector de /model: en pantallas angostas el estado se oculta", "[model_picker_view]") {
    Harness h;
    (void)h.picker.open(choices(), std::nullopt, "m1");
    for (const int width : {30, 12, 3}) {
        INFO(width);
        const std::vector<std::string> rows =
            rows_of(draw(h.picker, width, chatbot::cli::terminal_palette()));
        REQUIRE(rows.size() == 6);
        CHECK_FALSE(contains(rows[2], "key guardada"));
    }
    const std::vector<std::string> rows =
        rows_of(draw(h.picker, 20, chatbot::cli::terminal_palette()));
    CHECK(rows[2].find("  ● nvidia") == 0);
    CHECK(contains(rows[2], "…"));
}
