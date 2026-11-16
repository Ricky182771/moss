#include "task_list_view.h"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include <string>
#include <vector>

namespace {

using chatbot::cli::TaskItem;
using chatbot::cli::TaskState;

/// Las filas dibujadas, sin los espacios del final.
std::vector<std::string> rows(const std::vector<TaskItem>& tasks, int width,
                              const chatbot::cli::Palette& palette =
                                  chatbot::cli::terminal_palette()) {
    ftxui::Element element = chatbot::cli::render_task_list(tasks, width, palette);
    element->ComputeRequirement();
    if (element->requirement().min_y == 0) {
        return {};
    }
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                                 ftxui::Dimension::Fit(element, true));
    ftxui::Render(screen, element);
    std::vector<std::string> out;
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string row;
        for (int x = 0; x < screen.dimx(); ++x) {
            const std::string& character = screen.CellAt(x, y).character;
            row += character.empty() ? std::string{" "} : character;
        }
        row.erase(row.find_last_not_of(' ') + 1);
        out.push_back(row);
    }
    return out;
}

std::vector<TaskItem> numbered(int count) {
    std::vector<TaskItem> tasks;
    for (int i = 1; i <= count; ++i) {
        tasks.push_back({TaskState::Pending, "Tarea " + std::to_string(i), {}});
    }
    return tasks;
}

} // namespace

TEST_CASE("tareas: los tres estados en una fila", "[tareas]") {
    const std::vector<TaskItem> tasks{
        {TaskState::Done, "Leer las pruebas", "Leyendo las pruebas"},
        {TaskState::Active, "Agregar la prueba", "Agregando la prueba"},
        {TaskState::Pending, "Compilar y correr", "Compilando"},
    };
    CHECK(rows(tasks, 80) == std::vector<std::string>{
                                 "☑ Leer las pruebas  ◐ Agregando la prueba  ☐ Compilar y correr"});
    // En curso sin active_form: su contenido.
    CHECK(rows({{TaskState::Active, "Agregar", ""}}, 40) == std::vector<std::string>{"◐ Agregar"});
    const chatbot::cli::Palette simple{chatbot::cli::default_theme(),
                                       chatbot::cli::BackgroundMode::FromTheme,
                                       chatbot::cli::Symbols::Simple};
    CHECK(rows(tasks, 80, simple) ==
          std::vector<std::string>{
              "[x] Leer las pruebas  [~] Agregando la prueba  [ ] Compilar y correr"});
}

TEST_CASE("tareas: pasan a la fila siguiente cuando no caben", "[tareas]") {
    // Cada "☐ Tarea N" ocupa 9 columnas; con 2 de separación caben 2 en 20.
    CHECK(rows(numbered(5), 20) ==
          std::vector<std::string>{"☐ Tarea 1  ☐ Tarea 2", "☐ Tarea 3  ☐ Tarea 4", "☐ Tarea 5"});
}

TEST_CASE("tareas: tope de 3 filas con \"… N más\"", "[tareas]") {
    // 10 tareas a 2 por fila: 5 filas. Se ven 3; la última deja sitio al aviso.
    const std::vector<std::string> shown = rows(numbered(10), 20);
    REQUIRE(shown.size() == 3);
    CHECK(shown[0] == "☐ Tarea 1  ☐ Tarea 2");
    CHECK(shown[1] == "☐ Tarea 3  ☐ Tarea 4");
    CHECK(shown[2] == "☐ Tarea 5  … 5 más");
    // 3 por fila (31 columnas justas): la 9 sale para que quepa el aviso.
    const std::vector<std::string> wide = rows(numbered(10), 31);
    REQUIRE(wide.size() == 3);
    CHECK(wide[2] == "☐ Tarea 7  ☐ Tarea 8  … 2 más");
    // Si el aviso cabe junto a la fila completa, la fila no pierde tareas.
    const std::vector<std::string> wider = rows(numbered(11), 40);
    REQUIRE(wider.size() == 3);
    CHECK(wider[2] == "☐ Tarea 7  ☐ Tarea 8  ☐ Tarea 9  … 2 más");
}

TEST_CASE("tareas: en filas angostas \"… N más\" tampoco pasa del ancho", "[tareas]") {
    // Una tarea por fila: la tercera sale y el aviso queda solo en su fila.
    for (const int width : {10, 6, 3}) {
        CAPTURE(width);
        ftxui::Element element =
            chatbot::cli::render_task_list(numbered(12), width, chatbot::cli::terminal_palette());
        element->ComputeRequirement();
        CHECK(element->requirement().min_x <= width);
        const std::vector<std::string> shown = rows(numbered(12), width);
        REQUIRE(shown.size() == 3);
        CHECK(shown[2].starts_with("…"));
    }
}

TEST_CASE("tareas: un texto más ancho que la fila se recorta con …", "[tareas]") {
    const std::vector<std::string> shown =
        rows({{TaskState::Pending, std::string(60, 'x'), {}}, {TaskState::Done, "corta", {}}}, 20);
    REQUIRE(shown.size() == 2);
    CHECK(shown[0] == "☐ " + std::string(17, 'x') + "…");
    CHECK(ftxui::string_width(shown[0]) == 20);
    CHECK(shown[1] == "☑ corta");
}

TEST_CASE("tareas: texto filtrado y en una línea", "[tareas]") {
    const std::vector<std::string> shown =
        rows({{TaskState::Pending, "uno\ndos\x1b[2J", {}}}, 40);
    CHECK(shown == std::vector<std::string>{"☐ uno dos\xEF\xBF\xBD[2J"});
}

TEST_CASE("tareas: sin tareas, nada", "[tareas]") {
    ftxui::Element element =
        chatbot::cli::render_task_list({}, 40, chatbot::cli::terminal_palette());
    element->ComputeRequirement();
    CHECK(element->requirement().min_y == 0);
}
