#ifndef CHATBOT_CODE_PROJECT_ROOT_H
#define CHATBOT_CODE_PROJECT_ROOT_H

#include "chatbot/platform.h"
#include "chatbot/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace chatbot::code {

/// Por qué no se puede usar una ruta que pidió el modelo.
enum class PathErrorKind {
    Invalid,      ///< Vacía, con NUL, UTF-8 inválido o un nombre que Windows no deja (check_path_text).
    Outside,      ///< Fuera de la raíz: en C1 se rechaza. Otra unidad u otro servidor (Windows)
                  ///< se rechazan sin tocarlos, antes de mirar si están bloqueados.
    Blocked,      ///< En una carpeta de Moss (configuración o datos): nunca, en ningún modo.
    Unresolvable, ///< El sistema no la pudo canonizar (ciclo de enlaces, demasiado larga) o pasa
                  ///< por un enlace roto (abrirlo para escribir crearía su destino).
};

/// Error de una ruta. message va en inglés porque lo lee el modelo; repite la ruta pedida (si
/// es UTF-8 válido), nunca contenido de archivos.
struct PathError {
    PathErrorKind kind = PathErrorKind::Invalid;
    std::string message;
};

/// La ruta canónica o el error.
using ResolvedPath = std::variant<std::filesystem::path, PathError>;

/// Revisa el texto de una ruta antes de tocar el disco; nullopt si se puede seguir. En todos los
/// sistemas: vacía, byte NUL o UTF-8 inválido. Con Os::Windows además: \\?\ y \\.\, "C:x"
/// (relativa a la unidad), < > : " | ? * y controles (':' abriría un flujo alterno), nombres que
/// terminan en punto o espacio, y dispositivos (CON, NUL, COM1, LPT¹…, también con extensión).
/// Pura: recibe el sistema para probar las reglas de Windows también en Linux.
[[nodiscard]] std::optional<PathError> check_path_text(std::string_view path, Os os);

/// Raíz del proyecto del modo código: la frontera de lo que el modelo puede pedir. Decide por
/// componentes de la ruta canónica, nunca por prefijo de texto (/a/proy no contiene /a/proy2), sin
/// distinguir mayúsculas en Windows. Inmutable: se puede usar desde varios hilos.
/// En Windows, std::filesystem de MinGW no reconoce enlaces ni uniones (directory_entry::is_symlink
/// da false): los recorridos pasan cada carpeta por resolve_entry antes de entrar.
///
/// No cubre: la carrera entre resolver y abrir (si alguien cambia un enlace en medio, se abre el
/// destino nuevo), ni otra ruta al mismo archivo que no es enlace (enlaces duros, montajes bind).
class ProjectRoot {
public:
    /// Abre la raíz: root (donde arrancó `moss code` o la indicada, en UTF-8) se vuelve absoluta
    /// y canónica, con los enlaces resueltos; tiene que existir y ser carpeta. blocked son las
    /// carpetas de Moss (configuración y datos) ya calculadas por quien llama; se canonizan igual
    /// y pueden no existir todavía. Falla (ErrorKind::Config, en español para el usuario) si la
    /// raíz es o está dentro de una bloqueada.
    [[nodiscard]] static Result<ProjectRoot> open(std::string_view root,
                                                  const std::vector<std::string>& blocked);

    /// La raíz canónica.
    [[nodiscard]] const std::filesystem::path& path() const { return root_; }

    /// Resuelve una ruta que mandó el modelo (UTF-8, relativa a la raíz o absoluta): check_path_text
    /// y luego weakly_canonical contra la raíz (enlaces resueltos en el tramo que existe, "." y ".."
    /// normalizados en el resto). No hace falta que exista (write y edit crean archivos). Error si
    /// queda fuera de la raíz ("Path is outside the project: <ruta>") o en una carpeta bloqueada,
    /// también si se llega por un enlace.
    [[nodiscard]] ResolvedPath resolve(std::string_view request) const;

    /// Para los recorridos de glob y grep: lo mismo que resolve con una ruta que dio el sistema (una
    /// entrada de carpeta, quizá un enlace, o una ruta ya canónica). Error si está bloqueada o lleva
    /// fuera: así se saltan la carpeta de Moss dentro de la raíz y no siguen enlaces hacia fuera.
    [[nodiscard]] ResolvedPath resolve_entry(const std::filesystem::path& entry) const;

    /// Para mostrar (al modelo y en pantalla): target (canónica) relativa a la raíz y con "/", "."
    /// para la raíz misma; fuera de la raíz, la ruta completa. Siempre UTF-8 válido (U+FFFD en
    /// lo que no lo es).
    [[nodiscard]] std::string display(const std::filesystem::path& target) const;

private:
    ProjectRoot(std::filesystem::path root, std::vector<std::filesystem::path> blocked)
        : root_(std::move(root)), blocked_(std::move(blocked)) {}

    [[nodiscard]] ResolvedPath resolve_path(const std::filesystem::path& requested,
                                            std::string_view shown) const;

    /// true si child es parent o está dentro, comparando componente por componente.
    [[nodiscard]] static bool is_within(const std::filesystem::path& child,
                                        const std::filesystem::path& parent);

    // Lo que cambia por sistema: project_root_posix.cpp o project_root_windows.cpp (CMake elige).
    /// absolute canónica: en POSIX, weakly_canonical; en Windows, GetFinalPathNameByHandleW del
    /// tramo que existe (resuelve enlaces y uniones, que libstdc++ no ve) más el resto.
    [[nodiscard]] static std::filesystem::path canonical(const std::filesystem::path& absolute,
                                                         std::error_code& error);
    /// Mismo nombre: bytes iguales en POSIX; CompareStringOrdinal sin mayúsculas en Windows.
    [[nodiscard]] static bool same_component(const std::filesystem::path& a,
                                             const std::filesystem::path& b);
    /// UTF-8 ↔ ruta sin pasar por la página de códigos ANSI (en Windows, por UTF-16).
    [[nodiscard]] static std::filesystem::path from_utf8(std::string_view text);
    [[nodiscard]] static std::string to_utf8(const std::filesystem::path& target);

    std::filesystem::path root_;
    std::vector<std::filesystem::path> blocked_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_PROJECT_ROOT_H
