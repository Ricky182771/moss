#ifndef CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP
#define CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP

// Ayudas de las pruebas de herramientas: un proyecto temporal (project_root_fixture.hpp) con una
// carpeta de Moss dentro de la raíz, y el registro listo para ejecutar.

#include "chatbot/cancel_token.h"
#include "chatbot/child_process.h"
#include "chatbot/code/tool.h"

#include "env_guard.hpp"
#include "project_root_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tool_test {

/// El argv exacto con que glob, grep y las instrucciones lanzan git (run_git): sin nada que un
/// .git/config ajeno pueda ejecutar.
inline std::vector<std::string> git_argv(const std::string& git,
                                         const chatbot::code::ProjectRoot& root,
                                         const std::vector<std::string>& args) {
    std::vector<std::string> argv{git, "--no-optional-locks", "-c", "core.fsmonitor=",
                                  "-c", "core.hooksPath=/dev/null", "-C",
                                  project_root_test::utf8(root.path())};
    argv.insert(argv.end(), args.begin(), args.end());
    return argv;
}

/// git y rg corren sin ninguna CHAT_* (las keys) y con salida estable.
inline void check_helper_environment(const chatbot::process::ProcessOptions& options) {
    CHECK(options.env_remove_prefixes == std::vector<std::string>{"CHAT_"});
    CHECK(options.env_set == std::map<std::string, std::string>{{"LC_ALL", "C"},
                                                                {"GIT_TERMINAL_PROMPT", "0"},
                                                                {"GIT_OPTIONAL_LOCKS", "0"}});
}

namespace fs = std::filesystem;
using chatbot::code::ToolContext;
using chatbot::code::ToolRegistry;
using chatbot::code::ToolResult;
using chatbot::code::ToolState;
using chatbot::code::ToolStatus;

struct ToolFixture {
    project_root_test::Project project;
    fs::path moss_inside = project.proy / "datos-moss";
    chatbot::code::ProjectRoot root = make_root();
    chatbot::CancelToken cancel;
    ToolState state;
    ToolContext context{root, cancel, state};
    ToolRegistry registry = ToolRegistry::standard();

    ToolResult run(std::string_view name, std::string_view arguments) {
        return registry.execute(name, arguments, context);
    }

    /// Escribe proy/<name> y devuelve su ruta.
    fs::path file(const std::string& name, const std::string& text) {
        const fs::path path = project.proy / project_root_test::from_utf8(name);
        project_root_test::write_file(path, text);
        return path;
    }

    [[nodiscard]] bool was_read(const std::string& name) const {
        return state.reads.contains(fs::canonical(project.proy / project_root_test::from_utf8(name)));
    }

private:
    chatbot::code::ProjectRoot make_root() {
        fs::create_directories(moss_inside);
        project_root_test::write_file(moss_inside / "credentials.json", "{\"k\": \"secreto\"}");
        return project_root_test::open_root(project.proy, {project.moss, moss_inside});
    }
};

/// Repo de git real cuyo .git/config tiene un filtro clean que crea `marker` (fuera del repo)
/// cada vez que git lo corre, aplicado por .gitattributes a a.txt, que está confirmado y luego
/// modificado. Sin la configuración global ni la del sistema de la máquina. Quien lo usa salta
/// antes si no hay git (o en Windows) y revisa `ready`.
struct CleanFilterRepo {
    chatbot_test::EnvGuard global{"GIT_CONFIG_GLOBAL", "/dev/null"};
    chatbot_test::EnvGuard no_system{"GIT_CONFIG_NOSYSTEM", "1"};
    ToolFixture fixture;
    fs::path marker = fixture.project.temp.path() / "filtro-corrio";
    bool ready = setup();

    /// Control positivo: git status sí corre el filtro (si no, el montaje no prueba nada).
    void check_status_runs_filter() const {
        REQUIRE_FALSE(fs::exists(marker));
        REQUIRE(git({"status", "--porcelain"}));
        CHECK(fs::exists(marker));
    }

private:
    [[nodiscard]] bool git(std::vector<std::string> args) const {
        args.insert(args.begin(), {"git", "-C", project_root_test::utf8(fixture.project.proy)});
        return chatbot::process::run_process(args).ok();
    }

    bool setup() {
        if (!git({"init", "-q"})) {
            return false;
        }
        fixture.file("a.txt", "contenido-uno\n");
        REQUIRE(git({"add", "a.txt"}));
        REQUIRE(git({"-c", "user.name=prueba", "-c", "user.email=prueba@example.invalid", "commit",
                     "-q", "-m", "inicio"}));
        // Después del commit, porque git add sí corre el filtro clean.
        const std::string marker_path = project_root_test::utf8(marker);
        REQUIRE(marker_path.find('\'') == std::string::npos);
        REQUIRE(git({"config", "filter.marca.clean", "touch '" + marker_path + "'; cat"}));
        fixture.file(".gitattributes", "a.txt filter=marca\n");
        // Solo git lo oculta: si glob o grep no pasaran por git ls-files, saldría en su resultado.
        fixture.file(".gitignore", "ignorado.txt\n");
        fixture.file("ignorado.txt", "contenido-dos\n");
        // Mismo tamaño y otro contenido: con otro tamaño git status ya sabe que cambió sin leerlo
        // y no corre el filtro; así tiene que comparar el contenido (el control lo confirma).
        fixture.file("a.txt", "contenido-dos\n");
        REQUIRE_FALSE(fs::exists(marker));
        return true;
    }
};

} // namespace tool_test

#endif // CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP
