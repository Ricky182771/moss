#include "chatbot/code/system_prompt.h"

#include "chatbot/platform.h"
#include "chatbot/search_context.h"
#include "chatbot/utf8.h"

#include "tool_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace chatbot::code {

namespace {

namespace fs = std::filesystem;

constexpr std::chrono::milliseconds kGitTimeout{3000};
constexpr std::size_t kMaxBranchBytes = 200;

constexpr std::string_view kPersonalTag = "personal_instructions";
constexpr std::string_view kProjectTag = "project_instructions";
constexpr std::string_view kGitStatusTag = "git_status";

/// Texto ajeno (archivos, salida de git) listo para el modelo: UTF-8 inválido como U+FFFD y sin
/// controles C0 (salvo \n y \t), DEL ni C1; \r\n queda como \n.
std::string clean_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        const std::optional<std::uint32_t> code = utf8::next_code_point(text, i);
        if (!code) {
            out += utf8::kReplacement;
            ++i;
            continue;
        }
        const bool control =
            (*code < 0x20 && *code != '\n' && *code != '\t') || (*code >= 0x7F && *code <= 0x9F);
        if (!control) {
            out.append(text.substr(start, i - start));
        }
    }
    return out;
}

/// Que el texto no pueda cerrar ninguno de nuestros bloques: "</tag" pasa a "<\/tag".
std::string neutralize_tags(std::string text) {
    for (const std::string_view tag : {kPersonalTag, kProjectTag, kGitStatusTag}) {
        const std::string closing = "</" + std::string{tag};
        for (std::size_t pos = text.find(closing); pos != std::string::npos;
             pos = text.find(closing, pos + closing.size() + 1)) {
            text.insert(pos + 1, "\\");
        }
    }
    return text;
}

struct Instructions {
    std::string text;
    bool truncated = false;
};

/// Lee un archivo ya resuelto por root, hasta kMaxInstructionsBytes; nullopt si no se puede.
std::optional<Instructions> read_instructions(const ProjectRoot& root, const fs::path& resolved) {
    const ReadResult read = root.read_file(resolved, kMaxInstructionsBytes);
    const auto* content = std::get_if<FileContent>(&read);
    if (content == nullptr) {
        return std::nullopt;
    }
    std::string_view bytes = content->bytes;
    if (content->truncated && !bytes.empty()) {
        // Sin el carácter que el tope pudo dejar partido.
        bytes = utf8::prefix(bytes, bytes.size() - 1);
    }
    return Instructions{neutralize_tags(clean_text(bytes)), content->truncated};
}

std::optional<Instructions> read_project_file(const ProjectRoot& root, std::string_view name) {
    const ResolvedPath resolved = root.resolve(name);
    const auto* path = std::get_if<fs::path>(&resolved);
    return path == nullptr ? std::nullopt : read_instructions(root, *path);
}

/// El archivo personal está fuera de la raíz: se resuelven sus enlaces y se lee con read_file de
/// un ProjectRoot en su carpeta (solo un archivo normal, sin colgarse con una FIFO).
std::optional<Instructions> read_personal_file(const std::string& path_text) {
    try {
        std::error_code error;
        const fs::path target =
            fs::canonical(fs::path{std::u8string{path_text.begin(), path_text.end()}}, error);
        if (error || !target.has_parent_path() || !target.has_filename()) {
            return std::nullopt;
        }
        const Result<ProjectRoot> folder = ProjectRoot::open(utf8_text(target.parent_path()), {});
        if (folder.is_error()) {
            return std::nullopt;
        }
        return read_project_file(folder.value(), utf8_text(target.filename()));
    } catch (...) {
        return std::nullopt;
    }
}

/// Bytes que le tocan a uno de los dos archivos: la mitad garantizada, más lo que el otro no use.
std::size_t allowance(std::size_t own, std::size_t other) {
    constexpr std::size_t half = kMaxInstructionsBytes / 2;
    return std::min(own, std::max(half, kMaxInstructionsBytes - std::min(other, half)));
}

void fit(Instructions& instructions, std::size_t max_bytes) {
    if (instructions.text.size() > max_bytes) {
        instructions.text = std::string{utf8::prefix(instructions.text, max_bytes)};
        instructions.truncated = true;
    }
}

/// Las líneas de git status, hasta kMaxGitStatusLines y kMaxGitStatusBytes, con marca de recorte.
std::string cap_git_status(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        end = end == std::string_view::npos ? text.size() : end;
        if (end > start) {
            lines.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    std::string kept;
    std::size_t count = 0;
    bool cut = false;
    for (const std::string_view line : lines) {
        if (count == kMaxGitStatusLines) {
            break;
        }
        if (kept.size() + line.size() + 1 > kMaxGitStatusBytes) {
            if (count == 0) {
                kept.append(utf8::prefix(line, kMaxGitStatusBytes - 1)).push_back('\n');
                ++count;
                cut = true;
            }
            break;
        }
        kept.append(line).push_back('\n');
        ++count;
    }
    if (cut || count < lines.size()) {
        kept += "[… git status truncated, " + std::to_string(lines.size() - count) +
                " more lines …]\n";
    }
    return kept;
}

/// Rama y estado de git; vacío si no hay git, no es un repo, falla o se pasa del tiempo.
std::string git_section(const ProjectRoot& root, const SystemPromptOptions& options,
                        const CancelToken& cancel) {
    try {
        const std::optional<std::string> git =
            options.git_path ? options.git_path : find_program("git");
        if (!git || !options.run_process) {
            return {};
        }
        process::ProcessOptions process_options;
        process_options.timeout = kGitTimeout;
        process_options.cancel = &cancel;
        // Sin fsmonitor: un .git/config ajeno no puede lanzar un programa; sin candados opcionales,
        // no le estorba al git del usuario.
        const process::ProcessResult result = options.run_process(
            {*git, "--no-optional-locks", "-c", "core.fsmonitor=false", "-c",
             "color.status=false", "-C", utf8_text(root.path()), "status", "--short", "--branch"},
            process_options);
        if (!result.ok()) {
            return {};
        }
        const std::string out = neutralize_tags(clean_text(result.out));
        std::string_view rest = out;
        std::string branch = "unknown";
        if (rest.starts_with("## ")) {
            const std::size_t end = std::min(rest.find('\n'), rest.size());
            branch = shorten(rest.substr(3, end - 3), kMaxBranchBytes);
            rest = rest.substr(std::min(end + 1, rest.size()));
        }
        const std::string status = cap_git_status(rest);
        return "- Git repository: yes, branch " + branch + "\n- Git status (git status --short): " +
               (status.empty() ? "clean\n"
                               : "\n<" + std::string{kGitStatusTag} + ">\n" + status + "</" +
                                     std::string{kGitStatusTag} + ">\n");
    } catch (...) {
        return {};
    }
}

std::string environment_block(const ProjectRoot& root, const SystemPromptOptions& options,
                              const CancelToken& cancel) {
    std::string block = "# Environment\n\n- Project directory: " +
                        clean_text(utf8_text(root.path())) + "\n- Operating system: " +
                        (current_os() == Os::Windows ? "Windows" : "Linux") + "\n";
    const std::string date = options.now ? local_iso_date(options.now()) : std::string{};
    if (!date.empty()) {
        block += "- Today's date: " + date + "\n";
    }
    block += options.commands_available
                 ? "- Commands: available with the `bash` tool.\n"
                 : "- Commands: not available in this session; you cannot run anything.\n";
    block += git_section(root, options, cancel);
    return block;
}

std::string instructions_block(std::string_view tag, std::string_view attributes,
                               const Instructions& instructions) {
    std::string block = "<" + std::string{tag} + std::string{attributes} + ">\n" +
                        instructions.text;
    if (!block.ends_with('\n')) {
        block += '\n';
    }
    if (instructions.truncated) {
        block += "[… truncated: personal and project instructions are limited to 40 KiB …]\n";
    }
    return block + "</" + std::string{tag} + ">\n";
}

} // namespace

CodeSystemPrompt build_code_system_prompt(const ProjectRoot& root,
                                          const SystemPromptOptions& options,
                                          const CancelToken& cancel) {
    CodeSystemPrompt prompt;
    std::optional<Instructions> personal;
    if (options.personal_instructions) {
        personal = read_personal_file(*options.personal_instructions);
    }
    std::optional<Instructions> project = read_project_file(root, "AGENTS.md");
    prompt.project = ProjectInstructions::AgentsMd;
    if (!project) {
        project = read_project_file(root, "CLAUDE.md");
        prompt.project = project ? ProjectInstructions::ClaudeMd : ProjectInstructions::None;
    }

    const std::size_t personal_size = personal ? personal->text.size() : 0;
    const std::size_t project_size = project ? project->text.size() : 0;
    if (personal) {
        fit(*personal, allowance(personal_size, project_size));
    }
    if (project) {
        fit(*project, allowance(project_size, personal_size));
    }
    prompt.personal_loaded = personal.has_value();
    prompt.instructions_truncated =
        (personal && personal->truncated) || (project && project->truncated);

    prompt.text = std::string{fixed_system_prompt()};
    if (!prompt.text.ends_with('\n')) {
        prompt.text += '\n';
    }
    prompt.text += "\n" + environment_block(root, options, cancel);
    if (personal || project) {
        prompt.text += "\n# Personal and project instructions\n";
        if (personal) {
            prompt.text += "\n" + instructions_block(kPersonalTag, "", *personal);
        }
        if (project) {
            const std::string_view file =
                prompt.project == ProjectInstructions::AgentsMd ? "AGENTS.md" : "CLAUDE.md";
            prompt.text += "\n" + instructions_block(
                                      kProjectTag, " file=\"" + std::string{file} + "\"", *project);
        }
    }
    return prompt;
}

} // namespace chatbot::code
