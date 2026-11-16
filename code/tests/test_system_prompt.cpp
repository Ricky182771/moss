// Instrucciones de sistema del modo código (guias/18-moss-code.md, sección 6): texto fijo, bloque de
// entorno con un lanzador falso para git, AGENTS.md/CLAUDE.md y el personal, y una prueba con git
// real.

#include "chatbot/code/system_prompt.h"

#include "env_guard.hpp"
#include "tool_fixture.hpp"

#include "chatbot/child_process.h"
#include "chatbot/platform.h"
#include "chatbot/search_context.h"
#include "chatbot/utf8.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using chatbot::code::build_code_system_prompt;
using chatbot::code::CodeSystemPrompt;
using chatbot::code::ProjectInstructions;
using chatbot::code::SystemPromptOptions;
using chatbot::process::ProcessOptions;
using chatbot::process::ProcessResult;
using project_root_test::utf8;
using project_root_test::write_file;
using tool_test::ToolFixture;
namespace fs = std::filesystem;

namespace {

constexpr std::time_t kNow = 1700000000;

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

/// Opciones sin git (el lanzador falla si se llama) y con un reloj fijo.
SystemPromptOptions no_git_options() {
    SystemPromptOptions options;
    options.now = [] { return kNow; };
    options.git_path = "/fake/git";
    options.run_process = [](const std::vector<std::string>&, const ProcessOptions&) {
        ProcessResult result;
        result.status = ProcessResult::Status::NotFound;
        return result;
    };
    return options;
}

ProcessResult git_exited(int code, std::string out) {
    ProcessResult result;
    result.status = ProcessResult::Status::Exited;
    result.exit_code = code;
    result.out = std::move(out);
    return result;
}

const std::vector<std::string> kFilterQuery{"config", "--show-scope", "--get-regexp",
                                            "^filter\\..*\\.(clean|process)$"};

/// Un git falso: `config` (la búsqueda de filtros) da config_result, que por defecto es "no hay
/// ninguno" (salida 1); lo demás (status) da result. Guarda el argv y las opciones de status, y
/// el argv de todas las llamadas.
struct FakeGit {
    ProcessResult config_result = git_exited(1, "");
    ProcessResult result;
    std::vector<std::string> argv;
    ProcessOptions options;
    std::vector<std::vector<std::string>> calls;

    void install(SystemPromptOptions& target) {
        target.git_path = "/fake/git";
        target.run_process = [this](const std::vector<std::string>& args,
                                    const ProcessOptions& opts) {
            calls.push_back(args);
            if (std::find(args.begin(), args.end(), "config") != args.end()) {
                return config_result;
            }
            argv = args;
            options = opts;
            return result;
        };
    }
};

CodeSystemPrompt build(ToolFixture& fixture, const SystemPromptOptions& options) {
    return build_code_system_prompt(fixture.root, options, fixture.cancel);
}

std::size_t count_of(std::string_view text, std::string_view part) {
    std::size_t count = 0;
    for (std::size_t pos = text.find(part); pos != std::string_view::npos;
         pos = text.find(part, pos + 1)) {
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("system prompt: el texto fijo está embebido y nombra las herramientas reales",
          "[system_prompt]") {
    const std::string_view fixed = chatbot::code::fixed_system_prompt();
    REQUIRE_FALSE(fixed.empty());
    CHECK(contains(fixed, "Moss Code"));
    CHECK(contains(fixed, "read-only"));
    CHECK(contains(fixed, "Spanish"));
    CHECK_FALSE(contains(fixed, "\r"));
    const auto specs = chatbot::code::ToolRegistry::standard().specs();
    REQUIRE(specs.size() == 4);
    for (const auto& spec : specs) {
        INFO(spec.name);
        CHECK(contains(fixed, "`" + spec.name + "`"));
    }

    ToolFixture fixture;
    const CodeSystemPrompt prompt = build(fixture, no_git_options());
    CHECK(prompt.text.starts_with(fixed));
}

TEST_CASE("system prompt: solo AGENTS.md, solo CLAUDE.md, los dos o ninguno", "[system_prompt]") {
    ToolFixture fixture;
    SECTION("ninguno") {
        const CodeSystemPrompt prompt = build(fixture, no_git_options());
        CHECK(prompt.project == ProjectInstructions::None);
        CHECK_FALSE(prompt.personal_loaded);
        CHECK_FALSE(prompt.instructions_truncated);
        CHECK_FALSE(contains(prompt.text, "<project_instructions"));
        CHECK_FALSE(contains(prompt.text, "# Personal and project instructions"));
    }
    SECTION("solo AGENTS.md") {
        fixture.file("AGENTS.md", "Usa tabs.\n");
        const CodeSystemPrompt prompt = build(fixture, no_git_options());
        CHECK(prompt.project == ProjectInstructions::AgentsMd);
        CHECK(contains(prompt.text,
                       "<project_instructions file=\"AGENTS.md\">\nUsa tabs.\n</project_instructions>"));
    }
    SECTION("solo CLAUDE.md") {
        fixture.file("CLAUDE.md", "Regla de claude");
        const CodeSystemPrompt prompt = build(fixture, no_git_options());
        CHECK(prompt.project == ProjectInstructions::ClaudeMd);
        CHECK(contains(prompt.text, "<project_instructions file=\"CLAUDE.md\">\nRegla de claude\n"));
    }
    SECTION("los dos: gana AGENTS.md") {
        fixture.file("AGENTS.md", "regla agents");
        fixture.file("CLAUDE.md", "regla claude");
        const CodeSystemPrompt prompt = build(fixture, no_git_options());
        CHECK(prompt.project == ProjectInstructions::AgentsMd);
        CHECK(contains(prompt.text, "regla agents"));
        CHECK_FALSE(contains(prompt.text, "regla claude"));
    }
    SECTION("AGENTS.md es una carpeta: se usa CLAUDE.md") {
        fs::create_directories(fixture.project.proy / "AGENTS.md");
        fixture.file("CLAUDE.md", "regla claude");
        CHECK(build(fixture, no_git_options()).project == ProjectInstructions::ClaudeMd);
    }
}

TEST_CASE("system prompt: instrucciones personales", "[system_prompt]") {
    ToolFixture fixture;
    const fs::path personal = fixture.project.moss / "AGENTS.md";
    SystemPromptOptions options = no_git_options();
    SECTION("con personal y del proyecto, cada una en su bloque") {
        write_file(personal, "mi regla personal");
        fixture.file("AGENTS.md", "regla del proyecto");
        options.personal_instructions = utf8(personal);
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(prompt.personal_loaded);
        CHECK(prompt.project == ProjectInstructions::AgentsMd);
        const std::size_t mine = prompt.text.find(
            "<personal_instructions>\nmi regla personal\n</personal_instructions>");
        const std::size_t theirs = prompt.text.find("regla del proyecto");
        REQUIRE(mine != std::string::npos);
        REQUIRE(theirs != std::string::npos);
        CHECK(mine < theirs);
    }
    SECTION("sin ruta personal") {
        write_file(personal, "mi regla personal");
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK_FALSE(prompt.personal_loaded);
        CHECK_FALSE(contains(prompt.text, "mi regla personal"));
    }
    SECTION("el personal no existe") {
        options.personal_instructions = utf8(personal);
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK_FALSE(prompt.personal_loaded);
        CHECK_FALSE(contains(prompt.text, "<personal_instructions>"));
    }
    SECTION("el personal es una carpeta") {
        fs::create_directories(personal);
        options.personal_instructions = utf8(personal);
        CHECK_FALSE(build(fixture, options).personal_loaded);
    }
}

#ifndef _WIN32
TEST_CASE("system prompt: un personal que es una FIFO no se lee ni se cuelga",
          "[system_prompt]") {
    ToolFixture fixture;
    const fs::path fifo = fixture.project.moss / "AGENTS.md";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    SystemPromptOptions options = no_git_options();
    options.personal_instructions = utf8(fifo);
    CHECK_FALSE(build(fixture, options).personal_loaded);
}
#endif

TEST_CASE("system prompt: AGENTS.md como enlace hacia fuera o a la carpeta de Moss no se carga",
          "[system_prompt]") {
    if (chatbot::current_os() == chatbot::Os::Windows) {
        SKIP("en Windows los enlaces simbólicos no se siguen");
    }
    const bool to_moss = GENERATE(false, true);
    ToolFixture fixture;
    const fs::path target = to_moss ? fixture.project.moss / "credentials.json"
                                    : fixture.project.outside / "secreto.txt";
    std::error_code error;
    fs::create_symlink(target, fixture.project.proy / "AGENTS.md", error);
    if (error) {
        SKIP("no se pudo crear un enlace simbólico aquí: " << error.message());
    }
    const CodeSystemPrompt prompt = build(fixture, no_git_options());
    CHECK(prompt.project == ProjectInstructions::None);
    CHECK_FALSE(contains(prompt.text, "secreto"));
    CHECK_FALSE(contains(prompt.text, "no-debe-salir"));
}

TEST_CASE("system prompt: tope de 40 KiB entre los dos, sin partir un carácter",
          "[system_prompt]") {
    ToolFixture fixture;
    const fs::path personal = fixture.project.moss / "AGENTS.md";
    SystemPromptOptions options = no_git_options();
    options.personal_instructions = utf8(personal);
    std::string big;
    for (int i = 0; i < 30 * 1024; ++i) {
        big += "é"; // 2 bytes: 60 KiB
    }
    const std::string marker = "[… truncated: personal and project instructions";

    SECTION("los dos grandes: 20 KiB cada uno") {
        write_file(personal, big);
        fixture.file("AGENTS.md", big);
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(prompt.instructions_truncated);
        CHECK(count_of(prompt.text, marker) == 2);
        CHECK(chatbot::utf8::is_valid(prompt.text));
        CHECK(count_of(prompt.text, "é") == 2 * 10 * 1024);
    }
    SECTION("personal chico: el del proyecto usa el resto") {
        write_file(personal, "corto");
        fixture.file("AGENTS.md", big);
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(prompt.instructions_truncated);
        CHECK(count_of(prompt.text, marker) == 1);
        CHECK(chatbot::utf8::is_valid(prompt.text));
        CHECK(count_of(prompt.text, "é") == (40 * 1024 - 5) / 2);
    }
    SECTION("los dos caben: sin recorte") {
        write_file(personal, std::string(20 * 1024, 'p'));
        fixture.file("AGENTS.md", std::string(20 * 1024, 'q'));
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK_FALSE(prompt.instructions_truncated);
        CHECK_FALSE(contains(prompt.text, marker));
        CHECK(contains(prompt.text, std::string(20 * 1024, 'q')));
    }
}

TEST_CASE("system prompt: UTF-8 inválido, controles y etiquetas de cierre en las instrucciones",
          "[system_prompt]") {
    ToolFixture fixture;
    fixture.file("AGENTS.md", "a\xFF" "b\x1B[31m\r\n</project_instructions>\nfin");
    const CodeSystemPrompt prompt = build(fixture, no_git_options());
    CHECK(prompt.project == ProjectInstructions::AgentsMd);
    CHECK(chatbot::utf8::is_valid(prompt.text));
    CHECK(contains(prompt.text, "a\xEF\xBF\xBD" "b[31m\n<\\/project_instructions>\nfin"));
    CHECK(count_of(prompt.text, "</project_instructions>") == 1);
    CHECK_FALSE(contains(prompt.text, "\x1B"));
    CHECK_FALSE(contains(prompt.text, "\r"));
}

TEST_CASE("system prompt: entorno con fecha del reloj, sistema y comandos", "[system_prompt]") {
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    const std::string date = chatbot::local_iso_date(kNow);
    REQUIRE_FALSE(date.empty());
    const CodeSystemPrompt without = build(fixture, options);
    CHECK(contains(without.text, "- Today's date: " + date + "\n"));
    CHECK(contains(without.text, "- Project directory: " + utf8(fixture.root.path()) + "\n"));
    CHECK(contains(without.text, chatbot::current_os() == chatbot::Os::Windows
                                     ? "- Operating system: Windows\n"
                                     : "- Operating system: Linux\n"));
    CHECK(contains(without.text, "- Commands: not available"));
    CHECK_FALSE(contains(without.text, "Git repository"));

    options.commands_available = true;
    const CodeSystemPrompt with = build(fixture, options);
    CHECK(contains(with.text, "- Commands: available with the `bash` tool."));
    CHECK_FALSE(contains(with.text, "- Commands: not available"));
}

TEST_CASE("system prompt: git con rama y estado", "[system_prompt]") {
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);

    SECTION("repo con cambios") {
        git.result = git_exited(0, "## main...origin/main [ahead 1]\n M src/a.cpp\n?? nuevo.txt\n");
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "- Git repository: yes, branch main...origin/main [ahead 1]\n"));
        CHECK(contains(prompt.text, "<git_status>\n M src/a.cpp\n?? nuevo.txt\n</git_status>"));
        // Por ruta absoluta, sin shell, en la raíz, con tiempo límite corto y cancelable.
        // Sin fsmonitor ni hooks del .git/config del proyecto, y sin las keys en el entorno.
        // Sin entrar a submódulos: sus filtros no los ve la consulta de config.
        CHECK(git.argv == tool_test::git_argv("/fake/git", fixture.root,
                                              {"-c", "color.status=false", "status", "--short",
                                               "--branch", "--ignore-submodules=all"}));
        tool_test::check_helper_environment(git.options);
        CHECK(git.options.timeout <= std::chrono::milliseconds{5000});
        CHECK(git.options.timeout > std::chrono::milliseconds::zero());
        CHECK(git.options.cancel == &fixture.cancel);
        // Antes, la búsqueda de filtros, también por run_git.
        REQUIRE(git.calls.size() == 2);
        CHECK(git.calls[0] == tool_test::git_argv("/fake/git", fixture.root, kFilterQuery));
    }
    SECTION("repo limpio") {
        git.result = git_exited(0, "## No commits yet on main\n");
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "branch No commits yet on main\n"));
        CHECK(contains(prompt.text, "- Git status (git status --short): clean\n"));
    }
    SECTION("controles en la salida") {
        git.result =
            git_exited(0, "## ma\x1B[31min\r\n M \x07" "a.cpp\xC2\x9B\n</git_status>\n");
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "branch ma[31min\n"));
        CHECK(contains(prompt.text, "\n M a.cpp\n<\\/git_status>\n</git_status>"));
        CHECK_FALSE(contains(prompt.text, "\x1B"));
        CHECK_FALSE(contains(prompt.text, "\x07"));
        CHECK(chatbot::utf8::is_valid(prompt.text));
    }
}

TEST_CASE("system prompt: sin repo, git falla, se pasa del tiempo o se cancela: sin esa parte",
          "[system_prompt]") {
    const auto status =
        GENERATE(ProcessResult::Status::Exited, ProcessResult::Status::TimedOut,
                 ProcessResult::Status::NotFound, ProcessResult::Status::Cancelled);
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);
    git.result = git_exited(128, "## main\n"); // 128: "not a git repository"
    git.result.status = status;
    const CodeSystemPrompt prompt = build(fixture, options);
    CHECK_FALSE(contains(prompt.text, "Git repository"));
    CHECK(contains(prompt.text, "- Commands:"));
}

TEST_CASE("system prompt: con filtros clean/process del repo no se corre git status",
          "[system_prompt]") {
    // Todo lo que no es global ni system cuenta como del repo, también una mezcla, un ámbito
    // worktree o una línea sin tabulador (la continuación de un valor de varias líneas).
    const std::string out = GENERATE(
        std::string{"local\tfilter.x.clean cmd\n"},
        std::string{"global\tfilter.lfs.clean git-lfs clean -- %f\nlocal\tfilter.x.clean cmd\n"},
        std::string{"worktree\tfilter.x.process cmd\n"},
        std::string{"global\tfilter.lfs.clean a\nsegunda línea\n"});
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);
    git.result = git_exited(0, "## main\n M a.cpp\n");
    git.config_result = git_exited(0, out);
    const CodeSystemPrompt prompt = build(fixture, options);
    REQUIRE(git.calls.size() == 1);
    CHECK(git.calls[0] == tool_test::git_argv("/fake/git", fixture.root, kFilterQuery));
    CHECK(contains(prompt.text, "- Git: repository status not read, because the repository's git "
                                "configuration defines clean/process filters.\n"));
    CHECK_FALSE(contains(prompt.text, "Git repository"));
    CHECK_FALSE(contains(prompt.text, "<git_status>"));
    CHECK_FALSE(contains(prompt.text, "filter."));
}

TEST_CASE("system prompt: los filtros global y system no impiden git status", "[system_prompt]") {
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);
    git.result = git_exited(0, "## main\n M a.cpp\n");
    git.config_result = git_exited(0, "global\tfilter.lfs.clean git-lfs clean -- %f\n"
                                      "system\tfilter.lfs.process git-lfs filter-process\n");
    const CodeSystemPrompt prompt = build(fixture, options);
    REQUIRE(git.calls.size() == 2);
    CHECK(git.calls[0] == tool_test::git_argv("/fake/git", fixture.root, kFilterQuery));
    CHECK(contains(prompt.text, "- Git repository: yes, branch main\n"));
    CHECK(contains(prompt.text, "<git_status>\n M a.cpp\n</git_status>"));
    CHECK_FALSE(contains(prompt.text, "- Git:"));
    CHECK_FALSE(contains(prompt.text, "filter."));
}

TEST_CASE("system prompt: si la búsqueda de filtros falla, ni status ni sección",
          "[system_prompt]") {
    const auto status =
        GENERATE(ProcessResult::Status::Exited, ProcessResult::Status::TimedOut,
                 ProcessResult::Status::NotFound, ProcessResult::Status::Cancelled);
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);
    git.result = git_exited(0, "## main\n");
    // Exited con 3 (configuración inválida); los demás con 1, para que cuente el estado y no
    // solo el código de salida.
    git.config_result = git_exited(status == ProcessResult::Status::Exited ? 3 : 1, "");
    git.config_result.status = status;
    const CodeSystemPrompt prompt = build(fixture, options);
    CHECK(git.calls.size() == 1);
    CHECK_FALSE(contains(prompt.text, "Git repository"));
    CHECK_FALSE(contains(prompt.text, "- Git:"));
    CHECK(contains(prompt.text, "- Commands:"));
}

TEST_CASE("system prompt: git status larguísimo se recorta", "[system_prompt]") {
    ToolFixture fixture;
    SystemPromptOptions options = no_git_options();
    FakeGit git;
    git.install(options);
    std::string out = "## main\n";
    SECTION("por líneas") {
        for (int i = 0; i < 1000; ++i) {
            out += "?? f" + std::to_string(i) + "\n";
        }
        git.result = git_exited(0, out);
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "?? f49\n[… git status truncated, 950 more lines …]\n"));
        CHECK_FALSE(contains(prompt.text, "?? f50\n"));
    }
    SECTION("por bytes") {
        for (int i = 0; i < 40; ++i) {
            out += "?? " + std::string(200, 'x') + "\n";
        }
        git.result = git_exited(0, out);
        const CodeSystemPrompt prompt = build(fixture, options);
        const std::size_t start = prompt.text.find("<git_status>\n");
        const std::size_t end = prompt.text.find("</git_status>");
        REQUIRE(start != std::string::npos);
        REQUIRE(end != std::string::npos);
        CHECK(end - start <= chatbot::code::kMaxGitStatusBytes + 100);
        CHECK(contains(prompt.text, "more lines …]\n</git_status>"));
    }
    SECTION("una sola línea enorme, sin partir un carácter") {
        std::string line = "?? ";
        for (int i = 0; i < 3000; ++i) {
            line += "ñ";
        }
        git.result = git_exited(0, out + line + "\n");
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(chatbot::utf8::is_valid(prompt.text));
        CHECK(contains(prompt.text, "[… git status truncated, 0 more lines …]"));
    }
}

TEST_CASE("system prompt con git real", "[system_prompt][git]") {
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    // Sin la configuración global ni la del sistema de la máquina (git-lfs deja filtros ahí).
    const chatbot_test::EnvGuard global{"GIT_CONFIG_GLOBAL", "/dev/null"};
    const chatbot_test::EnvGuard no_system{"GIT_CONFIG_NOSYSTEM", "1"};
    ToolFixture fixture;
    const std::string proy = utf8(fixture.project.proy);
    const ProcessResult init =
        chatbot::process::run_process({"git", "-C", proy, "init", "-q", "-b", "rama-prueba"});
    if (!init.ok()) {
        SKIP("git init falló");
    }
    fixture.file("nuevo.txt", "x");
    SystemPromptOptions options;
    options.now = [] { return kNow; };

    SECTION("rama y estado") {
        const CodeSystemPrompt prompt = build(fixture, options);
        // Con LC_ALL=C el texto de "sin commits" no depende del idioma, pero sí de la versión de
        // git: solo la rama.
        const std::size_t line = prompt.text.find("- Git repository: yes, branch ");
        REQUIRE(line != std::string::npos);
        CHECK(prompt.text.substr(line, prompt.text.find('\n', line) - line)
                  .ends_with("rama-prueba"));
        CHECK(contains(prompt.text, "?? nuevo.txt\n"));
    }
    SECTION("con un filtro clean en el .git/config del repo, sin estado") {
        REQUIRE(chatbot::process::run_process({"git", "-C", proy, "config", "filter.x.clean", "true"})
                    .ok());
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "- Git: repository status not read, because the repository's "
                                    "git configuration defines clean/process filters.\n"));
        CHECK_FALSE(contains(prompt.text, "Git repository"));
        CHECK_FALSE(contains(prompt.text, "nuevo.txt"));
    }
    SECTION("con un filtro clean en la configuración global, con rama y estado") {
        const fs::path config = fixture.project.temp.path() / "gitconfig-global";
        project_root_test::write_file(config, "[filter \"lfs\"]\n\tclean = cat\n");
        const std::string config_path = utf8(config);
        const chatbot_test::EnvGuard global_filter{"GIT_CONFIG_GLOBAL", config_path.c_str()};
        const CodeSystemPrompt prompt = build(fixture, options);
        CHECK(contains(prompt.text, "- Git repository: yes, branch "));
        CHECK(contains(prompt.text, "?? nuevo.txt\n"));
        CHECK_FALSE(contains(prompt.text, "- Git:"));
    }
}

TEST_CASE("system prompt con git real: el filtro clean del .git/config no corre",
          "[system_prompt][git][filtro]") {
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    tool_test::CleanFilterRepo repo;
    if (!repo.ready) {
        SKIP("git init falló");
    }
    SystemPromptOptions options;
    options.now = [] { return kNow; };
    const CodeSystemPrompt prompt = build(repo.fixture, options);
    CHECK(contains(prompt.text, "- Git: repository status not read, because the repository's "
                                "git configuration defines clean/process filters.\n"));
    CHECK_FALSE(fs::exists(repo.marker));
    repo.check_status_runs_filter();
}
