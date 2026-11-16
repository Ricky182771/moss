// La herramienta glob (guias/18-moss-code.md, sección 4), por el registro, con un lanzador falso
// para git y una prueba con git real.

#include "tool_fixture.hpp"

#include "chatbot/child_process.h"
#include "chatbot/platform.h"

#include "tool_internal.h"
#include "tool_schema.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using chatbot::process::ProcessOptions;
using chatbot::process::ProcessResult;
using project_root_test::from_utf8;
using tool_test::ToolFixture;
using tool_test::ToolResult;
using tool_test::ToolStatus;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    for (std::size_t start = 0; start <= text.size();) {
        std::size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::vector<std::string> sorted_lines(const std::string& text) {
    std::vector<std::string> lines = split_lines(text);
    std::sort(lines.begin(), lines.end());
    return lines;
}

/// Un lanzador que responde que git terminó bien con esta salida (-z).
void fake_git(ToolFixture& fixture, std::string out, std::vector<std::string>* argv = nullptr,
              ProcessOptions* options = nullptr) {
    fixture.context.run_process = [out = std::move(out), argv, options](
                                      const std::vector<std::string>& args,
                                      const ProcessOptions& given) {
        if (argv != nullptr) {
            *argv = args;
        }
        if (options != nullptr) {
            *options = given;
        }
        ProcessResult result;
        result.status = ProcessResult::Status::Exited;
        result.exit_code = 0;
        result.out = out;
        return result;
    };
}

/// Un lanzador al que no le funciona nada (sin git, no es repo, Windows…).
void no_git(ToolFixture& fixture) {
    fixture.context.run_process = [](const std::vector<std::string>&, const ProcessOptions&) {
        return ProcessResult{};
    };
}

/// glob con git en una ruta falsa (sin depender del PATH ni del sistema): el lanzador falso
/// responde por él.
ToolResult glob_with_fake_git(ToolFixture& fixture, std::string_view raw) {
    static const auto tool = chatbot::code::make_glob_tool();
    nlohmann::json arguments = nlohmann::json::parse(raw);
    REQUIRE_FALSE(chatbot::code::validate_arguments(tool->schema, arguments).has_value());
    return chatbot::code::run_glob(arguments, fixture.context, std::string{"/fake/git"});
}

void touch(const fs::path& path, std::chrono::hours age) {
    fs::last_write_time(path, fs::file_time_type::clock::now() - age);
}

} // namespace

TEST_CASE("glob con la lista de git: lo ajeno no aparece", "[glob]") {
    ToolFixture fixture;
    fixture.file("b.cpp", "x");
    std::vector<std::string> argv;
    ProcessOptions options;
    using namespace std::string_literals;
    fake_git(fixture,
             "src/a.txt\0../fuera/secreto.txt\0datos-moss/credentials.json\0borrado.txt\0b.cpp\0"s +
                 "src\0"s + "src/a.txt\0"s,
             &argv, &options);
    const ToolResult result = glob_with_fake_git(fixture, R"({"pattern": "*"})");
    CHECK(result.status == ToolStatus::Ok);
    // El directorio "src" y el duplicado no cuentan; fuera, bloqueado y borrado, tampoco.
    CHECK(sorted_lines(result.text) == std::vector<std::string>{"b.cpp", "src/a.txt"});
    CHECK(result.display == "2 archivos");
    // Sin fsmonitor ni hooks del .git/config del proyecto, y sin las keys en el entorno.
    CHECK(argv == tool_test::git_argv("/fake/git", fixture.root,
                                      {"ls-files", "-z", "--cached", "--others",
                                       "--exclude-standard"}));
    tool_test::check_helper_environment(options);
    CHECK(options.cancel == &fixture.cancel);
}

TEST_CASE("glob con git: path acota y el patrón se compara contra esa carpeta", "[glob]") {
    ToolFixture fixture;
    fixture.file("b.txt", "x");
    using namespace std::string_literals;
    fake_git(fixture, "src/a.txt\0b.txt\0"s);
    const ToolResult result =
        glob_with_fake_git(fixture, R"({"pattern": "*.txt", "path": "src"})");
    CHECK(result.text == "src/a.txt");
    CHECK(result.display == "1 archivo");
}

TEST_CASE("glob cae al recorrido si git falla, no está o llega al tope", "[glob]") {
    ToolFixture fixture;
    fixture.file("b.cpp", "x");

    no_git(fixture);
    CHECK(sorted_lines(fixture.run("glob", R"({"pattern": "*"})").text) ==
          std::vector<std::string>{"b.cpp", "src/a.txt"});

    fixture.context.run_process = [](const std::vector<std::string>&, const ProcessOptions&) {
        ProcessResult result;
        result.status = ProcessResult::Status::Exited;
        result.exit_code = 128; // No es un repo.
        return result;
    };
    CHECK(sorted_lines(glob_with_fake_git(fixture, R"({"pattern": "*"})").text) ==
          std::vector<std::string>{"b.cpp", "src/a.txt"});

    // Lista incompleta: salida al tope. Lo que dijo git se ignora.
    fake_git(fixture, std::string(chatbot::process::kMaxProcessOutput, 'x'));
    CHECK(sorted_lines(glob_with_fake_git(fixture, R"({"pattern": "*"})").text) ==
          std::vector<std::string>{"b.cpp", "src/a.txt"});

    // Sin git en el PATH: ni se intenta.
    std::vector<std::string> argv;
    fake_git(fixture, "nada.txt", &argv);
    nlohmann::json arguments = {{"pattern", "*"}};
    CHECK(sorted_lines(chatbot::code::run_glob(arguments, fixture.context, std::nullopt).text) ==
          std::vector<std::string>{"b.cpp", "src/a.txt"});
    CHECK(argv.empty());
}

TEST_CASE("find_program solo da rutas absolutas", "[glob]") {
    if (const std::optional<std::string> git = chatbot::code::find_program("git")) {
        CHECK(git->starts_with('/'));
    }
    CHECK_FALSE(chatbot::code::find_program("no-existe-este-programa-moss").has_value());
}

TEST_CASE("glob: el recorrido salta .git, node_modules, build* y la carpeta de Moss", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    for (const char* dir : {".git", "node_modules/p", "build-debug", "build", "docs"}) {
        fs::create_directories(fixture.project.proy / dir);
    }
    fixture.file(".git/config", "x");
    fixture.file("node_modules/p/i.js", "x");
    fixture.file("build-debug/x.o", "x");
    fixture.file("build/y.o", "x");
    fixture.file("docs/guia.md", "x");
    fixture.file("keep.md", "x");
    const ToolResult result = fixture.run("glob", R"({"pattern": "**/*"})");
    CHECK(sorted_lines(result.text) ==
          std::vector<std::string>{"docs/guia.md", "keep.md", "src/a.txt"});
    CHECK(sorted_lines(fixture.run("glob", R"({"pattern": "*.md"})").text) ==
          std::vector<std::string>{"docs/guia.md", "keep.md"});
    CHECK(sorted_lines(fixture.run("glob", R"({"pattern": "docs/*.md"})").text) ==
          std::vector<std::string>{"docs/guia.md"});
}

TEST_CASE("glob ordena por fecha de modificación, el más reciente primero", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    touch(fixture.file("t1.txt", "x"), std::chrono::hours{30});
    touch(fixture.file("t2.txt", "x"), std::chrono::hours{1});
    touch(fixture.file("t3.txt", "x"), std::chrono::hours{10});
    CHECK(fixture.run("glob", R"({"pattern": "t*.txt"})").text == "t2.txt\nt3.txt\nt1.txt");
}

TEST_CASE("glob corta a 200 y avisa cuántos eran", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    fs::create_directories(fixture.project.proy / "many");
    for (int i = 0; i < 205; ++i) {
        fixture.file("many/f" + std::to_string(i) + ".txt", "x");
    }
    const ToolResult result = fixture.run("glob", R"({"pattern": "many/*"})");
    CHECK(result.status == ToolStatus::Ok);
    const std::vector<std::string> lines = split_lines(result.text);
    REQUIRE(lines.size() == 202);
    CHECK(lines[200].empty());
    CHECK(lines[201].starts_with("(Showing 200 of 205 files"));
    CHECK(lines[201].find("Narrow the pattern") != std::string::npos);
    CHECK(result.display == "200 de 205 archivos");

    const ToolResult exact = fixture.run("glob", R"({"pattern": "many/f1?.txt"})");
    CHECK(split_lines(exact.text).size() == 10);
    CHECK(exact.text.find("Showing") == std::string::npos);
}

TEST_CASE("glob: path que no es carpeta, no existe o está fuera", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    const ToolResult file = fixture.run("glob", R"({"pattern": "*", "path": "src/a.txt"})");
    CHECK(file.status == ToolStatus::Error);
    CHECK(file.text.starts_with("Path is not a directory"));
    CHECK(file.display == "No es una carpeta");
    CHECK(fixture.run("glob", R"({"pattern": "*", "path": "nada"})").status == ToolStatus::Error);

    const ToolResult outside = fixture.run("glob", R"({"pattern": "*", "path": "../fuera"})");
    CHECK(outside.status == ToolStatus::Error);
    CHECK(outside.display == "Fuera del proyecto");
    CHECK(outside.text.find("secreto") == std::string::npos);

    const ToolResult blocked = fixture.run("glob", R"({"pattern": "*", "path": "datos-moss"})");
    CHECK(blocked.status == ToolStatus::Error);
    CHECK(blocked.text.find("no-debe-salir") == std::string::npos);
}

TEST_CASE("glob sin coincidencias no es un error", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    const ToolResult result = fixture.run("glob", R"({"pattern": "**/*.zig"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(result.text.starts_with("No files matched the pattern \"**/*.zig\""));
    CHECK(result.display == "Sin coincidencias");
}

TEST_CASE("glob cancelado da el error de cancelación", "[glob]") {
    ToolFixture fixture;
    no_git(fixture);
    fixture.cancel.cancel();
    const ToolResult result = fixture.run("glob", R"({"pattern": "*"})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text == "Cancelled by the user.");

    ToolFixture with_git;
    with_git.context.run_process = [&with_git](const std::vector<std::string>&,
                                               const ProcessOptions&) {
        with_git.cancel.cancel();
        ProcessResult cancelled;
        cancelled.status = ProcessResult::Status::Cancelled;
        return cancelled;
    };
    CHECK(glob_with_fake_git(with_git, R"({"pattern": "*"})").text == "Cancelled by the user.");
}

TEST_CASE("glob: argumentos y resumen", "[glob]") {
    ToolFixture fixture;
    CHECK(fixture.run("glob", "{}").status == ToolStatus::InvalidArguments);
    CHECK(fixture.run("glob", R"({"pattern": 3})").status == ToolStatus::InvalidArguments);
    CHECK(fixture.registry.label("glob") == "Buscar archivos");
    CHECK(fixture.registry.summarize("glob", R"({"pattern": "**/*.cpp", "path": "cli/"})") ==
          "Buscar archivos **/*.cpp en cli/");
    CHECK(fixture.registry.summarize("glob", R"({"pattern": "*.md"})") == "Buscar archivos *.md");
}

TEST_CASE("glob con git real respeta .gitignore", "[glob][git]") {
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    ToolFixture fixture;
    const ProcessResult init = chatbot::process::run_process(
        {"git", "-C", project_root_test::utf8(fixture.project.proy), "init", "-q"});
    if (!init.ok()) {
        SKIP("git init falló");
    }
    fs::create_directories(fixture.project.proy / "sub");
    fixture.file(".gitignore", "ignorado.txt\n");
    fixture.file("a.txt", "x");
    fixture.file("ignorado.txt", "x");
    fixture.file("sub/b.txt", "x");
    const ToolResult result = fixture.run("glob", R"({"pattern": "**/*.txt"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(sorted_lines(result.text) == std::vector<std::string>{"a.txt", "src/a.txt", "sub/b.txt"});
}

TEST_CASE("glob con git real no corre el filtro clean del .git/config", "[glob][git][filtro]") {
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    tool_test::CleanFilterRepo repo;
    if (!repo.ready) {
        SKIP("git init falló");
    }
    const ToolResult result = repo.fixture.run("glob", R"({"pattern": "**/*.txt"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(sorted_lines(result.text) == std::vector<std::string>{"a.txt", "src/a.txt"});
    CHECK_FALSE(fs::exists(repo.marker));
    repo.check_status_runs_filter();
}
