// Raíz del proyecto y seguridad de rutas (project_root.h), en carpetas temporales. Los enlaces
// simbólicos pueden no crearse en Windows (privilegios, o std::filesystem de MinGW sin soporte):
// esas pruebas se saltan con SKIP en vez de fallar.

#include "chatbot/code/project_root.h"

#include "chatbot/platform.h"
#include "chatbot/utf8.h"

#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using chatbot::Os;
using chatbot::code::check_path_text;
using chatbot::code::PathError;
using chatbot::code::PathErrorKind;
using chatbot::code::ProjectRoot;
using chatbot::code::ResolvedPath;

namespace {

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path from_utf8(std::string_view text) {
    return fs::path{std::u8string{text.begin(), text.end()}};
}

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
    REQUIRE(file.good());
}

/// Crea un enlace simbólico o salta la prueba (Windows sin privilegios o sin soporte).
void symlink_or_skip(const fs::path& target, const fs::path& link) {
    std::error_code error;
    if (fs::is_directory(target, error)) {
        fs::create_directory_symlink(target, link, error);
    } else {
        fs::create_symlink(target, link, error);
    }
    if (error) {
        SKIP("no se pudo crear un enlace simbólico aquí: " << error.message());
    }
}

ProjectRoot open_root(const fs::path& root, const std::vector<fs::path>& blocked = {}) {
    std::vector<std::string> texts;
    for (const fs::path& dir : blocked) {
        texts.push_back(utf8(dir));
    }
    const chatbot::Result<ProjectRoot> result = ProjectRoot::open(utf8(root), texts);
    if (result.is_error()) {
        UNSCOPED_INFO(result.error().message);
    }
    REQUIRE(result.is_ok());
    return result.value();
}

fs::path resolved(const ResolvedPath& result) {
    if (const auto* error = std::get_if<PathError>(&result)) {
        UNSCOPED_INFO(error->message);
    }
    REQUIRE(std::holds_alternative<fs::path>(result));
    return std::get<fs::path>(result);
}

fs::path resolved(const ProjectRoot& root, std::string_view request) {
    return resolved(root.resolve(request));
}

PathError failure(const ResolvedPath& result) {
    REQUIRE(std::holds_alternative<PathError>(result));
    return std::get<PathError>(result);
}

PathErrorKind failure_kind(const ProjectRoot& root, std::string_view request) {
    return failure(root.resolve(request)).kind;
}

/// temp/proy (con src/a.txt) es la raíz; temp/fuera (con secreto.txt) queda fuera, y
/// temp/config-moss (con credentials.json) hace de carpeta de Moss.
struct Project {
    chatbot_test::ScopedTempDir temp;
    fs::path proy = temp.path() / "proy";
    fs::path outside = temp.path() / "fuera";
    fs::path moss = temp.path() / "config-moss";

    Project() {
        fs::create_directories(proy / "src");
        fs::create_directories(outside);
        fs::create_directories(moss);
        write_file(proy / "src" / "a.txt", "a");
        write_file(outside / "secreto.txt", "secreto");
        write_file(moss / "credentials.json", "{\"clave\": \"no-debe-salir\"}");
    }
};

} // namespace

TEST_CASE("ProjectRoot: abre la raíz canónica y resuelve una ruta relativa", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(fs::equivalent(root.path(), project.proy));
    CHECK(resolved(root, "src/a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, ".") == root.path());
    CHECK(resolved(root, "./src/") == root.path() / "src");
}

TEST_CASE("ProjectRoot: ../ que sale se rechaza; a/../b que no sale se acepta",
          "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    const PathError error = failure(root.resolve("../fuera/secreto.txt"));
    CHECK(error.kind == PathErrorKind::Outside);
    CHECK(error.message == "Path is outside the project: ../fuera/secreto.txt");
    CHECK(failure_kind(root, "..") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "src/../../proy2") == PathErrorKind::Outside);
    CHECK(resolved(root, "src/../src/a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "nuevo/../src") == root.path() / "src");
}

TEST_CASE("ProjectRoot: rutas absolutas dentro y fuera", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(resolved(root, utf8(root.path() / "src" / "a.txt")) == root.path() / "src" / "a.txt");
    CHECK(failure_kind(root, utf8(project.outside / "secreto.txt")) == PathErrorKind::Outside);
    CHECK(failure_kind(root, utf8(project.temp.path())) == PathErrorKind::Outside);
}

TEST_CASE("ProjectRoot: una hermana con el mismo prefijo no está dentro", "[project_root]") {
    const Project project;
    fs::create_directories(project.temp.path() / "proy2");
    write_file(project.temp.path() / "proy2" / "x.txt", "x");
    const ProjectRoot root = open_root(project.proy);
    CHECK(failure_kind(root, utf8(project.temp.path() / "proy2" / "x.txt")) ==
          PathErrorKind::Outside);
    CHECK(failure_kind(root, "../proy2/x.txt") == PathErrorKind::Outside);
}

TEST_CASE("ProjectRoot: enlaces simbólicos hacia fuera, hacia Moss y dentro", "[project_root]") {
    const Project project;
    symlink_or_skip(project.outside / "secreto.txt", project.proy / "fuera-archivo");
    symlink_or_skip(project.outside, project.proy / "fuera-carpeta");
    symlink_or_skip(project.moss, project.proy / "claves");
    symlink_or_skip(project.moss / "credentials.json", project.proy / "credenciales");
    symlink_or_skip(project.proy / "src", project.proy / "atajo");
    const ProjectRoot root = open_root(project.proy, {project.moss});

    CHECK(failure_kind(root, "fuera-archivo") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "fuera-carpeta") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "fuera-carpeta/secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "fuera-carpeta/nuevo.txt") == PathErrorKind::Outside);

    const PathError blocked = failure(root.resolve("claves/credentials.json"));
    CHECK(blocked.kind == PathErrorKind::Blocked);
    CHECK(blocked.message.find("no-debe-salir") == std::string::npos);
    CHECK(failure_kind(root, "claves") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, "credenciales") == PathErrorKind::Blocked);

    CHECK(resolved(root, "atajo/a.txt") == root.path() / "src" / "a.txt");
}

TEST_CASE("ProjectRoot: enlace en un tramo intermedio", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / "hondo");
    symlink_or_skip(project.outside, project.proy / "hondo" / "enlace");
    symlink_or_skip(project.proy / "src", project.proy / "hondo" / "dentro");
    const ProjectRoot root = open_root(project.proy);
    CHECK(failure_kind(root, "hondo/enlace/secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "hondo/enlace/nuevo/archivo.txt") == PathErrorKind::Outside);
    // ".." después de un enlace sube desde su destino, como lo haría el sistema (quitarlo como
    // texto daría hondo/fuera/secreto.txt, dentro).
    CHECK(failure_kind(root, "hondo/enlace/../fuera/secreto.txt") == PathErrorKind::Outside);
    CHECK(resolved(root, "hondo/enlace/../proy/src/a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "hondo/dentro/nuevo/b.txt") == root.path() / "src" / "nuevo" / "b.txt");
}

TEST_CASE("ProjectRoot: ciclo de enlaces y enlace roto", "[project_root]") {
    const Project project;
    symlink_or_skip(project.proy / "ciclo-b", project.proy / "ciclo-a");
    symlink_or_skip(project.proy / "ciclo-a", project.proy / "ciclo-b");
    symlink_or_skip(project.outside / "no-existe.txt", project.proy / "roto");
    const ProjectRoot root = open_root(project.proy);

    const PathError loop = failure(root.resolve("ciclo-a/x.txt"));
    CHECK(loop.kind == PathErrorKind::Unresolvable);
    CHECK(loop.message == "Cannot resolve path (symbolic link loop): ciclo-a/x.txt");
    CHECK(failure_kind(root, "ciclo-a") == PathErrorKind::Unresolvable);

    // Abrir "roto" para escribir crearía fuera/no-existe.txt.
    const PathError broken = failure(root.resolve("roto"));
    CHECK(broken.kind == PathErrorKind::Unresolvable);
    CHECK(broken.message == "Cannot resolve path (broken symbolic link): roto");
}

TEST_CASE("ProjectRoot: rutas que no existen", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(resolved(root, "nuevo/dir/archivo.txt") ==
          root.path() / "nuevo" / "dir" / "archivo.txt");
    CHECK(failure_kind(root, "nuevo/../../fuera/x.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "nuevo/../../proy/../fuera") == PathErrorKind::Outside);
}

TEST_CASE("ProjectRoot: la raíz misma es un enlace", "[project_root]") {
    const Project project;
    const fs::path link = project.temp.path() / "enlace-proy";
    symlink_or_skip(project.proy, link);
    const ProjectRoot direct = open_root(project.proy);
    const ProjectRoot through_link = open_root(link);
    CHECK(through_link.path() == direct.path());
    CHECK(resolved(through_link, utf8(link / "src" / "a.txt")) ==
          direct.path() / "src" / "a.txt");
}

TEST_CASE("ProjectRoot: carpeta bloqueada dentro de la raíz", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / "datos");
    // .moss todavía no existe: se bloquea igual.
    const ProjectRoot root =
        open_root(project.proy, {project.proy / ".moss", project.proy / "datos", project.moss});
    CHECK(failure_kind(root, ".moss/config.json") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, ".moss") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, "src/../.moss/x") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, "datos/conversacion.json") == PathErrorKind::Blocked);
    CHECK(resolved(root, ".moss2/x") == root.path() / ".moss2" / "x");
    CHECK(resolved(root, "src/a.txt") == root.path() / "src" / "a.txt");
    // Fuera de la raíz, lo bloqueado gana: con permisos (C3) nunca se preguntará por ella.
    const PathError error = failure(root.resolve(utf8(project.moss / "credentials.json")));
    CHECK(error.kind == PathErrorKind::Blocked);
    CHECK(error.message.find("no-debe-salir") == std::string::npos);
}

TEST_CASE("ProjectRoot: no abre una raíz que es o está dentro de la carpeta de Moss",
          "[project_root]") {
    const Project project;
    std::vector<std::string> blocked{utf8(project.proy)};
    for (const fs::path& root : {project.proy, project.proy / "src"}) {
        const chatbot::Result<ProjectRoot> result = ProjectRoot::open(utf8(root), blocked);
        REQUIRE(result.is_error());
        CHECK(result.error().kind == chatbot::ErrorKind::Config);
        CHECK(result.error().message.find("carpeta de Moss") != std::string::npos);
    }
}

TEST_CASE("ProjectRoot: abrir falla si la raíz no existe, no es carpeta o no es texto válido",
          "[project_root]") {
    const Project project;
    const std::vector<std::string> none;
    const std::vector<std::string> roots{utf8(project.temp.path() / "no-existe"),
                                         utf8(project.proy / "src" / "a.txt"), "",
                                         std::string{"a\0b", 3}, "\xFF"};
    for (const std::string& root : roots) {
        const chatbot::Result<ProjectRoot> result = ProjectRoot::open(root, none);
        REQUIRE(result.is_error());
        CHECK(result.error().kind == chatbot::ErrorKind::Config);
    }
    CHECK(ProjectRoot::open(utf8(project.proy), {""}).is_error());
}

TEST_CASE("ProjectRoot: nombres con espacios y acentos", "[project_root]") {
    const Project project;
    const fs::path folder = project.proy / from_utf8("carpeta con espacios");
    fs::create_directories(folder);
    write_file(folder / from_utf8("ñandú.txt"), "x");
    const ProjectRoot root = open_root(project.proy);
    const fs::path file = resolved(root, "carpeta con espacios/ñandú.txt");
    CHECK(file == root.path() / from_utf8("carpeta con espacios") / from_utf8("ñandú.txt"));
    CHECK(root.display(file) == "carpeta con espacios/ñandú.txt");
    CHECK(resolved(root, "carpeta con espacios/canción nueva.md") ==
          root.path() / from_utf8("carpeta con espacios") / from_utf8("canción nueva.md"));
}

TEST_CASE("ProjectRoot: vacía, con NUL o con UTF-8 inválido", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    for (const std::string& request : {std::string{}, std::string{"src/a\0.txt", 10},
                                       std::string{"src/\xFF\xFE.txt"}, std::string{"\xC3"}}) {
        const PathError error = failure(root.resolve(request));
        CHECK(error.kind == PathErrorKind::Invalid);
        // El mensaje va en JSON al modelo: nunca repite bytes inválidos.
        CHECK(chatbot::utf8::is_valid(error.message));
        CHECK(error.message.find('\0') == std::string::npos);
    }
}

TEST_CASE("ProjectRoot: resolve_entry para los recorridos de glob y grep", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / ".moss");
    write_file(project.proy / ".moss" / "config.json", "{}");
    symlink_or_skip(project.outside, project.proy / "fuera-carpeta");
    symlink_or_skip(project.proy / "src", project.proy / "atajo");
    const ProjectRoot root = open_root(project.proy, {project.proy / ".moss"});

    std::vector<std::string> allowed;
    std::vector<std::string> skipped;
    for (const fs::directory_entry& entry : fs::directory_iterator{root.path()}) {
        const ResolvedPath result = root.resolve_entry(entry.path());
        const std::string name = utf8(entry.path().filename());
        if (std::holds_alternative<fs::path>(result)) {
            allowed.push_back(name);
        } else {
            skipped.push_back(name);
        }
    }
    std::sort(allowed.begin(), allowed.end());
    std::sort(skipped.begin(), skipped.end());
    CHECK(allowed == std::vector<std::string>{"atajo", "src"});
    CHECK(skipped == std::vector<std::string>{".moss", "fuera-carpeta"});

    CHECK(failure(root.resolve_entry(root.path() / ".moss" / "config.json")).kind ==
          PathErrorKind::Blocked);
    CHECK(failure(root.resolve_entry(root.path() / "fuera-carpeta")).kind ==
          PathErrorKind::Outside);
    CHECK(resolved(root.resolve_entry(root.path() / "atajo")) == root.path() / "src");
    // Una ruta ya canónica vuelve igual.
    CHECK(resolved(root.resolve_entry(root.path() / "src" / "a.txt")) ==
          root.path() / "src" / "a.txt");
}

TEST_CASE("ProjectRoot: display relativa a la raíz con /", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(root.display(root.path()) == ".");
    CHECK(root.display(root.path() / "src" / "a.txt") == "src/a.txt");
    CHECK(root.display(root.path() / "src") == "src");
    CHECK(root.display(project.outside) == utf8(project.outside));
}

TEST_CASE("check_path_text: reglas de Windows (pura, en todos los sistemas)", "[project_root]") {
    const std::vector<std::string> invalid{
        "C:x",          "C:",           "c:archivo.txt", "NUL",           "nul.txt",
        "src/COM1",     "Lpt9.log",     "con .txt",      "COM\xC2\xB9",   "CONIN$",
        "aux",          "src\\prn.c",   "archivo.txt:flujo", "a/b:c/d",   "src/a.txt::$DATA",
        "\\\\?\\C:\\x", "//?/C:/x",     "\\\\.\\pipe\\x", "\\\\.\\C:\\x", "\\??\\C:\\x",
        "src/x.",       "src/x ",       "a<b",           "a|b",           "a*",
        "a?",           "a\"b",         "a\tb"};
    for (const std::string& path : invalid) {
        INFO(path);
        const std::optional<PathError> error = check_path_text(path, Os::Windows);
        REQUIRE(error.has_value());
        CHECK(error->kind == PathErrorKind::Invalid);
        CHECK(error->message.find(path) != std::string::npos);
        // En POSIX son nombres normales (salvo los que no son UTF-8).
        CHECK_FALSE(check_path_text(path, Os::Posix).has_value());
    }
    const std::vector<std::string> valid{
        "C:\\x",        "C:/x",        "c:\\",          "src\\a.txt",     "src/a.txt",
        "\\\\servidor\\recurso\\x",    "\\x",           "..\\x",          "./a",
        "nul-no.txt",   "console.txt", "COM10",         "LPT",            "comida",
        "ñandú.txt",    "a b/c d.txt", ".gitignore",    "src/",           "."};
    for (const std::string& path : valid) {
        INFO(path);
        CHECK_FALSE(check_path_text(path, Os::Windows).has_value());
        CHECK_FALSE(check_path_text(path, Os::Posix).has_value());
    }
    for (const Os os : {Os::Posix, Os::Windows}) {
        CHECK(check_path_text("", os).has_value());
        CHECK(check_path_text(std::string{"a\0b", 3}, os).has_value());
        CHECK(check_path_text("\xFF", os).has_value());
    }
}

TEST_CASE("ProjectRoot: en Windows, unidades, \\ y mayúsculas", "[project_root]") {
    if (chatbot::current_os() != Os::Windows) {
        SKIP("solo Windows");
    }
    const Project project;
    const ProjectRoot root = open_root(project.proy, {project.proy / ".moss"});
    const std::string drive = utf8(root.path().root_name()); // "C:"
    REQUIRE(drive.size() == 2);

    CHECK(resolved(root, "src\\a.txt") == root.path() / "src" / "a.txt");
    // GetFinalPathNameByHandleW da las mayúsculas que tiene en el disco.
    CHECK(resolved(root, "SRC\\A.TXT") == root.path() / "src" / "a.txt");
    std::string forward = utf8(root.path() / "src" / "a.txt");
    std::replace(forward.begin(), forward.end(), '\\', '/');
    CHECK(resolved(root, forward) == root.path() / "src" / "a.txt");

    CHECK(failure_kind(root, drive + "\\") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "\\") == PathErrorKind::Outside);
    const std::string other_drive = drive[0] == 'Z' || drive[0] == 'z' ? "Y:\\x" : "Z:\\x";
    CHECK(failure_kind(root, other_drive) == PathErrorKind::Outside);
    // Otro servidor queda fuera sin conectar con él.
    CHECK(failure_kind(root, "\\\\servidor-que-no-existe\\recurso\\x") == PathErrorKind::Outside);

    CHECK(failure_kind(root, drive + "x") == PathErrorKind::Invalid);
    CHECK(failure_kind(root, "src\\NUL") == PathErrorKind::Invalid);
    CHECK(failure_kind(root, "src\\a.txt:flujo") == PathErrorKind::Invalid);

    // La carpeta bloqueada no existe todavía: se compara sin distinguir mayúsculas.
    CHECK(failure_kind(root, ".MOSS\\config.json") == PathErrorKind::Blocked);
    CHECK(root.display(root.path() / "src" / "a.txt") == "src/a.txt");
}
