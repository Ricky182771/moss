#include "output_cap.h"
#include "tool_internal.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chatbot::code {

namespace {

using json = nlohmann::json;

constexpr std::int64_t kDefaultLimit = 2000;
constexpr std::size_t kMaxLineChars = 2000;
/// Más grande que esto no se lee: con grep se encuentra lo que importa.
constexpr std::size_t kMaxReadBytes = 10 * 1024 * 1024;

constexpr std::string_view kDescription =
    "Reads a text file from the project. Returns up to 2000 lines starting at offset (1 = first "
    "line), numbered like `cat -n`. Lines longer than 2000 characters are truncated. Use offset "
    "and limit to read a long file in parts. Binary files and files over 10 MiB cannot be read; "
    "use grep for those.";

constexpr std::string_view kSchema = R"({
    "type": "object",
    "properties": {
        "path": {"type": "string",
                 "description": "File path, relative to the project root or absolute."},
        "offset": {"type": "integer", "minimum": 1,
                   "description": "Line number to start reading from (1 = first line). Default 1."},
        "limit": {"type": "integer", "minimum": 1,
                  "description": "Maximum number of lines to return. Default 2000."}
    },
    "required": ["path"]
})";

std::string lines_es(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " línea" : " líneas");
}

/// Número alineado a la derecha en 6 columnas, como cat -n.
std::string numbered(std::size_t number) {
    std::string text = std::to_string(number);
    if (text.size() < 6) {
        text.insert(0, 6 - text.size(), ' ');
    }
    return text;
}

ToolResult run_read(const json& arguments, ToolContext& context) {
    const ResolvedPath resolved = context.root.resolve(arguments.at("path").get<std::string>());
    if (const auto* error = std::get_if<PathError>(&resolved)) {
        return path_error_result(*error);
    }
    const std::filesystem::path& path = std::get<std::filesystem::path>(resolved);
    const ReadResult read = context.root.read_file(path, kMaxReadBytes);
    if (const auto* error = std::get_if<PathError>(&read)) {
        return path_error_result(*error);
    }
    const FileContent& content = std::get<FileContent>(read);
    const std::string shown = context.root.display(path);
    if (content.truncated) {
        return ToolResult{"File is too large to read: " + shown + " (" +
                              std::to_string(content.size) +
                              " bytes; the limit is 10 MiB). Use grep to find the lines you need.",
                          "Archivo demasiado grande", ToolStatus::Error};
    }
    if (content.bytes.find('\0') != std::string::npos) {
        return ToolResult{"Cannot read binary file: " + shown, "Archivo binario",
                          ToolStatus::Error};
    }

    std::vector<std::string_view> lines;
    const std::string_view bytes = content.bytes;
    for (std::size_t start = 0; start < bytes.size();) {
        const std::size_t end = bytes.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(bytes.substr(start));
            break;
        }
        lines.push_back(bytes.substr(start, end - start));
        start = end + 1;
    }
    const std::size_t total = lines.size();
    const ReadRecord record{content.size, content.modified};
    if (total == 0) {
        context.state.reads[path] = record;
        return ToolResult{"The file " + shown + " is empty.", "Archivo vacío", ToolStatus::Ok};
    }

    const auto offset = static_cast<std::uint64_t>(arguments.value("offset", std::int64_t{1}));
    const auto limit = static_cast<std::uint64_t>(arguments.value("limit", kDefaultLimit));
    if (offset > total) {
        return ToolResult{"Offset " + std::to_string(offset) + " is past the end of " + shown +
                              ", which has " + std::to_string(total) + " lines.",
                          "Más allá del final (" + lines_es(total) + ")", ToolStatus::Error};
    }
    const std::size_t first = static_cast<std::size_t>(offset);
    const std::size_t last =
        limit > total - first ? total : first + static_cast<std::size_t>(limit) - 1;

    std::string text;
    for (std::size_t number = first; number <= last; ++number) {
        text += numbered(number);
        text += '\t';
        text += shown_line(lines[number - 1], kMaxLineChars);
        text += '\n';
    }
    std::string display = lines_es(last - first + 1);
    if (first != 1 || last != total) {
        text += "\n(Showing lines " + std::to_string(first) + "-" + std::to_string(last) + " of " +
                std::to_string(total) + ". ";
        text += last < total ? "Use offset=" + std::to_string(last + 1) + " to continue.)"
                             : std::string{"Use offset to read the earlier lines.)"};
        display += " (" + std::to_string(first) + "–" + std::to_string(last) + " de " +
                   std::to_string(total) + ")";
    } else {
        text.pop_back();
    }
    context.state.reads[path] = record;
    return ToolResult{std::move(text), std::move(display), ToolStatus::Ok};
}

std::string summarize_read(const json& arguments) {
    std::string summary = shorten(arguments.at("path").get<std::string>(), 200);
    if (arguments.contains("offset") || arguments.contains("limit")) {
        const std::int64_t first = arguments.value("offset", std::int64_t{1});
        const std::int64_t limit = arguments.value("limit", kDefaultLimit);
        const bool unbounded = limit - 1 > std::numeric_limits<std::int64_t>::max() - first;
        summary += " (" + std::to_string(first) + "–" +
                   (unbounded ? std::string{"…"} : std::to_string(first + limit - 1)) + ")";
    }
    return summary;
}

} // namespace

std::shared_ptr<const Tool> make_read_tool() {
    return std::make_shared<const Tool>(Tool{"read", std::string{kDescription},
                                             json::parse(kSchema), "Leer", summarize_read,
                                             run_read});
}

} // namespace chatbot::code
