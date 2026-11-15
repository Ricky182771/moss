// ProjectRoot en Windows con Win32: uniones (no piden privilegios) y enlaces simbólicos (que no
// se siguen; si Windows no deja crearlos, SKIP). CMake solo compila este archivo en Windows.

#include "project_root_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <winioctl.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

using namespace project_root_test;

namespace {

/// Quita las uniones y los enlaces de carpeta al terminar, antes de que ScopedTempDir borre la
/// carpeta (su remove_all los seguiría, y uno apunta a un servidor). RemoveDirectoryW quita el
/// punto de reparación, no su destino.
class ReparseCleanup {
public:
    ReparseCleanup() = default;
    ReparseCleanup(const ReparseCleanup&) = delete;
    ReparseCleanup& operator=(const ReparseCleanup&) = delete;
    ~ReparseCleanup() {
        for (const fs::path& link : links_) {
            ::RemoveDirectoryW(link.c_str());
        }
    }
    void add(const fs::path& link) { links_.push_back(link); }

private:
    std::vector<fs::path> links_;
};

/// La cabecera de REPARSE_DATA_BUFFER para una unión (MountPointReparseBuffer), que no está en
/// los headers de modo usuario. La siguen el nombre NT y el nombre para mostrar, con sus NUL.
struct MountPointHeader {
    DWORD reparse_tag;
    WORD reparse_data_length;
    WORD reserved;
    WORD substitute_name_offset;
    WORD substitute_name_length;
    WORD print_name_offset;
    WORD print_name_length;
};

/// Crea la unión link → target (absoluta) con FSCTL_SET_REPARSE_POINT, como mklink /J.
void create_junction(const fs::path& target, const fs::path& link, ReparseCleanup& cleanup) {
    REQUIRE(::CreateDirectoryW(link.c_str(), nullptr) != 0);
    cleanup.add(link);
    const std::wstring print = target.native();
    const std::wstring substitute = L"\\??\\" + print;
    const std::size_t substitute_bytes = substitute.size() * sizeof(wchar_t);
    const std::size_t print_bytes = print.size() * sizeof(wchar_t);
    const std::size_t path_bytes = substitute_bytes + print_bytes + 2 * sizeof(wchar_t);

    MountPointHeader header{};
    header.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    header.reparse_data_length = static_cast<WORD>(4 * sizeof(WORD) + path_bytes);
    header.substitute_name_offset = 0;
    header.substitute_name_length = static_cast<WORD>(substitute_bytes);
    header.print_name_offset = static_cast<WORD>(substitute_bytes + sizeof(wchar_t));
    header.print_name_length = static_cast<WORD>(print_bytes);
    std::vector<char> buffer(sizeof header + path_bytes, '\0');
    std::memcpy(buffer.data(), &header, sizeof header);
    std::memcpy(buffer.data() + sizeof header, substitute.data(), substitute_bytes);
    std::memcpy(buffer.data() + sizeof header + header.print_name_offset, print.data(),
                print_bytes);

    HANDLE handle = ::CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                  nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    const BOOL ok = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                      static_cast<DWORD>(buffer.size()), nullptr, 0, &returned,
                                      nullptr);
    const DWORD error = ::GetLastError();
    ::CloseHandle(handle);
    INFO("FSCTL_SET_REPARSE_POINT, código " << error);
    REQUIRE(ok != 0);
}

/// Crea un enlace simbólico de carpeta, o salta la prueba si Windows no deja (sin modo de
/// desarrollador ni privilegio).
void dir_symlink_or_skip(const std::wstring& target, const fs::path& link,
                         ReparseCleanup& cleanup) {
    if (::CreateSymbolicLinkW(link.c_str(), target.c_str(),
                              SYMBOLIC_LINK_FLAG_DIRECTORY |
                                  SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == 0) {
        SKIP("Windows no dejó crear un enlace simbólico (código " << ::GetLastError() << ")");
    }
    cleanup.add(link);
}

} // namespace

TEST_CASE("ProjectRoot (Windows): una unión dentro de la raíz se sigue", "[project_root]") {
    const Project project;
    ReparseCleanup cleanup;
    create_junction(project.proy / "src", project.proy / "union-src", cleanup);
    const ProjectRoot root = open_root(project.proy);
    // GetFinalPathNameByHandleW resuelve la unión.
    CHECK(resolved(root, "union-src\\a.txt") == root.path() / "src" / "a.txt");
    CHECK(resolved(root, "union-src\\nuevo.txt") == root.path() / "src" / "nuevo.txt");
    CHECK(resolved(root.resolve_entry(root.path() / "union-src")) == root.path() / "src");
    CHECK(read_ok(root, resolved(root, "union-src\\a.txt"), 100).bytes == "a");
    // walk no baja por ella: src/a.txt sale una sola vez.
    CHECK(walk_all(root, root.path()) == std::vector<std::string>{"src/a.txt"});
}

TEST_CASE("ProjectRoot (Windows): uniones hacia fuera y hacia la carpeta de Moss",
          "[project_root]") {
    const Project project;
    ReparseCleanup cleanup;
    create_junction(project.outside, project.proy / "union-fuera", cleanup);
    create_junction(project.moss, project.proy / "union-moss", cleanup);
    const ProjectRoot root = open_root(project.proy, {project.moss});

    CHECK(failure_kind(root, "union-fuera\\secreto.txt") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "union-fuera") == PathErrorKind::Outside);
    CHECK(failure_kind(root, "union-moss\\credentials.json") == PathErrorKind::Blocked);
    CHECK(walk_all(root, root.path()) == std::vector<std::string>{"src/a.txt"});

    // En Win32 el ".." es léxico: se quita antes de mirar la unión. En POSIX, el mismo texto con
    // un enlace subiría desde el destino (test_project_root.cpp, "enlace en un tramo intermedio").
    CHECK(resolved(root, "union-fuera\\..\\src\\a.txt") == root.path() / "src" / "a.txt");

    // read_file comprueba la ruta real del handle: con una ruta sin resolver que cruza la unión
    // (como si la hubieran cambiado después de resolver), no lee nada de fuera ni de Moss.
    CHECK(read_failure(root, root.path() / "union-fuera" / "secreto.txt").kind ==
          PathErrorKind::Outside);
    const PathError moss = read_failure(root, root.path() / "union-moss" / "credentials.json");
    CHECK(moss.kind == PathErrorKind::Blocked);
    CHECK(moss.message.find("no-debe-salir") == std::string::npos);
}

TEST_CASE("ProjectRoot (Windows): los enlaces simbólicos no se siguen", "[project_root]") {
    const Project project;
    ReparseCleanup cleanup;
    dir_symlink_or_skip((project.proy / "src").native(), project.proy / "enlace-src", cleanup);
    // Un enlace hacia un servidor: mirarlo sin seguirlo no conecta con él.
    dir_symlink_or_skip(L"\\\\servidor-que-no-existe\\recurso", project.proy / "red", cleanup);
    const ProjectRoot root = open_root(project.proy);

    const PathError local = failure(root.resolve("enlace-src\\a.txt"));
    CHECK(local.kind == PathErrorKind::Unresolvable);
    CHECK(local.message == "Symbolic links are not followed on Windows: enlace-src\\a.txt");

    const auto start = std::chrono::steady_clock::now();
    CHECK(failure_kind(root, "red\\x.txt") == PathErrorKind::Unresolvable);
    CHECK(failure(root.resolve_entry(root.path() / "red")).kind == PathErrorKind::Unresolvable);
    CHECK(walk_all(root, root.path()) == std::vector<std::string>{"src/a.txt"});
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    WARN("enlace hacia \\\\servidor-que-no-existe: " << ms << " ms");
    CHECK(ms < 2000);
}
