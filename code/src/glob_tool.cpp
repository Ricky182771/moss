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

/// Los archivos normales bajo base que acepta keep, según git; nullopt si no se pudo usar (sin
/// git, no es un repo, falló, se pasó del tiempo o la lista llegó al tope y está incompleta).
std::optional<std::vector<WalkFile>> list_with_git(ToolContext& context, const fs::path& base,
                                                   const RelativeFilter& keep,
                                                   const std::string& git) {
    const process::ProcessResult result =
        run_git(context.run_process, git, context.root,
                {"ls-files", "-z", "--cached", "--others", "--exclude-standard"}, kGitTimeout,
                context.cancel);
    if (!result.ok() || result.out.size() >= process::kMaxProcessOutput) {
        return std::nullopt;
    }

    const std::string base_display = context.root.display(base);
    const std::string base_prefix = base_display == "." ? std::string{} : base_display + "/";
    std::vector<WalkFile> found;
    std::set<fs::path> seen;
    const std::string_view out = result.out;
    for (std::size_t start = 0; start < out.size() && !context.cancel.is_cancelled();) {
        std::size_t end = out.find('\0', start);
        end = end == std::string_view::npos ? out.size() : end;
        const std::string_view entry = out.substr(start, end - start);
        start = end + 1;
        // Texto ajeno: primero un filtro barato, y solo lo que pasa va a resolve.
        if (entry.empty() || !entry.starts_with(base_prefix) ||
            !keep(entry.substr(base_prefix.size()))) {
            continue;
        }
        const ResolvedPath resolved = context.root.resolve(entry);
        const auto* path = std::get_if<fs::path>(&resolved);
        std::error_code error;
        if (path == nullptr || !fs::is_regular_file(*path, error) || error ||
            !seen.insert(*path).second) {
            continue;
        }
        found.push_back({*path, context.root.display(*path)});
    }
    return found;
}

std::string files_es(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " archivo" : " archivos");
}

} // namespace

ToolResult run_glob(const json& arguments, ToolContext& context,
                    const std::optional<std::string>& git_path) {
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
    const ProjectFiles listed = list_project_files(
        context, base, [&pattern](std::string_view relative) { return pattern.matches(relative); },
        git_path);
    if (const auto* error = std::get_if<PathError>(&listed)) {
        return path_error_result(*error);
    }
    if (context.cancel.is_cancelled()) {
        return cancelled_result();
    }
    std::vector<Found> found;
    for (const WalkFile& file : std::get<std::vector<WalkFile>>(listed)) {
        std::error_code error;
        const fs::file_time_type modified = fs::last_write_time(file.path, error);
        if (!error) {
            found.push_back({file.display, modified});
        }
    }

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

namespace {

std::string summarize_glob(const json& arguments) {
    std::string summary = shorten(arguments.at("pattern").get<std::string>(), kMaxShownBytes);
    if (arguments.contains("path")) {
        summary += " en " + shorten(arguments.at("path").get<std::string>(), kMaxShownBytes);
    }
    return summary;
}

} // namespace

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

std::optional<std::string> find_program(std::string_view name) {
    std::optional<std::string> found = process::find_in_path(name);
    // find_in_path ya salta las entradas relativas del PATH; esta es una segunda barrera.
    if (!found || !found->starts_with('/')) {
        return std::nullopt;
    }
    return found;
}

process::ProcessOptions helper_options(std::chrono::milliseconds timeout,
                                       const CancelToken& cancel) {
    process::ProcessOptions options;
    options.timeout = timeout;
    options.cancel = &cancel;
    options.env_remove_prefixes = {"CHAT_"};
    options.env_set = {{"LC_ALL", "C"}, {"GIT_TERMINAL_PROMPT", "0"}, {"GIT_OPTIONAL_LOCKS", "0"}};
    return options;
}

process::ProcessResult run_git(const ProcessLauncher& launch, const std::string& git_path,
                               const ProjectRoot& root, const std::vector<std::string>& args,
                               std::chrono::milliseconds timeout, const CancelToken& cancel) {
    // core.fsmonitor vacío (no "false": git < 2.36 lo tomaría como el programa "false") y hooks en
    // una carpeta que no existe: un .git/config ajeno no lanza nada.
    std::vector<std::string> argv{git_path, "--no-optional-locks", "-c", "core.fsmonitor=",
                                  "-c", "core.hooksPath=/dev/null", "-C", utf8_text(root.path())};
    argv.insert(argv.end(), args.begin(), args.end());
    return launch(argv, helper_options(timeout, cancel));
}

ProjectFiles list_project_files(ToolContext& context, const fs::path& base,
                                const RelativeFilter& keep,
                                const std::optional<std::string>& git_path) {
    std::optional<std::vector<WalkFile>> listed;
    if (git_path) {
        listed = list_with_git(context, base, keep, *git_path);
    }
    if (listed || context.cancel.is_cancelled()) {
        return listed ? std::move(*listed) : std::vector<WalkFile>{};
    }
    std::vector<WalkFile> found;
    const std::optional<PathError> error = context.root.walk(
        base,
        [](std::string_view name) {
            return name == ".git" || name == "node_modules" || name.starts_with("build");
        },
        [&](const WalkFile& file) {
            if (keep(generic_text(file.path.lexically_relative(base)))) {
                found.push_back(file);
            }
            return true;
        },
        context.cancel);
    if (error) {
        return *error;
    }
    return found;
}

std::shared_ptr<const Tool> make_glob_tool() {
    return std::make_shared<const Tool>(Tool{"glob", std::string{kDescription},
                                             json::parse(kSchema), "Buscar archivos",
                                             summarize_glob,
                                             [](const json& arguments, ToolContext& context) {
                                                 return run_glob(arguments, context,
                                                                 find_program("git"));
                                             }});
}

} // namespace chatbot::code
