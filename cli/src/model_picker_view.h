#ifndef CHATBOT_CLI_MODEL_PICKER_VIEW_H
#define CHATBOT_CLI_MODEL_PICKER_VIEW_H

#include "model_picker.h"
#include "theme.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// "key guardada", "local", la nota de no elegible ("sin key", "falta la URL") o "".
[[nodiscard]] std::string_view provider_state_text(const ProviderChoice& choice);

/// Dibuja el selector de /model abierto, de width columnas, en el lugar del
/// menú de comandos y con sus reglas: una línea en el color de borde, un
/// renglón que dice el paso (en notice) y hasta ModelPicker::kVisibleRows
/// filas desde picker.first(). Proveedores: marca del actual
/// (Glyph::ChosenMark), id, nombre y, a la derecha, provider_state_text; los
/// que no se pueden elegir, en notice. Modelos: marca del actual e id. Los
/// avisos (cargando, sin coincidencias) van en notice y sin cursor. El
/// cursor lleva Palette::selection() a todo lo ancho. En pantallas
/// angostas el estado se oculta (con menos de kMenuMinDescription columnas)
/// y lo demás se recorta con "…". Ids y filtro pasan por md::sanitize.
/// row_boxes recibe la caja de cada fila visible, en el orden de rows()
/// desde first() (una por fila; las demás quedan vacías).
[[nodiscard]] ftxui::Element render_model_picker(const ModelPicker& picker, int width,
                                                 const Palette& palette,
                                                 std::vector<ftxui::Box>* row_boxes = nullptr);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_MODEL_PICKER_VIEW_H
