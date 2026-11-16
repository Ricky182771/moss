#include "status_line.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

using chatbot::cli::StatusItem;

/// La línea dibujada en una pantalla de width x 1, sin espacios al final.
std::string draw(const std::vector<StatusItem>& items, int width) {
    ftxui::Screen screen(width, 1);
    ftxui::Render(screen, chatbot::cli::status_line(items, width));
    std::string text;
    for (int x = 0; x < width; ++x) {
        const std::string& character = screen.CellAt(x, 0).character;
        text += character.empty() ? std::string{" "} : character;
    }
    text.erase(text.find_last_not_of(' ') + 1);
    return text;
}

const std::string kThinking = "Pensando… (Esc para cancelar)";
const std::string kIncomplete = "⚠ #1 incompleto (aún no termina) · copiado con wl-copy";

} // namespace

TEST_CASE("estado: ⚠ mide una columna", "[estado]") { CHECK(ftxui::string_width("⚠") == 1); }

TEST_CASE("estado: con una respuesta en curso, el indicador completo y el aviso recortado",
          "[estado]") {
    const int width = GENERATE(40, 80);
    INFO(width << " columnas");
    const std::vector<StatusItem> items{{kIncomplete, ftxui::bold}, {kThinking, {}, true}};
    const std::string row = draw(items, width);
    CHECK(row.ends_with("   " + kThinking));
    CHECK(ftxui::string_width(row) <= width);
    CHECK(ftxui::string_width(row) >= width - 1); // Sin el espacio antes del "…".
    const std::string notice = row.substr(0, row.size() - kThinking.size() - 3);
    CHECK(notice.ends_with("…"));
    CHECK(kIncomplete.starts_with(notice.substr(0, notice.size() - std::string{"…"}.size())));
    if (width == 80) {
        CHECK(notice == "⚠ #1 incompleto (aún no termina) · copiado con…");
    } else {
        CHECK(notice == "⚠ #1 in…");
    }
}

TEST_CASE("estado: si todo cabe, nada se recorta y el orden se conserva", "[estado]") {
    const std::vector<StatusItem> items{{"#1 copiado con wl-copy", ftxui::bold},
                                        {kThinking, {}, true},
                                        {"↓ Hay más abajo (End)", ftxui::bold, true}};
    CHECK(draw(items, 100) == "#1 copiado con wl-copy   " + kThinking + "   ↓ Hay más abajo (End)");
}

TEST_CASE("estado: el primer indicador nunca se quita; los siguientes, si no caben",
          "[estado]") {
    const std::vector<StatusItem> items{
        {"aviso", ftxui::bold}, {kThinking, {}, true}, {"↓ Hay más abajo (End)", {}, true}};
    // 29 + 3 + 21 = 53 > 40: el segundo indicador se quita y el aviso usa lo que sobra.
    CHECK(draw(items, 40) == "aviso   " + kThinking);
    CHECK(draw(items, 34) == "a…   " + kThinking);
    CHECK(draw(items, 33) == kThinking); // Un "…" solo no se muestra.
    CHECK(draw(items, 20).starts_with("Pensando… (Esc")); // Más angosto que el indicador.
}

TEST_CASE("estado: después de un aviso recortado no se muestran los demás", "[estado]") {
    const std::vector<StatusItem> items{{std::string(30, 'a'), {}},
                                        {"Se omitieron 4 mensajes antiguos.", {}}};
    CHECK(draw(items, 20) == std::string(19, 'a') + "…");
    CHECK(draw(items, 80) == std::string(30, 'a') + "   Se omitieron 4 mensajes antiguos.");
}

TEST_CASE("estado: sin partes mide una fila", "[estado]") {
    ftxui::Element element = chatbot::cli::status_line({}, 40);
    element->ComputeRequirement();
    CHECK(element->requirement().min_y == 1);
}

namespace {

using chatbot::cli::StatusZones;

std::string draw_zones(const StatusZones& zones, int width) {
    ftxui::Screen screen(width, 1);
    ftxui::Render(screen, chatbot::cli::zoned_status_line(zones, width));
    std::string text;
    for (int x = 0; x < width; ++x) {
        const std::string& character = screen.CellAt(x, 0).character;
        text += character.empty() ? std::string{" "} : character;
    }
    text.erase(text.find_last_not_of(' ') + 1);
    return text;
}

/// Modo (12 columnas), "Pensando…" (9) con su pista (20) y el modelo (16): 63 en total.
StatusZones thinking() {
    StatusZones zones;
    zones.left = {"Solo lectura"};
    zones.activity = "Pensando…";
    zones.hint = " (Esc para cancelar)";
    zones.right = {"modelo-de-prueba"};
    return zones;
}

} // namespace

TEST_CASE("estado en zonas: el modo a la izquierda, la actividad al centro y el modelo a la "
          "derecha",
          "[estado][zonas]") {
    const int width = GENERATE(63, 80, 100);
    INFO(width << " columnas");
    const std::string row = draw_zones(thinking(), width);
    CHECK(row.starts_with("Solo lectura   "));
    CHECK(row.ends_with("modelo-de-prueba"));
    CHECK(ftxui::string_width(row) == width); // El modelo pegado al borde derecho.
    // Centrada (29 columnas con la pista) sin acercarse a menos de 3 de los lados: en 63, todo
    // justo, pegada al modo. Antes de "Pensando" todo es ASCII: el byte es la columna.
    const std::size_t at = row.find("Pensando… (Esc para cancelar)");
    REQUIRE(at != std::string::npos);
    CHECK(static_cast<int>(at) == std::clamp((width - 29) / 2, 15, width - 29 - 3 - 16));
}

TEST_CASE("estado en zonas: en angosto se quita la pista, luego el modelo y luego el modo",
          "[estado][zonas]") {
    // 62: sin la pista, centrada en (62 - 9) / 2 = 26.
    std::string row = draw_zones(thinking(), 62);
    CHECK(row.find("(Esc") == std::string::npos);
    CHECK(row.find("Pensando…") == 26);
    CHECK(row.ends_with("   modelo-de-prueba"));
    // 12 + 3 + 9 + 3 + 16 = 43 todavía con el modelo: no queda espacio para centrar.
    row = draw_zones(thinking(), 43);
    CHECK(row.starts_with("Solo lectura   Pensando…"));
    CHECK(row.ends_with("   modelo-de-prueba"));
    // 42: sin el modelo, centrada en (42 - 9) / 2 = 16; 12 + 3 + 9 = 24 todavía con el modo.
    CHECK(draw_zones(thinking(), 42) == "Solo lectura    Pensando…");
    CHECK(draw_zones(thinking(), 24) == "Solo lectura   Pensando…");
    // 23: solo la actividad, que nunca se quita.
    CHECK(draw_zones(thinking(), 23) == "Pensando…");
    CHECK(draw_zones(thinking(), 5).starts_with("Pensa"));
}

TEST_CASE("estado en zonas: sin turno, el modo y el modelo", "[estado][zonas]") {
    StatusZones zones = thinking();
    zones.activity.clear(); // La pista sola no aparece.
    CHECK(draw_zones(zones, 31) == "Solo lectura   modelo-de-prueba");
    CHECK(draw_zones(zones, 40).ends_with("modelo-de-prueba"));
    CHECK(draw_zones(zones, 40).find("(Esc") == std::string::npos);
    CHECK(draw_zones(zones, 30) == "Solo lectura");
    CHECK(draw_zones(zones, 11).empty()); // Solo el centro (vacío) en la última opción.
}

TEST_CASE("estado en zonas: un aviso largo quita los lados y se recorta; los indicadores no",
          "[estado][zonas]") {
    StatusZones zones = thinking();
    const std::string notice(60, 'a');
    zones.center = {{notice, ftxui::bold}, {"↓ Hay más abajo (End)", ftxui::bold, true}};
    // 9 + 3 + 60 + 3 + 21 = 96 > 80: sin modo ni modelo; el aviso usa lo que sobra.
    const std::string row = draw_zones(zones, 80);
    CHECK(row.starts_with("Pensando…   aaa"));
    CHECK(row.find("…   ↓ Hay más abajo (End)") != std::string::npos);
    CHECK(row.find("Solo lectura") == std::string::npos);
    CHECK(row.find("modelo-de-prueba") == std::string::npos);
    // Si cabe todo, conviven: los avisos junto al modo y la actividad (con su pista) lo más
    // cerca del centro después de ellos.
    zones.center = {{"Guardado.", ftxui::bold}, {"↓ Hay más abajo (End)", ftxui::bold, true}};
    const std::string wide = draw_zones(zones, 120);
    CHECK(wide.starts_with("Solo lectura   Guardado.   ↓ Hay más abajo (End)   "));
    CHECK(wide.find("   Pensando… (Esc para cancelar)") != std::string::npos);
    CHECK(wide.ends_with("modelo-de-prueba"));
    // A 180, el centro exacto: (180 - 29) / 2 = 75 columnas antes de "Pensando".
    const std::string wider = draw_zones(zones, 180);
    const std::size_t at = wider.find("Pensando…");
    REQUIRE(at != std::string::npos);
    CHECK(ftxui::string_width(wider.substr(0, at)) == 75);
}

TEST_CASE("estado en zonas: sin partes mide una fila", "[estado][zonas]") {
    ftxui::Element element = chatbot::cli::zoned_status_line(StatusZones{}, 40);
    element->ComputeRequirement();
    CHECK(element->requirement().min_y == 1);
}
