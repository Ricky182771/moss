#include "chatbot/code/output_cap.h"
#include "chatbot/code/tool.h"
#include "chatbot/hide_secret.h"
#include "chatbot/utf8.h"

#include "tool_internal.h"
#include "tool_schema.h"

#include <string_view>

namespace chatbot::code {

namespace {

using json = nlohmann::json;

/// Tope de un nombre o una ruta del modelo en los mensajes.
constexpr std::size_t kMaxShownBytes = 200;

/// Los argumentos crudos como objeto JSON, o el error. Vacío cuenta como {} (algunos modelos lo
/// mandan así cuando no hay argumentos).
std::optional<std::string> parse_arguments(std::string_view raw, json& arguments) {
    if (raw.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        arguments = json::object();
        return std::nullopt;
    }
    arguments = json::parse(raw, nullptr, false);
    if (arguments.is_discarded()) {
        return "Arguments are not valid JSON; send a JSON object that matches the tool's schema";
    }
    if (!arguments.is_object()) {
        return "Arguments must be a JSON object";
    }
    return std::nullopt;
}

ToolResult invalid_arguments(const Tool& tool, const std::string& message) {
    return ToolResult{"Invalid arguments for " + tool.name + ": " + message + ".",
                      "Argumentos inválidos", ToolStatus::InvalidArguments};
}

ToolResult run(const Tool* tool, std::string_view name, std::string_view raw,
               ToolContext& context, const std::vector<std::shared_ptr<const Tool>>& tools) {
    if (tool == nullptr) {
        std::string available;
        for (const auto& known : tools) {
            available += available.empty() ? "" : ", ";
            available += known->name;
        }
        const std::string shown = shorten(name, kMaxShownBytes);
        return ToolResult{"Unknown tool: " + shown + ". Available tools: " + available + ".",
                          "Herramienta desconocida: " + shown, ToolStatus::UnknownTool};
    }
    json arguments;
    if (std::optional<std::string> error = parse_arguments(raw, arguments)) {
        return invalid_arguments(*tool, *error);
    }
    if (std::optional<std::string> error = validate_arguments(tool->schema, arguments)) {
        return invalid_arguments(*tool, *error);
    }
    return tool->run(arguments, context);
}

} // namespace

std::string shorten(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return std::string{text};
    }
    return std::string{utf8::prefix(text, max_bytes)} + "\xE2\x80\xA6"; // …
}

ToolResult path_error_result(const PathError& error) {
    std::string display;
    switch (error.kind) {
    case PathErrorKind::Invalid:
        display = "Ruta inválida";
        break;
    case PathErrorKind::Outside:
        display = "Fuera del proyecto";
        break;
    case PathErrorKind::Blocked:
        display = "Carpeta de Moss: bloqueada";
        break;
    case PathErrorKind::Unresolvable:
        display = "No se pudo resolver la ruta";
        break;
    case PathErrorKind::WrongType:
        display = "No es un archivo";
        break;
    case PathErrorKind::ReadFailed:
        display = "No se pudo leer";
        break;
    }
    return ToolResult{error.message, display, ToolStatus::Error};
}

ToolRegistry ToolRegistry::standard() {
    return ToolRegistry{{make_read_tool(), make_glob_tool(), make_todo_write_tool()}};
}

const Tool* ToolRegistry::find(std::string_view name) const {
    for (const auto& tool : tools_) {
        if (tool->name == name) {
            return tool.get();
        }
    }
    return nullptr;
}

std::vector<ToolSpec> ToolRegistry::specs() const {
    std::vector<ToolSpec> specs;
    for (const auto& tool : tools_) {
        specs.push_back(ToolSpec{tool->name, tool->description, tool->schema.dump()});
    }
    return specs;
}

std::optional<std::string> ToolRegistry::label(std::string_view name) const {
    const Tool* tool = find(name);
    if (tool == nullptr) {
        return std::nullopt;
    }
    return tool->label;
}

std::string ToolRegistry::summarize(std::string_view name, std::string_view raw_arguments) const {
    const Tool* tool = find(name);
    if (tool == nullptr) {
        return "Herramienta desconocida: " + shorten(name, kMaxShownBytes);
    }
    try {
        json arguments;
        if (parse_arguments(raw_arguments, arguments) ||
            validate_arguments(tool->schema, arguments) || !tool->summarize) {
            return tool->label;
        }
        const std::string detail = tool->summarize(arguments);
        return detail.empty() ? tool->label : tool->label + " " + detail;
    } catch (...) {
        return tool->label;
    }
}

ToolResult ToolRegistry::execute(std::string_view name, std::string_view raw_arguments,
                                 ToolContext& context) const {
    ToolResult result;
    try {
        result = run(find(name), name, raw_arguments, context, tools_);
        for (const std::string& secret : context.secrets) {
            result.text = hide_secret(std::move(result.text), secret);
            result.display = hide_secret(std::move(result.display), secret);
        }
        if (result.text.empty()) {
            result.text = "(no output)";
        }
        result.text = cap_tool_output(result.text);
    } catch (...) {
        return ToolResult{"Internal error while running the tool.", "Error interno",
                          ToolStatus::Error};
    }
    return result;
}

} // namespace chatbot::code
