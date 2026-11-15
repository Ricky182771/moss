// Raíz del proyecto y seguridad de rutas (project_root.h), en carpetas temporales. Las pruebas
// con enlaces simbólicos son de POSIX: en Windows no se siguen (y crearlos pide privilegios); sus
// casos, con uniones, están en test_project_root_windows.cpp.

#include "project_root_fixture.hpp"

#include "chatbot/cancel_token.h"
#include "chatbot/platform.h"
#include "chatbot/utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace project_root_test;
using chatbot::CancelToken;
using chatbot::Os;
using chatbot::code::check_path_text;

namespace {

/// Crea un enlace simbólico (target tal cual: puede ser relativo). En Windows la prueba se salta:
/// allí no se siguen.
void symlink_or_skip(const fs::path& target, const fs::path& link) {
    if (chatbot::current_os() == Os::Windows) {
        SKIP("en Windows los enlaces simbólicos no se siguen (test_project_root_windows.cpp)");
    }
    std::error_code error;
    fs::create_symlink(target, link, error);
    if (error) {
        SKIP("no se pudo crear un enlace simbólico aquí: " << error.message());
    }
}

bool has_control(std::string_view text) {
    return std::any_of(text.begin(), text.end(),
                       [](char c) { return static_cast<unsigned char>(c) < 0x20U; });
}

} // namespace

TEST_CASE("ProjectRoot: abre la raíz canónica y resuelve una ruta relativa", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(fs::equivalent(root.path(), project.proy));
    CHECK(resolved(root, "src/a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "src//a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "src/./a.txt") == root.path() / "src" / "a.txt");
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

TEST_CASE("ProjectRoot: un enlace detrás de un tramo que no existe", "[project_root]") {
    const Project project;
    symlink_or_skip(project.outside, project.proy / "fuera-carpeta");
    symlink_or_skip(project.moss, project.proy / "claves");
    symlink_or_skip(project.proy / "src", project.proy / "atajo");
    const ProjectRoot root = open_root(project.proy, {project.moss});
    // Quitar "nuevo/.." deja el enlace a la vista: se vuelve a canonizar, no es un enlace roto.
    CHECK(resolved(root, "nuevo/../atajo/a.txt") == root.path() / "src" / "a.txt");
    CHECK(failure_kind(root, "nuevo/../fuera-carpeta/secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "nuevo/../claves/credentials.json") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, "a/b/../../claves/credentials.json") == PathErrorKind::Blocked);
}

TEST_CASE("ProjectRoot: enlaces con destino relativo y encadenados", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / "hondo");
    symlink_or_skip("../../fuera", project.proy / "hondo" / "rel");
    symlink_or_skip("c2", project.proy / "c1");
    symlink_or_skip("src", project.proy / "c2");
    symlink_or_skip("d2", project.proy / "d1");
    symlink_or_skip("../fuera", project.proy / "d2");
    const ProjectRoot root = open_root(project.proy);
    CHECK(failure_kind(root, "hondo/rel/secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "hondo/rel") == PathErrorKind::Outside);
    CHECK(resolved(root, "c1/a.txt") == root.path() / "src" / "a.txt");
    CHECK(failure_kind(root, "d1/secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "d1/nuevo.txt") == PathErrorKind::Outside);
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
    // texto daría hondo/fuera/secreto.txt, dentro). En Windows el ".." es léxico, como en Win32.
    CHECK(failure_kind(root, "hondo/enlace/../fuera/secreto.txt") == PathErrorKind::Outside);
    CHECK(resolved(root, "hondo/enlace/../proy/src/a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "hondo/dentro/nuevo/b.txt") == root.path() / "src" / "nuevo" / "b.txt");
}

TEST_CASE("ProjectRoot: ciclo de enlaces y enlaces rotos", "[project_root]") {
    const Project project;
    symlink_or_skip(project.proy / "ciclo-b", project.proy / "ciclo-a");
    symlink_or_skip(project.proy / "ciclo-a", project.proy / "ciclo-b");
    symlink_or_skip(project.outside / "no-existe.txt", project.proy / "roto");
    symlink_or_skip("../fuera/no-existe-dir", project.proy / "roto-dir");
    const ProjectRoot root = open_root(project.proy);

    const PathError loop = failure(root.resolve("ciclo-a/x.txt"));
    CHECK(loop.kind == PathErrorKind::Unresolvable);
    CHECK(loop.message == "Cannot resolve path (symbolic link loop): ciclo-a/x.txt");
    CHECK(failure_kind(root, "ciclo-a") == PathErrorKind::Unresolvable);

    // Abrir "roto" para escribir crearía fuera/no-existe.txt.
    const PathError broken = failure(root.resolve("roto"));
    CHECK(broken.kind == PathErrorKind::Unresolvable);
    CHECK(broken.message == "Cannot resolve path (broken symbolic link): roto");
    // Roto en un tramo intermedio: crear roto-dir/x.txt crearía fuera de la raíz.
    CHECK(failure(root.resolve("roto-dir/x.txt")).message ==
          "Cannot resolve path (broken symbolic link): roto-dir/x.txt");
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
    CHECK(direct.is_same_directory(utf8(link)));
}

TEST_CASE("ProjectRoot: is_same_directory compara por identidad", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(root.is_same_directory(utf8(project.proy)));
    CHECK(root.is_same_directory(utf8(project.proy / "src" / "..")));
    CHECK_FALSE(root.is_same_directory(utf8(project.temp.path())));
    CHECK_FALSE(root.is_same_directory(utf8(project.proy / "no-existe")));
    CHECK_FALSE(root.is_same_directory(""));
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

TEST_CASE("ProjectRoot: carpeta bloqueada creada después de abrir", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy, {project.proy / "tarde"});
    CHECK(resolved(root, "src/a.txt") == root.path() / "src" / "a.txt");
    fs::create_directories(project.proy / "tarde");
    write_file(project.proy / "tarde" / "x.json", "{}");
    CHECK(failure_kind(root, "tarde/x.json") == PathErrorKind::Blocked);
}

TEST_CASE("ProjectRoot: bloqueada que aparece después por un enlace en su ruta",
          "[project_root]") {
    const Project project;
    // temp/x/datos no existe al abrir; luego temp/x es un enlace a temp/real.
    const ProjectRoot root = open_root(project.proy, {project.temp.path() / "x" / "datos"});
    fs::create_directories(project.temp.path() / "real" / "datos");
    write_file(project.temp.path() / "real" / "datos" / "secreto.json", "{}");
    symlink_or_skip(project.temp.path() / "real", project.temp.path() / "x");
    symlink_or_skip(project.temp.path() / "real" / "datos", project.proy / "acceso");
    CHECK(failure_kind(root, "acceso/secreto.json") == PathErrorKind::Blocked);
    CHECK(failure_kind(root, utf8(project.temp.path() / "real" / "datos" / "secreto.json")) ==
          PathErrorKind::Blocked);
}

TEST_CASE("ProjectRoot: blocked_within da las bloqueadas que hoy están dentro, relativas",
          "[project_root]") {
    const Project project;
    // Dentro (una aún no existe), fuera, y una que entra después por un enlace en su ruta.
    const ProjectRoot root =
        open_root(project.proy, {project.proy / "datos" / "moss", project.proy / ".moss",
                                 project.moss, project.temp.path() / "x" / "datos"});
    CHECK(root.blocked_within() == std::vector<std::string>{"datos/moss", ".moss"});
    symlink_or_skip(project.proy, project.temp.path() / "x");
    CHECK(root.blocked_within() == std::vector<std::string>{"datos/moss", ".moss", "datos"});
}

TEST_CASE("ProjectRoot: carpeta bloqueada que es un enlace o cuyo padre lo es",
          "[project_root]") {
    const Project project;
    fs::create_directories(project.temp.path() / "padre-real" / "moss");
    write_file(project.temp.path() / "padre-real" / "moss" / "x.json", "{}");
    symlink_or_skip(project.moss, project.temp.path() / "moss-enlace");
    symlink_or_skip(project.temp.path() / "padre-real", project.temp.path() / "padre-enlace");
    symlink_or_skip(project.temp.path() / "padre-real" / "moss", project.proy / "acceso");
    const ProjectRoot root = open_root(project.proy, {project.temp.path() / "moss-enlace",
                                                      project.temp.path() / "padre-enlace" /
                                                          "moss"});
    CHECK(failure_kind(root, utf8(project.moss / "credentials.json")) == PathErrorKind::Blocked);
    CHECK(failure_kind(root, utf8(project.temp.path() / "padre-real" / "moss" / "x.json")) ==
          PathErrorKind::Blocked);
    CHECK(failure_kind(root, "acceso/x.json") == PathErrorKind::Blocked);
}

TEST_CASE("ProjectRoot: la carpeta de Moss se reconoce por identidad aunque cambie de nombre",
          "[project_root]") {
    // Sin privilegios no se puede montar un sistema sin mayúsculas ni un bind: renombrarla después
    // de abrir deja la misma carpeta (mismo inodo) bajo una ruta que no se parece en nada.
    const Project project;
    const ProjectRoot root = open_root(project.proy, {project.moss});
    const fs::path renamed = project.temp.path() / "otro-nombre";
    fs::rename(project.moss, renamed);
    const PathError error = failure(root.resolve(utf8(renamed / "credentials.json")));
    CHECK(error.kind == PathErrorKind::Blocked);
    const ReadResult read = root.read_file(renamed / "credentials.json", 1024);
    REQUIRE(std::holds_alternative<PathError>(read));
    CHECK(std::get<PathError>(read).kind == PathErrorKind::Blocked);
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

TEST_CASE("ProjectRoot: no abre la raíz del sistema de archivos", "[project_root]") {
    if (chatbot::current_os() == Os::Windows) {
        SKIP("en Windows, la raíz de la unidad: test_project_root_windows.cpp");
    }
    const chatbot::Result<ProjectRoot> result = ProjectRoot::open("/", {});
    REQUIRE(result.is_error());
    CHECK(result.error().kind == chatbot::ErrorKind::Config);
    CHECK(result.error().message.find("raíz") != std::string::npos);
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
        // El mensaje va en JSON al modelo: nunca repite bytes inválidos ni controles.
        CHECK(chatbot::utf8::is_valid(error.message));
        CHECK_FALSE(has_control(error.message));
    }
}

TEST_CASE("ProjectRoot: rutas larguísimas, con muchos tramos o con controles",
          "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);

    const PathError too_long = failure(root.resolve(std::string(5000, 'a')));
    CHECK(too_long.kind == PathErrorKind::Invalid);
    CHECK(too_long.message.size() < 400); // La ruta va recortada.
    CHECK(too_long.message.ends_with("\xE2\x80\xA6"));

    std::string deep;
    for (int i = 0; i < 257; ++i) {
        deep += "a/";
    }
    CHECK(failure_kind(root, deep) == PathErrorKind::Invalid);
    const std::string deep_ok = deep.substr(2, deep.size() - 3); // 256 tramos, sin "/" final.
    CHECK(resolved(root, deep_ok) == root.path() / from_utf8(deep_ok));

    std::string far = "..";
    for (int i = 0; i < 30; ++i) {
        far += "/xxxxxxxxx"; // Tramos cortos: uno de más de 255 bytes no se podría ni mirar.
    }
    const PathError outside = failure(root.resolve(far));
    CHECK(outside.kind == PathErrorKind::Outside);
    CHECK(outside.message.size() < 300);

    // Un salto de línea en el nombre: escapado en el mensaje (en Windows ni se acepta).
    const PathError control = failure(root.resolve("nuevo\n/../../fuera\x1B[31m"));
    CHECK_FALSE(has_control(control.message));
    CHECK(control.message.find("\\x0A") != std::string::npos);
    CHECK(control.message.find("\\x1B") != std::string::npos);
}

TEST_CASE("ProjectRoot: display escapa los controles de un nombre", "[project_root]") {
    if (chatbot::current_os() == Os::Windows) {
        SKIP("Windows no deja controles en los nombres");
    }
    const Project project;
    const fs::path odd = project.proy / ("con\x01" "control\x7F.txt");
    write_file(odd, "x");
    const ProjectRoot root = open_root(project.proy);
    CHECK(root.display(resolved(root.resolve_entry(odd))) == "con\\x01control\\x7F.txt");
    CHECK(root.display(root.path() / "inv\xFFlido") == "inv\xEF\xBF\xBDlido");
}

TEST_CASE("ProjectRoot: resolve_entry para los recorridos de glob y grep", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / ".moss");
    write_file(project.proy / ".moss" / "config.json", "{}");
    symlink_or_skip(project.outside, project.proy / "fuera-carpeta");
    symlink_or_skip(project.proy / "src", project.proy / "atajo");
    symlink_or_skip(project.moss / "credentials.json", project.proy / "credenciales");
    symlink_or_skip(".", project.proy / "bucle");
    const ProjectRoot root = open_root(project.proy, {project.proy / ".moss", project.moss});

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
    CHECK(allowed == std::vector<std::string>{"atajo", "bucle", "src"});
    CHECK(skipped == std::vector<std::string>{".moss", "credenciales", "fuera-carpeta"});

    CHECK(failure(root.resolve_entry(root.path() / ".moss" / "config.json")).kind ==
          PathErrorKind::Blocked);
    CHECK(failure(root.resolve_entry(root.path() / "credenciales")).kind ==
          PathErrorKind::Blocked);
    CHECK(failure(root.resolve_entry(root.path() / "fuera-carpeta")).kind ==
          PathErrorKind::Outside);
    CHECK(resolved(root.resolve_entry(root.path() / "atajo")) == root.path() / "src");
    CHECK(resolved(root.resolve_entry(root.path() / "bucle")) == root.path());
    // Una ruta ya canónica vuelve igual.
    CHECK(resolved(root.resolve_entry(root.path() / "src" / "a.txt")) ==
          root.path() / "src" / "a.txt");
}

TEST_CASE("ProjectRoot: walk en orden, saltando carpetas y lo bloqueado", "[project_root]") {
    const Project project;
    for (const char* dir : {"src/sub", ".git", "node_modules/x", "build-dev", ".moss", "vacía"}) {
        fs::create_directories(project.proy / from_utf8(dir));
    }
    write_file(project.proy / "z.txt", "z");
    write_file(project.proy / "b.txt", "b");
    write_file(project.proy / "src" / "sub" / "c.txt", "c");
    write_file(project.proy / ".git" / "config", "x");
    write_file(project.proy / "node_modules" / "x" / "i.js", "x");
    write_file(project.proy / "build-dev" / "o.o", "x");
    write_file(project.proy / ".moss" / "credentials.json", "{}");
    const ProjectRoot root = open_root(project.proy, {project.proy / ".moss"});

    CHECK(walk_all(root, root.path()) ==
          std::vector<std::string>{"b.txt", "z.txt", "src/a.txt", "src/sub/c.txt"});
    CHECK(walk_all(root, root.path() / "src") ==
          std::vector<std::string>{"src/a.txt", "src/sub/c.txt"});

    // El visitante o la cancelación lo detienen.
    const CancelToken cancel;
    int seen = 0;
    const auto skip_none = std::function<bool(std::string_view)>{};
    REQUIRE_FALSE(root.walk(root.path(), skip_none,
                            [&](const WalkFile&) { return ++seen < 2; }, cancel)
                      .has_value());
    CHECK(seen == 2);
    CancelToken cancelled;
    cancelled.cancel();
    seen = 0;
    REQUIRE_FALSE(root.walk(root.path(), skip_none,
                            [&](const WalkFile&) { return ++seen > 0; }, cancelled)
                      .has_value());
    CHECK(seen == 0);

    // start tiene que ser una carpeta que se pueda usar.
    const auto walk_error = [&](const fs::path& start) {
        const std::optional<PathError> error =
            root.walk(start, skip_none, [](const WalkFile&) { return true; }, cancel);
        REQUIRE(error.has_value());
        return error->kind;
    };
    CHECK(walk_error(root.path() / "z.txt") == PathErrorKind::WrongType);
    CHECK(walk_error(root.path() / ".moss") == PathErrorKind::Blocked);
    CHECK(walk_error(project.outside) == PathErrorKind::Outside);
}

TEST_CASE("ProjectRoot: walk no sigue enlaces (bucle -> ., archivo hacia credentials.json)",
          "[project_root]") {
    const Project project;
    write_file(project.proy / "z.txt", "z");
    symlink_or_skip(".", project.proy / "bucle");
    symlink_or_skip(project.moss / "credentials.json", project.proy / "credenciales");
    symlink_or_skip(project.outside, project.proy / "fuera-carpeta");
    symlink_or_skip(project.proy / "src", project.proy / "atajo");
    symlink_or_skip(project.proy / "z.txt", project.proy / "z-enlace.txt");
    const ProjectRoot root = open_root(project.proy, {project.moss});
    CHECK(walk_all(root, root.path()) == std::vector<std::string>{"z.txt", "src/a.txt"});
}

TEST_CASE("ProjectRoot: read_file lee con tope, tamaño y fecha", "[project_root]") {
    const Project project;
    write_file(project.proy / "cien.txt", std::string(100, 'x'));
    const ProjectRoot root = open_root(project.proy);
    const fs::path file = resolved(root, "cien.txt");

    const FileContent all = read_ok(root, file, 1000);
    CHECK(all.bytes == std::string(100, 'x'));
    CHECK_FALSE(all.truncated);
    CHECK(all.size == 100);
    CHECK(all.modified != 0);
    CHECK_FALSE(read_ok(root, file, 100).truncated);

    const FileContent cut = read_ok(root, file, 10);
    CHECK(cut.bytes == std::string(10, 'x'));
    CHECK(cut.truncated);
    CHECK(cut.size == 100);
    CHECK(cut.modified == all.modified);
    CHECK(read_ok(root, file, 0).bytes.empty());

    CHECK(read_failure(root, root.path() / "src").kind == PathErrorKind::WrongType);
    CHECK(read_failure(root, root.path() / "no-existe.txt").kind == PathErrorKind::ReadFailed);
}

TEST_CASE("ProjectRoot: read_file rechaza lo bloqueado y lo de fuera", "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / ".moss");
    write_file(project.proy / ".moss" / "credentials.json", "{\"clave\": \"no-debe-salir\"}");
    const ProjectRoot root = open_root(project.proy, {project.proy / ".moss"});
    CHECK(read_failure(root, root.path() / ".moss" / "credentials.json").kind ==
          PathErrorKind::Blocked);
    CHECK(read_failure(root, project.outside / "secreto.txt").kind == PathErrorKind::Outside);
}

TEST_CASE("ProjectRoot: read_file con un enlace cambiado entre resolver y abrir",
          "[project_root]") {
    const Project project;
    fs::create_directories(project.proy / "dir-cambia");
    fs::create_directories(project.proy / "dir-moss");
    write_file(project.proy / "cambia.txt", "dentro");
    write_file(project.proy / "dir-cambia" / "secreto.txt", "dentro");
    write_file(project.proy / "dir-moss" / "credentials.json", "dentro");
    const ProjectRoot root = open_root(project.proy, {project.moss});
    const fs::path file = resolved(root, "cambia.txt");
    const fs::path through_dir = resolved(root, "dir-cambia/secreto.txt");
    const fs::path into_moss = resolved(root, "dir-moss/credentials.json");

    // Después de resolver, alguien cambia el archivo y las carpetas por enlaces.
    fs::remove(project.proy / "cambia.txt");
    symlink_or_skip(project.outside / "secreto.txt", project.proy / "cambia.txt");
    fs::rename(project.proy / "dir-cambia", project.proy / "dir-vieja");
    symlink_or_skip(project.outside, project.proy / "dir-cambia");
    fs::rename(project.proy / "dir-moss", project.proy / "dir-moss-vieja");
    symlink_or_skip(project.moss, project.proy / "dir-moss");

    const PathError last = read_failure(root, file);
    CHECK(last.kind == PathErrorKind::ReadFailed);
    CHECK(last.message == "Path changed while it was being opened: cambia.txt");
    CHECK(read_failure(root, through_dir).kind == PathErrorKind::Outside);
    const PathError moss = read_failure(root, into_moss);
    CHECK(moss.kind == PathErrorKind::Blocked);
    CHECK(moss.message.find("no-debe-salir") == std::string::npos);
}

#ifndef _WIN32
TEST_CASE("ProjectRoot: read_file no se cuelga con una FIFO", "[project_root]") {
    const Project project;
    REQUIRE(::mkfifo((project.proy / "tubo").c_str(), 0600) == 0);
    const ProjectRoot root = open_root(project.proy);
    const fs::path fifo = resolved(root, "tubo");
    const PathError error = read_failure(root, fifo);
    CHECK(error.kind == PathErrorKind::WrongType);
    // walk tampoco la entrega: no es un archivo normal.
    CHECK(walk_all(root, root.path()) == std::vector<std::string>{"src/a.txt"});
}
#endif

TEST_CASE("ProjectRoot: display relativa a la raíz con /", "[project_root]") {
    const Project project;
    const ProjectRoot root = open_root(project.proy);
    CHECK(root.display(root.path()) == ".");
    CHECK(root.display(root.path() / "src" / "a.txt") == "src/a.txt");
    CHECK(root.display(root.path() / "src") == "src");
    CHECK(root.display(project.outside) == utf8(project.outside));
}

TEST_CASE("is_reserved_windows_name: una sola regla en el núcleo", "[project_root]") {
    for (const char* name : {"con", "CON", "con.cpp", "con .txt", "Aux.tar.gz", "nul", "prn.log",
                             "conin$", "CONOUT$.log", "com1", "LPT9", "com\xC2\xB9",
                             "lpt\xC2\xB3.txt"}) {
        INFO(name);
        CHECK(chatbot::is_reserved_windows_name(name));
    }
    for (const char* name : {"com0", "lpt0.txt", "com10", "console", "com", "", ".con",
                             "nul-algo", "año.con", "coma"}) {
        INFO(name);
        CHECK_FALSE(chatbot::is_reserved_windows_name(name));
    }
}

TEST_CASE("check_path_text: reglas de Windows (pura, en todos los sistemas)", "[project_root]") {
    const std::vector<std::string> invalid{
        "C:x",          "C:",           "c:archivo.txt", "NUL",           "nul.txt",
        "src/COM1",     "Lpt9.log",     "con .txt",      "COM\xC2\xB9",   "CONIN$",
        "aux",          "src\\prn.c",   "archivo.txt:flujo", "a/b:c/d",   "src/a.txt::$DATA",
        "\\\\?\\C:\\x", "//?/C:/x",     "\\\\.\\pipe\\x", "\\\\.\\C:\\x", "\\??\\C:\\x",
        "src/x.",       "src/x ",       "a<b",           "a|b",           "a*",
        "a?",           "a\"b"};
    for (const std::string& path : invalid) {
        INFO(path);
        const std::optional<PathError> error = check_path_text(path, Os::Windows);
        REQUIRE(error.has_value());
        CHECK(error->kind == PathErrorKind::Invalid);
        CHECK(error->message.find(path) != std::string::npos);
        // En POSIX son nombres normales.
        CHECK_FALSE(check_path_text(path, Os::Posix).has_value());
    }
    const std::optional<PathError> tab = check_path_text("a\tb", Os::Windows);
    REQUIRE(tab.has_value());
    CHECK(tab->message.find("a\\x09b") != std::string::npos);
    CHECK_FALSE(check_path_text("a\tb", Os::Posix).has_value());

    const std::vector<std::string> valid{
        "C:\\x",        "C:/x",        "c:\\",          "src\\a.txt",     "src/a.txt",
        "\\\\servidor\\recurso\\x",    "\\x",           "..\\x",          "./a",
        "nul-no.txt",   "console.txt", "COM10",         "LPT",            "comida",
        "ñandú.txt",    "a b/c d.txt", ".gitignore",    "src/",           ".",
        "com0",         "lpt0.txt"};
    for (const std::string& path : valid) {
        INFO(path);
        CHECK_FALSE(check_path_text(path, Os::Windows).has_value());
        CHECK_FALSE(check_path_text(path, Os::Posix).has_value());
    }
    for (const Os os : {Os::Posix, Os::Windows}) {
        CHECK(check_path_text("", os).has_value());
        CHECK(check_path_text(std::string{"a\0b", 3}, os).has_value());
        CHECK(check_path_text("\xFF", os).has_value());
        CHECK(check_path_text(std::string(4097, 'a'), os).has_value());
        CHECK_FALSE(check_path_text(std::string(4096, 'a'), os).has_value());
    }
    // Los tramos se cuentan con los separadores de cada sistema.
    std::string backslashes;
    for (int i = 0; i < 257; ++i) {
        backslashes += "a\\";
    }
    CHECK(check_path_text(backslashes, Os::Windows).has_value());
    CHECK_FALSE(check_path_text(backslashes, Os::Posix).has_value());
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
    CHECK(resolved(root, "..\\PROY\\src\\a.txt") == root.path() / "src" / "a.txt");
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

    // La raíz de una unidad no se abre.
    const chatbot::Result<ProjectRoot> drive_root = ProjectRoot::open(drive + "\\", {});
    REQUIRE(drive_root.is_error());
    CHECK(drive_root.error().message.find("raíz") != std::string::npos);
}
