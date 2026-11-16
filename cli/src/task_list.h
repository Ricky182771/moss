#ifndef CHATBOT_CLI_TASK_LIST_H
#define CHATBOT_CLI_TASK_LIST_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace chatbot::cli {

/// En qué va una tarea de la lista del modelo (todo_write).
enum class TaskState : std::uint8_t { Pending, Active, Done };

/// Una tarea de la lista, como la dibuja render_task_list. Texto externo: se
/// filtra con md::sanitize al dibujarlo.
struct TaskItem {
    TaskState state = TaskState::Pending;
    std::string content;     ///< "Agregar la prueba".
    std::string active_form; ///< "Agregando la prueba": se ve mientras está en curso.
};

/// La lista que se ve al empezar un turno: si todas las tareas están hechas,
/// se esconde (vacía); si no, sigue igual.
[[nodiscard]] inline std::vector<TaskItem> tasks_for_new_turn(std::vector<TaskItem> tasks) {
    const bool all_done = std::all_of(tasks.begin(), tasks.end(), [](const TaskItem& task) {
        return task.state == TaskState::Done;
    });
    return all_done ? std::vector<TaskItem>{} : tasks;
}

} // namespace chatbot::cli

#endif // CHATBOT_CLI_TASK_LIST_H
