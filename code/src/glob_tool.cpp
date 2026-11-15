#include "chatbot/platform.h"

#include "glob_match.h"
#include "tool_internal.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace chatbot::code {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::size_t kMaxResults = 200;
constexpr std::chrono::milliseconds kGitTimeout{10000};
constexpr std::size_t kMaxShownBytes = 200;

constexpr std::string_view kDescription =
    "Finds files by name pattern. Supports * (not across directories), ? , ** (across "
    "directories), [abc] classes and {a,b} alternatives, e.g. \"**/*.{ts,tsx}\". A pattern "
    "without a slash is matched against the file name at any depth (\"*.md\" finds docs/a.md); "
    "with a slash, against the path relative to the search directory. In a git repository it "
    "respects .gitignore. Returns up to 200 paths, most recently modified first.";

constexpr std::string_view kSchema = R"({
    "type": "object",
    "properties": {
        "pattern": {"type": "string",
                    "description": "Glob pattern, e.g. \"**/*.cpp\" or \"src/**/test_*.py\"."},
        "path": {"type": "string",
                 "description": "Directory to search, relative to the project root or absolute. Default: the project root."}
    },
    "required": ["pattern"]
})";

struct Found {
    std::string display;
    fs::file_time_type modified;
};

std::string utf8_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::string generic_text(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

ToolResult cancelled_result() {
    return ToolResult{"Cancelled by the user.", "Cancelado", ToolStatus::Error};
}

/// Todos los archivos regulares bajo base que coinciden, según git; nullopt si no se pudo usar
/// (sin git, no es un repo, falló, se pasó del tiempo o la lista llegó al tope y está incompleta).
std::optional<std::vector<Found>> list_with_git(ToolContext& context, const fs::path& base,
                                                const GlobPattern& pattern) {
    process::ProcessOptions options;
    options.timeout = kGitTimeout;
    options.cancel = &context.cancel;
    const process::ProcessResult result = context.run_process(
        {"git", "-C", utf8_text(context.root.path()), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard"},
        options);
    if (!result.ok() || result.out.size() >= process::kMaxProcessOutput) {
        return std::nullopt;
    }

    const std::string base_display = context.root.display(base);
    const std::string base_prefix = base_display == "." ? std::string{} : base_display + "/";
    std::vector<Found> found;
    std::set<fs::path> seen;
    const std::string_view out = result.out;
    for (std::size_t start = 0; start < out.size() && !context.cancel.is_cancelled();) {
        std::size_t end = out.find('\0', start);
        end = end == std::string_view::npos ? out.size() : end;
        const std::string_view entry = out.substr(start, end - start);
        start = end + 1;
        // Texto ajeno: primero un filtro barato, y solo lo que coincide pasa por resolve.
        if (entry.empty() || !entry.starts_with(base_prefix) ||
            !pattern.matches(entry.substr(base_prefix.size()))) {
            continue;
        }
        const ResolvedPath resolved = context.root.resolve(entry);
        const auto* path = std::get_if<fs::path>(&resolved);
        std::error_code error;
        if (path == nullptr || !fs::is_regular_file(*path, error) || error ||
            !seen.insert(*path).second) {
            continue;
        }
        const fs::file_time_type modified = fs::last_write_time(*path, error);
        if (!error) {
            found.push_back({context.root.display(*path), modified});
        }
    }
    return found;
}

/// Lo mismo, recorriendo la carpeta a mano.
std::optional<PathError> list_with_walk(ToolContext& context, const fs::path& base,
                                        const GlobPattern& pattern, std::vector<Found>& found) {
    return context.root.walk(
        base,
        [](std::string_view name) {
            return name == ".git" || name == "node_modules" || name.starts_with("build");
        },
        [&](const WalkFile& file) {
            if (!pattern.matches(generic_text(file.path.lexically_relative(base)))) {
                return true;
            }
            std::error_code error;
            const fs::file_time_type modified = fs::last_write_time(file.path, error);
            if (!error) {
                found.push_back({file.display, modified});
            }
            return true;
        },
        context.cancel);
}

std::string files_es(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " archivo" : " archivos");
}

ToolResult run_glob(const json& arguments, ToolContext& context) {
    const std::string pattern_text = arguments.at("pattern").get<std::string>();
    fs::path base = context.root.path();
    if (arguments.contains("path")) {
        const ResolvedPath resolved = context.root.resolve(arguments.at("path").get<std::string>());
        if (const auto* error = std::get_if<PathError>(&resolved)) {
            return path_error_result(*error);
        }
        base = std::get<fs::path>(resolved);
        std::error_code error;
        if (!fs::is_directory(base, error)) {
            return ToolResult{"Path is not a directory: " + context.root.display(base),
                              "No es una carpeta", ToolStatus::Error};
        }
    }

    const GlobPattern pattern{pattern_text, current_os() == Os::Windows};
    std::optional<std::vector<Found>> listed = list_with_git(context, base, pattern);
    if (context.cancel.is_cancelled()) {
        return cancelled_result();
    }
    if (!listed) {
        listed.emplace();
        if (const std::optional<PathError> error = list_with_walk(context, base, pattern, *listed)) {
            return path_error_result(*error);
        }
        if (context.cancel.is_cancelled()) {
            return cancelled_result();
        }
    }
    std::vector<Found>& found = *listed;

    if (found.empty()) {
        return ToolResult{"No files matched the pattern \"" + shorten(pattern_text, kMaxShownBytes) +
                              "\" in " + context.root.display(base) + ".",
                          "Sin coincidencias", ToolStatus::Ok};
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.modified != b.modified ? a.modified > b.modified : a.display < b.display;
    });
    const std::size_t total = found.size();
    const std::size_t shown = std::min(total, kMaxResults);
    std::string text;
    for (std::size_t i = 0; i < shown; ++i) {
        text += found[i].display;
        text += '\n';
    }
    if (total > shown) {
        text += "\n(Showing " + std::to_string(shown) + " of " + std::to_string(total) +
                " files, most recently modified first. Narrow the pattern or the path to see "
                "the rest.)";
    } else {
        text.pop_back();
    }
    return ToolResult{std::move(text),
                      total > shown ? std::to_string(shown) + " de " + files_es(total)
                                    : files_es(total),
                      ToolStatus::Ok};
}

std::string summarize_glob(const json& arguments) {
    std::string summary = shorten(arguments.at("pattern").get<std::string>(), kMaxShownBytes);
    if (arguments.contains("path")) {
        summary += " en " + shorten(arguments.at("path").get<std::string>(), kMaxShownBytes);
    }
    return summary;
}

} // namespace

std::shared_ptr<const Tool> make_glob_tool() {
    return std::make_shared<const Tool>(Tool{"glob", std::string{kDescription},
                                             json::parse(kSchema), "Buscar archivos",
                                             summarize_glob, run_glob});
}

} // namespace chatbot::code
