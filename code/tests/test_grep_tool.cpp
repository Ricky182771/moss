// La herramienta grep (guias/18-moss-code.md, sección 4, D12). Los casos comunes corren contra
// las dos implementaciones: la búsqueda propia y rg real (se salta si no está en el PATH). Luego,
// lo que solo tiene la búsqueda propia, y rg con un lanzador falso.

#include "tool_fixture.hpp"

#include "chatbot/child_process.h"
#include "chatbot/platform.h"

#include "performance.hpp"
#include "tool_internal.h"
#include "tool_schema.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using chatbot::process::ProcessOptions;
using chatbot::process::ProcessResult;
using project_root_test::utf8;
using tool_test::ToolFixture;
using tool_test::ToolResult;
using tool_test::ToolStatus;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

enum class Engine { Own, Rg };

/// rg real para Engine::Rg (SKIP si no está); nullopt para la búsqueda propia.
std::optional<std::string> rg_for(Engine engine) {
    if (engine == Engine::Own) {
        return std::nullopt;
    }
    std::optional<std::string> rg = chatbot::process::find_in_path("rg");
    if (!rg) {
        SKIP("sin rg en el PATH (o Windows): este caso solo corre con la búsqueda propia");
    }
    return rg;
}

/// Valida los argumentos con el esquema de grep (como el registro) y ejecuta con ese rg y ese git
/// (por defecto, el git real si está).
ToolResult grep(ToolFixture& fixture, const std::optional<std::string>& rg, std::string_view raw,
                const std::optional<std::string>& git = chatbot::code::find_program("git")) {
    static const auto tool = chatbot::code::make_grep_tool();
    json arguments = json::parse(raw);
    REQUIRE_FALSE(chatbot::code::validate_arguments(tool->schema, arguments).has_value());
    return chatbot::code::run_grep(arguments, fixture.context, rg, git);
}

/// Cambia la carpeta actual mientras vive (rg compara sus globs según ella).
class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const fs::path& path) : previous_{fs::current_path()} {
        fs::current_path(path);
    }
    ~ScopedCurrentPath() {
        std::error_code ignored;
        fs::current_path(previous_, ignored);
    }
    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
    fs::path previous_;
};

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

constexpr std::size_t kMiB = 1024 * 1024;

} // namespace

// ---- Casos comunes: búsqueda propia y rg real ---------------------------------------------

TEST_CASE("grep: los tres modos de salida", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("uno.txt", "hola mundo\nadiós\nhola otra vez\n");
    fixture.file("src/dos.txt", "nada\nhola\n");

    const ToolResult files = grep(fixture, rg, R"({"pattern": "hola"})");
    CHECK(files.status == ToolStatus::Ok);
    CHECK(files.text == "src/dos.txt\nuno.txt");
    CHECK(files.display == "2 archivos");

    const ToolResult content = grep(fixture, rg, R"({"pattern": "hola", "output_mode": "content"})");
    CHECK(content.text == "src/dos.txt:2:hola\nuno.txt:1:hola mundo\nuno.txt:3:hola otra vez");
    CHECK(content.display == "3 coincidencias");

    const ToolResult count = grep(fixture, rg, R"({"pattern": "hola", "output_mode": "count"})");
    CHECK(count.text == "src/dos.txt:1\nuno.txt:2");
    CHECK(count.display == "3 coincidencias en 2 archivos");
}

TEST_CASE("grep: context junta los grupos y los separa con --", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("c.txt", "l1\nl2\nX3\nl4\nl5\nl6\nl7\nX8\nl9\n");
    const ToolResult result =
        grep(fixture, rg, R"({"pattern": "X", "output_mode": "content", "context": 1})");
    CHECK(result.text ==
          "c.txt-2-l2\nc.txt:3:X3\nc.txt-4-l4\n--\nc.txt-7-l7\nc.txt:8:X8\nc.txt-9-l9");
    CHECK(result.display == "2 coincidencias");

    // Más de 10 cuenta como 10: aquí, todo el archivo en un solo grupo.
    const ToolResult wide =
        grep(fixture, rg, R"({"pattern": "X", "output_mode": "content", "context": 50})");
    CHECK(split_lines(wide.text).size() == 9);
    CHECK(wide.text.find("--") == std::string::npos);
}

TEST_CASE("grep: ignore_case", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("m.txt", "HOLA\n");
    CHECK(grep(fixture, rg, R"({"pattern": "hola"})").display == "Sin coincidencias");
    CHECK(grep(fixture, rg, R"({"pattern": "hola", "ignore_case": true})").text == "m.txt");
}

TEST_CASE("grep: glob filtra por nombre o por ruta relativa a path", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("x.cpp", "foo\n");
    fixture.file("src/y.cpp", "foo\n");
    fixture.file("z.h", "foo\n");
    CHECK(grep(fixture, rg, R"({"pattern": "foo", "glob": "*.cpp"})").text == "src/y.cpp\nx.cpp");
    CHECK(grep(fixture, rg, R"({"pattern": "foo", "glob": "src/*.cpp"})").text == "src/y.cpp");
    CHECK(grep(fixture, rg, R"({"pattern": "foo", "glob": "*.cpp", "path": "src"})").text ==
          "src/y.cpp");
}

TEST_CASE("grep: path a un archivo, a una carpeta, que no existe o fuera", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("x.cpp", "foo\n");
    fixture.file("src/y.cpp", "foo\n");
    CHECK(grep(fixture, rg, R"({"pattern": "foo", "path": "x.cpp", "output_mode": "content"})")
              .text == "x.cpp:1:foo");
    CHECK(grep(fixture, rg, R"({"pattern": "foo", "path": "src"})").text == "src/y.cpp");

    const ToolResult missing = grep(fixture, rg, R"({"pattern": "foo", "path": "nada"})");
    CHECK(missing.status == ToolStatus::Error);
    CHECK(missing.display == "No existe");
    const ToolResult outside = grep(fixture, rg, R"({"pattern": "secreto", "path": "../fuera"})");
    CHECK(outside.status == ToolStatus::Error);
    CHECK(outside.display == "Fuera del proyecto");
    CHECK(grep(fixture, rg, R"({"pattern": "k", "path": "datos-moss"})").status ==
          ToolStatus::Error);
}

TEST_CASE("grep sin coincidencias no es un error", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    const ToolResult result = grep(fixture, rg, R"({"pattern": "zzz"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(result.text == "No matches for the pattern \"zzz\" in .");
    CHECK(result.display == "Sin coincidencias");
}

TEST_CASE("grep corta en 200 y avisa que hay más", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fs::create_directories(fixture.project.proy / "many");
    for (int i = 0; i < 205; ++i) {
        fixture.file("many/f" + std::to_string(1000 + i) + ".txt", "hit\n");
    }
    std::string lines;
    for (int i = 0; i < 250; ++i) {
        lines += "hit\n";
    }
    fixture.file("big.txt", lines);

    const ToolResult files = grep(fixture, rg, R"({"pattern": "hit", "path": "many"})");
    std::vector<std::string> shown = split_lines(files.text);
    REQUIRE(shown.size() == 202);
    CHECK(shown[0] == "many/f1000.txt");
    CHECK(shown[199] == "many/f1199.txt");
    CHECK(shown[200].empty());
    CHECK(shown[201].starts_with("(Showing the first 200 files; there are more."));
    CHECK(files.display == "más de 200 archivos");

    const ToolResult count =
        grep(fixture, rg, R"({"pattern": "hit", "path": "many", "output_mode": "count"})");
    CHECK(split_lines(count.text).size() == 202);

    const ToolResult content =
        grep(fixture, rg, R"({"pattern": "hit", "path": "big.txt", "output_mode": "content"})");
    shown = split_lines(content.text);
    REQUIRE(shown.size() == 202);
    CHECK(shown[199] == "big.txt:200:hit");
    CHECK(shown[201].starts_with("(Showing the first 200 matches; there are more."));
    CHECK(content.display == "más de 200 coincidencias");

    const ToolResult exact =
        grep(fixture, rg, R"({"pattern": "hit", "path": "many", "glob": "f11*.txt"})");
    CHECK(split_lines(exact.text).size() == 100);
    CHECK(exact.display == "100 archivos");
}

TEST_CASE("grep: UTF-8 con acentos, en el texto y en el nombre", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("ñandú.txt", "canción\nacción rápida\n");
    CHECK(grep(fixture, rg, R"({"pattern": "acción", "output_mode": "content"})").text ==
          "ñandú.txt:2:acción rápida");
    CHECK(grep(fixture, rg, R"({"pattern": "^acción r"})").text == "ñandú.txt");
}

TEST_CASE("grep: las líneas largas se muestran recortadas", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("ancho.txt", std::string(600, 'y') + "hola\n");
    const ToolResult result = grep(fixture, rg, R"({"pattern": "hola", "output_mode": "content"})");
    CHECK(result.text == "ancho.txt:1:" + std::string(500, 'y') + " [\xE2\x80\xA6 line truncated]");
}

TEST_CASE("grep ignora los binarios y los archivos de más de 1 MiB", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    fixture.file("bin.dat", std::string("hola\0mundo\n", 11));
    fixture.file("grande.txt", "hola\n" + std::string(kMiB, 'x'));
    fixture.file("justo.txt", "hola\n" + std::string(kMiB - 5, 'x'));
    fixture.file("texto.txt", "hola\n");
    CHECK(grep(fixture, rg, R"({"pattern": "hola"})").text == "justo.txt\ntexto.txt");
}

TEST_CASE("grep: la carpeta de Moss y los enlaces hacia fuera no se ven", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    std::error_code error;
    fs::create_directory_symlink(fixture.project.outside, fixture.project.proy / "enlace", error);
    if (!error) {
        fs::create_symlink(fixture.project.outside / "secreto.txt", fixture.project.proy / "s.txt",
                           error);
    }
    if (error) {
        SKIP("no se pudo crear un enlace simbólico: " << error.message());
    }
    // datos-moss/credentials.json (dentro de la raíz) y fuera/secreto.txt dicen "secreto".
    CHECK(grep(fixture, rg, R"({"pattern": "secreto"})").display == "Sin coincidencias");
    CHECK(grep(fixture, rg, R"({"pattern": "no-debe-salir"})").display == "Sin coincidencias");
}

TEST_CASE("grep: un patrón inválido es un error claro", "[grep]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    ToolFixture fixture;
    const ToolResult result = grep(fixture, rg, R"({"pattern": "(abc"})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text.starts_with("Invalid regular expression: "));
    CHECK(result.display == "Patrón inválido");
}

TEST_CASE("grep respeta .gitignore", "[grep][git]") {
    const std::optional<std::string> rg = rg_for(GENERATE(Engine::Own, Engine::Rg));
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    ToolFixture fixture;
    const ProcessResult init = chatbot::process::run_process(
        {"git", "-C", utf8(fixture.project.proy), "init", "-q"});
    if (!init.ok()) {
        SKIP("git init falló");
    }
    fixture.file(".gitignore", "ignorado.txt\n");
    fixture.file("a.txt", "clave\n");
    fixture.file("ignorado.txt", "clave\n");
    CHECK(grep(fixture, rg, R"({"pattern": "clave"})").text == "a.txt");
}

TEST_CASE("grep con la lista de git no corre el filtro clean del .git/config",
          "[grep][git][filtro]") {
    if (chatbot::current_os() == chatbot::Os::Windows ||
        !chatbot::process::find_in_path("git").has_value()) {
        SKIP("sin git en el PATH (o Windows, donde no se lanzan programas)");
    }
    tool_test::CleanFilterRepo repo;
    if (!repo.ready) {
        SKIP("git init falló");
    }
    CHECK(grep(repo.fixture, std::nullopt, R"({"pattern": "contenido-dos"})").text == "a.txt");
    CHECK_FALSE(fs::exists(repo.marker));
    repo.check_status_runs_filter();
}

// ---- Solo la búsqueda propia ---------------------------------------------------------------

TEST_CASE("grep propio: salta las líneas de más de 4 KiB", "[grep]") {
    ToolFixture fixture;
    fixture.file("largo.txt", std::string(5000, 'x') + "hola\nhola corta\n");
    CHECK(grep(fixture, std::nullopt, R"({"pattern": "hola", "output_mode": "content"})").text ==
          "largo.txt:2:hola corta");
}

TEST_CASE("grep propio: los patrones catastróficos terminan a tiempo", "[grep][perf]") {
    ToolFixture fixture;
    std::string lines;
    for (int i = 0; i < 20; ++i) {
        lines += std::string(4000, 'a') + "!\n";
    }
    fixture.file("malo.txt", lines);
    const auto start = std::chrono::steady_clock::now();
    for (const char* pattern : {"(a+)+$", "(a|aa)+$", "(a*)*b", "(.*a){12}$", "^(a+)+$"}) {
        INFO(pattern);
        const ToolResult result = grep(fixture, std::nullopt,
                                       json{{"pattern", pattern}, {"path", "malo.txt"}}.dump());
        CHECK(result.status == ToolStatus::Ok);
        CHECK(result.display == "Sin coincidencias");
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    WARN("5 patrones catastróficos sobre 20 líneas de 4000 bytes: " << ms << " ms");
    CHECK(grep(fixture, std::nullopt, R"({"pattern": "^(a+)+!$", "path": "malo.txt"})").display ==
          "1 archivo");
    // Con retroceso no terminarían nunca: este tope solo dice que no se colgó.
    CHECK(ms < 60000.0);
    if (chatbot_test::strict_performance()) {
        CHECK(ms < 2000.0);
    }
}

TEST_CASE("grep propio: cientos de grupos que capturan terminan a tiempo", "[grep][perf]") {
    ToolFixture fixture;
    std::string lines;
    for (int i = 0; i < 5; ++i) {
        lines += std::string(4000, 'a') + "\n";
    }
    fixture.file("grupos.txt", lines);
    std::string groups;
    for (int i = 0; i < 300; ++i) {
        groups += "(.)";
    }
    const auto start = std::chrono::steady_clock::now();
    // Sin coincidencia (no hay '!'): cada línea se recorre entera. Con coincidencia, igual.
    const ToolResult none = grep(fixture, std::nullopt,
                                 json{{"pattern", groups + "!"}, {"path", "grupos.txt"}}.dump());
    const ToolResult found = grep(fixture, std::nullopt,
                                  json{{"pattern", groups}, {"path", "grupos.txt"}}.dump());
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    WARN("300 grupos que capturan sobre 5 líneas de 4000 bytes: " << ms << " ms");
    CHECK(none.status == ToolStatus::Ok);
    CHECK(none.display == "Sin coincidencias");
    CHECK(found.text == "grupos.txt");
    CHECK(ms < 60000.0);
    if (chatbot_test::strict_performance()) {
        CHECK(ms < 2000.0);
    }
}

TEST_CASE("grep propio: rechaza lo que su motor no puede acotar", "[grep]") {
    ToolFixture fixture;
    for (const char* pattern : {"(a{100}){100}", "a{1000000}", "(?=a)b", "(?!a)b", "(a)\\1"}) {
        INFO(pattern);
        const ToolResult result =
            grep(fixture, std::nullopt, json{{"pattern", pattern}}.dump());
        CHECK(result.status == ToolStatus::Error);
        CHECK(result.display == "Patrón inválido");
    }
    // Un paréntesis escapado no es un lookahead, y un ")" de más no cierra nada.
    fixture.file("p.txt", "=x\n");
    CHECK(grep(fixture, std::nullopt, json{{"pattern", "\\(?=x"}}.dump()).text == "p.txt");
    CHECK(grep(fixture, std::nullopt, R"({"pattern": "a)|(b"})").status == ToolStatus::Error);
    CHECK(grep(fixture, std::nullopt, json{{"pattern", std::string(1001, 'a')}}.dump()).status ==
          ToolStatus::Error);
}

TEST_CASE("grep propio con la lista de git: argv y entorno sin nada ajeno", "[grep]") {
    ToolFixture fixture;
    fixture.file("a.txt", "clave\n");
    std::vector<std::string> argv;
    ProcessOptions options;
    fixture.context.run_process = [&](const std::vector<std::string>& args,
                                      const ProcessOptions& given) {
        argv = args;
        options = given;
        ProcessResult result;
        result.status = ProcessResult::Status::Exited;
        result.exit_code = 0;
        result.out = std::string{"a.txt\0", 6};
        return result;
    };
    CHECK(grep(fixture, std::nullopt, R"({"pattern": "clave"})", std::string{"/fake/git"}).text ==
          "a.txt");
    CHECK(argv == tool_test::git_argv("/fake/git", fixture.root,
                                      {"ls-files", "-z", "--cached", "--others",
                                       "--exclude-standard"}));
    tool_test::check_helper_environment(options);
    CHECK(options.cancel == &fixture.cancel);
}

TEST_CASE("grep propio cancelado", "[grep]") {
    ToolFixture fixture;
    fixture.cancel.cancel();
    const ToolResult result = grep(fixture, std::nullopt, R"({"pattern": "a"})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text == "Cancelled by the user.");

    ToolFixture during_git;
    during_git.context.run_process = [&during_git](const std::vector<std::string>&,
                                                   const ProcessOptions&) {
        during_git.cancel.cancel();
        ProcessResult cancelled;
        cancelled.status = ProcessResult::Status::Cancelled;
        return cancelled;
    };
    CHECK(grep(during_git, std::nullopt, R"({"pattern": "a"})", std::string{"/fake/git"}).text ==
          "Cancelled by the user.");
}

// ---- Solo rg, con un lanzador falso --------------------------------------------------------

namespace {

constexpr std::string_view kFakeRg = "/fake/rg";

/// rg responde con esta salida; git (para la búsqueda propia) no funciona: se recorre.
void fake_rg(ToolFixture& fixture, ProcessResult rg_result,
             std::vector<std::string>* argv = nullptr, ProcessOptions* options = nullptr) {
    fixture.context.run_process = [rg_result = std::move(rg_result), argv, options](
                                      const std::vector<std::string>& args,
                                      const ProcessOptions& given) {
        if (args.empty() || args[0] != kFakeRg) {
            return ProcessResult{};
        }
        if (argv != nullptr) {
            *argv = args;
        }
        if (options != nullptr) {
            *options = given;
        }
        return rg_result;
    };
}

ProcessResult exited(int code, std::string out, std::string err = {}) {
    ProcessResult result;
    result.status = ProcessResult::Status::Exited;
    result.exit_code = code;
    result.out = std::move(out);
    result.err = std::move(err);
    return result;
}

std::string message(std::string_view type, json data) {
    return json{{"type", std::string{type}}, {"data", std::move(data)}}.dump() + "\n";
}

std::string rg_line(std::string_view type, const fs::path& path, std::uint64_t number,
                    json lines) {
    return message(type, {{"path", {{"text", utf8(path)}}},
                          {"lines", std::move(lines)},
                          {"line_number", number},
                          {"absolute_offset", 0},
                          {"submatches", json::array()}});
}

std::string rg_match(const fs::path& path, std::uint64_t number, const std::string& text) {
    return rg_line("match", path, number, {{"text", text + "\n"}});
}

std::string rg_end(const fs::path& path, json binary_offset = nullptr) {
    return message("end", {{"path", {{"text", utf8(path)}}},
                           {"binary_offset", std::move(binary_offset)},
                           {"stats", json::object()}});
}

std::string rg_summary() {
    return message("summary", {{"stats", json::object()}});
}

ToolResult fake_grep(ToolFixture& fixture, std::string_view raw) {
    return grep(fixture, std::string{kFakeRg}, raw);
}

} // namespace

TEST_CASE("grep con rg: argumentos sin shell, sin --follow ni configuración", "[grep][rg]") {
    ToolFixture fixture;
    std::vector<std::string> argv;
    ProcessOptions options;
    fake_rg(fixture, exited(1, rg_summary()), &argv, &options);
    const ToolResult result = fake_grep(
        fixture,
        R"({"pattern": "-x", "ignore_case": true, "output_mode": "content", "context": 2})");
    CHECK(result.display == "Sin coincidencias");
    tool_test::check_helper_environment(options);
    CHECK(options.cancel == &fixture.cancel);
    REQUIRE(argv.size() >= 4);
    CHECK(argv[0] == kFakeRg);
    const auto has = [&argv](std::string_view flag) {
        return std::find(argv.begin(), argv.end(), flag) != argv.end();
    };
    CHECK(has("--json"));
    CHECK(has("--no-config"));
    CHECK(has("--ignore-case"));
    CHECK(has("--context"));
    CHECK_FALSE(has("--follow"));
    CHECK_FALSE(has("-L"));
    CHECK(argv[argv.size() - 3] == "--");
    CHECK(argv[argv.size() - 2] == "-x");
    CHECK(argv.back() == utf8(fixture.root.path()));
}

TEST_CASE("grep con rg: excluye la carpeta de Moss, con el nombre escapado", "[grep][rg]") {
    if (chatbot::current_os() == chatbot::Os::Windows) {
        SKIP("en Windows no se usa rg");
    }
    ToolFixture fixture;
    std::vector<std::string> argv;
    fake_rg(fixture, exited(1, rg_summary()), &argv);
    CHECK(fake_grep(fixture, R"({"pattern": "x"})").display == "Sin coincidencias");
    const auto has = [&argv](const std::string& value) {
        return std::find(argv.begin(), argv.end(), value) != argv.end();
    };
    const std::string inside = utf8(fixture.root.path()).substr(1) + "/datos-moss";
    CHECK(has("!**/" + inside));
    CHECK(has("!**/" + inside + "/**"));

    // Un nombre con caracteres de glob se escapa; uno con controles no se puede: búsqueda propia.
    const project_root_test::Project project;
    const chatbot::code::ProjectRoot root = project_root_test::open_root(
        project.proy, {project.proy / "da[t]os {x}", project.proy / "otra"});
    const chatbot::CancelToken cancel;
    chatbot::code::ToolState state;
    chatbot::code::ToolContext context{root, cancel, state};
    argv.clear();
    context.run_process = [&argv](const std::vector<std::string>& args, const ProcessOptions&) {
        if (!args.empty() && args[0] == kFakeRg) {
            argv = args;
            return exited(1, rg_summary());
        }
        return ProcessResult{};
    };
    const json arguments = {{"pattern", "a"}};
    CHECK(chatbot::code::run_grep(arguments, context, std::string{kFakeRg}, std::nullopt).display ==
          "Sin coincidencias");
    const std::string escaped = utf8(root.path()).substr(1) + "/da\\[t\\]os\\ \\{x\\}";
    CHECK(has("!**/" + escaped));
    CHECK(has("!**/" + escaped + "/**"));
    CHECK(has("!**/" + utf8(root.path()).substr(1) + "/otra/**"));

    const chatbot::code::ProjectRoot control =
        project_root_test::open_root(project.proy, {project.proy / "da\x01tos"});
    chatbot::code::ToolContext control_context{control, cancel, state};
    control_context.run_process = context.run_process;
    argv.clear();
    CHECK(chatbot::code::run_grep(arguments, control_context, std::string{kFakeRg}, std::nullopt)
              .text == "src/a.txt");
    CHECK(argv.empty());
}

TEST_CASE("grep con rg real: ni lee la carpeta de Moss dentro de la raíz", "[grep][rg]") {
    const std::optional<std::string> rg = rg_for(Engine::Rg);
    // Desde otra carpeta y desde la raíz: rg compara sus globs según la carpeta actual.
    const bool from_root = GENERATE(false, true);
    ToolFixture fixture;
    fixture.file("visible.txt", "secreto\n");
    std::string rg_out;
    fixture.context.run_process = [&rg_out, &rg](const std::vector<std::string>& argv,
                                                 const ProcessOptions& options) {
        ProcessResult result = chatbot::process::run_process(argv, options);
        if (!argv.empty() && argv[0] == *rg) {
            rg_out += result.out;
        }
        return result;
    };
    std::optional<ScopedCurrentPath> cwd;
    if (from_root) {
        cwd.emplace(fixture.project.proy);
    }
    CHECK(grep(fixture, rg, R"({"pattern": "secreto"})").text == "visible.txt");
    CHECK(rg_out.find("visible.txt") != std::string::npos);
    CHECK(rg_out.find("credentials.json") == std::string::npos);
}

TEST_CASE("grep con rg: salta rutas fuera de la raíz o bloqueadas y los binarios",
          "[grep][rg]") {
    ToolFixture fixture;
    const fs::path& proy = fixture.project.proy;
    const fs::path outside = fixture.project.outside / "secreto.txt";
    const fs::path blocked = proy / "datos-moss" / "credentials.json";
    fake_rg(fixture,
            exited(0, rg_match(proy / "x.txt", 3, "hola") + rg_end(proy / "x.txt") +
                          rg_match(outside, 1, "secreto") + rg_end(outside) +
                          rg_match(blocked, 1, "secreto") + rg_end(blocked) +
                          rg_match(proy / "bin.dat", 1, "hola") + rg_end(proy / "bin.dat", 4) +
                          rg_match(proy / ".." / "fuera" / "secreto.txt", 1, "secreto") +
                          rg_end(proy / ".." / "fuera" / "secreto.txt") + rg_summary()));
    const ToolResult result = fake_grep(fixture, R"({"pattern": "x", "output_mode": "content"})");
    CHECK(result.text == "x.txt:3:hola");
    CHECK(result.display == "1 coincidencia");
}

TEST_CASE("grep con rg: líneas que no son UTF-8 y líneas de más de 4 KiB", "[grep][rg]") {
    ToolFixture fixture;
    const fs::path file = fixture.project.proy / "x.txt";
    fake_rg(fixture, exited(0, rg_line("match", file, 1, {{"bytes", "aG9sYSD/"}}) + // "hola \xFF"
                                   rg_match(file, 2, std::string(5000, 'h')) + rg_end(file) +
                                   rg_summary()));
    CHECK(fake_grep(fixture, R"({"pattern": "h", "output_mode": "content"})").text ==
          "x.txt:1:hola \xEF\xBF\xBD");
}

TEST_CASE("grep con rg: un error de sintaxis de rg vuelve como error", "[grep][rg]") {
    ToolFixture fixture;
    fake_rg(fixture, exited(2, "", "regex parse error:\n    (\n    ^\nerror: unclosed group\n"));
    const ToolResult result = fake_grep(fixture, R"({"pattern": "("})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text.starts_with("Invalid regular expression: regex parse error"));
    CHECK(result.text.find("unclosed group") != std::string::npos);
    CHECK(result.display == "Patrón inválido");
}

TEST_CASE("grep con rg: si rg falla, cae a la búsqueda propia", "[grep][rg]") {
    ToolFixture fixture;
    fixture.file("x.txt", "hola\n");
    const std::string other = rg_match(fixture.project.proy / "otro.txt", 1, "hola") +
                              rg_end(fixture.project.proy / "otro.txt");

    fake_rg(fixture, exited(2, other, "algo salió mal"));
    CHECK(fake_grep(fixture, R"({"pattern": "hola"})").text == "x.txt");

    ProcessResult timed_out = exited(0, other);
    timed_out.status = ProcessResult::Status::TimedOut;
    fake_rg(fixture, timed_out);
    CHECK(fake_grep(fixture, R"({"pattern": "hola"})").text == "x.txt");

    fake_rg(fixture, ProcessResult{}); // No se pudo lanzar.
    CHECK(fake_grep(fixture, R"({"pattern": "hola"})").text == "x.txt");

    // Con exit 2 pero completa ("summary"): un archivo ilegible no tira la búsqueda.
    fake_rg(fixture, exited(2, other + rg_summary(), "permiso denegado"));
    CHECK(fake_grep(fixture, R"({"pattern": "hola"})").text == "otro.txt");
}

TEST_CASE("grep con rg: salida al tope", "[grep][rg]") {
    ToolFixture fixture;
    fixture.file("x.txt", "hola\n");
    const auto padded = [](std::string out) {
        out += "{\"type\":\"match\",\"data\":{";
        out.resize(chatbot::process::kMaxProcessOutput, 'x');
        return out;
    };

    // Cortada antes de juntar el tope: incompleta, así que la búsqueda propia.
    fake_rg(fixture, exited(0, padded(rg_match(fixture.project.proy / "otro.txt", 1, "hola") +
                                      rg_end(fixture.project.proy / "otro.txt"))));
    CHECK(fake_grep(fixture, R"({"pattern": "hola"})").text == "x.txt");

    // Con más del tope ya juntado, alcanza para responder y avisar.
    std::string many;
    for (int i = 0; i < 201; ++i) {
        const fs::path path = fixture.project.proy / ("f" + std::to_string(1000 + i) + ".txt");
        many += rg_match(path, 1, "hola") + rg_end(path);
    }
    fake_rg(fixture, exited(0, padded(many)));
    const ToolResult result = fake_grep(fixture, R"({"pattern": "hola"})");
    CHECK(split_lines(result.text).size() == 202);
    CHECK(result.display == "más de 200 archivos");
}

TEST_CASE("grep con rg cancelado", "[grep][rg]") {
    ToolFixture fixture;
    fixture.context.run_process = [&fixture](const std::vector<std::string>&,
                                             const ProcessOptions&) {
        fixture.cancel.cancel();
        ProcessResult cancelled;
        cancelled.status = ProcessResult::Status::Cancelled;
        return cancelled;
    };
    CHECK(fake_grep(fixture, R"({"pattern": "a"})").text == "Cancelled by the user.");
}

TEST_CASE("grep: etiqueta, resumen y argumentos por el registro", "[grep]") {
    ToolFixture fixture;
    CHECK(fixture.registry.label("grep") == "Buscar");
    CHECK(fixture.registry.summarize("grep", R"({"pattern": "trim_history", "path": "tests/"})") ==
          "Buscar \"trim_history\" en tests/");
    CHECK(fixture.registry.summarize("grep", R"({"pattern": "x"})") == "Buscar \"x\"");
    CHECK(fixture.run("grep", R"({"pattern": "x", "output_mode": "lines"})").status ==
          ToolStatus::InvalidArguments);
    CHECK(fixture.run("grep", R"({"pattern": "x", "context": -1})").status ==
          ToolStatus::InvalidArguments);
    fixture.file("x.txt", "x\n");
    CHECK(fixture.run("grep", R"({"pattern": "x"})").text == "x.txt");
}
