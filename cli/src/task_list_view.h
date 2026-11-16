#ifndef CHATBOT_CLI_TASK_LIST_VIEW_H
#define CHATBOT_CLI_TASK_LIST_VIEW_H

#include "task_list.h"
#include "theme.h"

#include <ftxui/dom/elements.hpp>

#include <vector>

namespace chatbot::cli {

/// Filas como máximo de la lista de tareas.
inline constexpr int kTaskListMaxRows = 3;

/// La lista de tareas para ponerla arriba de la caja: "☐" pendiente, "◐" en
/// curso (con su active_form, o content si está vacío) y "☑" hecha, en línea
/// y separadas por dos espacios, pasando a la fila siguiente cuando no caben.
/// Con más de kTaskListMaxRows filas, la última termina en "… N más". Una
/// tarea más ancha que la fila se recorta con "…". Sin tareas, un elemento
/// de 0 filas.
[[nodiscard]] ftxui::Element render_task_list(const std::vector<TaskItem>& tasks, int width,
                                              const Palette& palette);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_TASK_LIST_VIEW_H
