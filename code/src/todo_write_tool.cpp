#include "tool_internal.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::code {

namespace {

using json = nlohmann::json;

constexpr std::string_view kDescription =
    "Replaces the whole task list for the current work. Send the complete list every time: "
    "each item has content (imperative, e.g. \"Fix the parser\"), active_form (present "
    "continuous, e.g. \"Fixing the parser\") and status (pending, in_progress or completed). "
    "Keep exactly one task in_progress while working, and mark tasks completed as soon as they "
    "are done. An empty list clears it. Use it for work with three or more steps.";

constexpr std::string_view kSchema = R"({
    "type": "object",
    "properties": {
        "todos": {
            "type": "array",
            "description": "The complete task list.",
            "items": {
                "type": "object",
                "properties": {
                    "content": {"type": "string", "description": "What to do, imperative."},
                    "status": {"type": "string", "enum": ["pending", "in_progress", "completed"]},
                    "active_form": {"type": "string",
                                    "description": "What is being done, present continuous."}
                },
                "required": ["content", "status", "active_form"]
            }
        }
    },
    "required": ["todos"]
})";

TodoStatus parse_status(const std::string& status) {
    if (status == "in_progress") {
        return TodoStatus::InProgress;
    }
    return status == "completed" ? TodoStatus::Completed : TodoStatus::Pending;
}

ToolResult run_todo_write(const json& arguments, ToolContext& context) {
    std::vector<TodoItem> todos;
    for (const json& item : arguments.at("todos")) {
        std::string content = item.at("content").get<std::string>();
        if (content.find_first_not_of(" \t\r\n") == std::string::npos) {
            return ToolResult{"Invalid arguments for todo_write: todos[" +
                                  std::to_string(todos.size()) + "].content must not be empty.",
                              "Argumentos inválidos", ToolStatus::InvalidArguments};
        }
        todos.push_back({std::move(content), parse_status(item.at("status").get<std::string>()),
                         item.at("active_form").get<std::string>()});
    }
    const auto count = [&todos](TodoStatus status) {
        return static_cast<std::size_t>(std::count_if(
            todos.begin(), todos.end(), [status](const TodoItem& t) { return t.status == status; }));
    };
    const std::size_t pending = count(TodoStatus::Pending);
    const std::size_t in_progress = count(TodoStatus::InProgress);
    const std::size_t completed = count(TodoStatus::Completed);
    const std::size_t total = todos.size();
    context.state.todos = std::move(todos);

    if (total == 0) {
        return ToolResult{"Todo list cleared.", "Lista de tareas vaciada", ToolStatus::Ok};
    }
    std::string display = std::to_string(total) + (total == 1 ? " tarea" : " tareas");
    if (in_progress > 0) {
        display += " · " + std::to_string(in_progress) + " en curso";
    }
    return ToolResult{"Todo list updated: " + std::to_string(total) + " tasks (" +
                          std::to_string(pending) + " pending, " + std::to_string(in_progress) +
                          " in progress, " + std::to_string(completed) + " completed).",
                      std::move(display), ToolStatus::Ok};
}

std::string summarize_todo_write(const json& arguments) {
    return "(" + std::to_string(arguments.at("todos").size()) + ")";
}

} // namespace

std::shared_ptr<const Tool> make_todo_write_tool() {
    return std::make_shared<const Tool>(Tool{"todo_write", std::string{kDescription},
                                             json::parse(kSchema), "Lista de tareas",
                                             summarize_todo_write, run_todo_write});
}

} // namespace chatbot::code
