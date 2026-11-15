#ifndef CHATBOT_CODE_PROJECT_ROOT_H
#define CHATBOT_CODE_PROJECT_ROOT_H

#include "chatbot/cancel_token.h"
#include "chatbot/platform.h"
#include "chatbot/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
    Invalid,      ///< Vacía, larga, con NUL, UTF-8 inválido o un nombre que Windows no deja.
    Outside,      ///< Fuera de la raíz: en C1 se rechaza. Otra unidad u otro servidor (Windows)
                  ///< se rechazan sin tocarlos, antes de mirar si están bloqueados.
    Blocked,      ///< En una carpeta de Moss (configuración o datos): nunca, en ningún modo.
    Unresolvable, ///< No se pudo canonizar: ciclo de enlaces, enlace roto (abrirlo para escribir
                  ///< crearía su destino) o, en Windows, un enlace simbólico (no se siguen).
    WrongType,    ///< read_file: no es un archivo normal; walk: no es una carpeta.
    ReadFailed,   ///< read_file: no existe, sin permiso, error al leer o cambió al abrirlo.
};

/// Error de una ruta. message va en inglés porque lo lee el modelo; trae la ruta pedida
/// recortada y con los controles escapados, nunca contenido de archivos.
struct PathError {
    PathErrorKind kind = PathErrorKind::Invalid;
    std::string message;
};

/// La ruta canónica o el error.
using ResolvedPath = std::variant<std::filesystem::path, PathError>;

/// Topes de check_path_text y de la ruta que repiten los mensajes de error.
inline constexpr std::size_t kMaxPathBytes = 4096;
inline constexpr std::size_t kMaxPathComponents = 256;
inline constexpr std::size_t kMaxShownPathChars = 200;

/// Revisa el texto de una ruta antes de tocar el disco; nullopt si se puede seguir. En todos los
/// sistemas: vacía, más de kMaxPathBytes bytes o kMaxPathComponents tramos, byte NUL o UTF-8
/// inválido. Con Os::Windows además: \\?\ y \\.\, "C:x" (relativa a la unidad), < > : " | ? * y
/// controles (':' abriría un flujo alterno), nombres que terminan en punto o espacio, y
/// dispositivos (is_reserved_windows_name). Pura: recibe el sistema para probar las reglas de
/// Windows también en Linux.
[[nodiscard]] std::optional<PathError> check_path_text(std::string_view path, Os os);

/// Lo que devuelve read_file.
struct FileContent {
    std::string bytes;         ///< Hasta max_bytes.
    bool truncated = false;    ///< El archivo tenía más de max_bytes.
    std::uint64_t size = 0;    ///< Tamaño al abrirlo.
    std::int64_t modified = 0; ///< Fecha de modificación, opaca: solo se compara con otra de
                               ///< read_file (POSIX: ns desde 1970; Windows: FILETIME).
};
using ReadResult = std::variant<FileContent, PathError>;

/// Un archivo que entrega walk.
struct WalkFile {
    std::filesystem::path path; ///< Canónica.
    std::string display;        ///< ProjectRoot::display(path).
};

/// Raíz del proyecto del modo código: la frontera de lo que el modelo puede pedir. Inmutable: se
/// puede usar desde varios hilos.
/// - Dentro de la raíz: por componentes de la ruta canónica, exactos (nunca por prefijo de texto:
///   /a/proy no contiene /a/proy2; en Windows, con las mayúsculas que da el disco).
/// - Carpetas de Moss: por componentes (en Windows sin mayúsculas) y por identidad (dispositivo e
///   inodo; en Windows, volumen y FileId) de cada ancestro que existe, así que un sistema de
///   archivos sin mayúsculas, un montaje bind o una carpeta renombrada no se la saltan. La que no
///   existía al abrir se vuelve a buscar en cada consulta.
/// - Solo se abre lo que devuelven resolve, resolve_entry o walk, nunca el texto que pidió el
///   modelo; y para leer, read_file.
/// - Quien use rg o git ls-files en vez de walk: nunca --follow; las carpetas bloqueadas que estén
///   dentro de la raíz se le pasan como exclusiones; y cada ruta de su salida es texto ajeno: pasa
///   por resolve(), nunca por resolve_entry.
///
/// No cubre: la carrera entre resolver y usar una ruta fuera de read_file (rg, git, escribir), ni
/// otra ruta al mismo archivo dentro de la raíz que no es enlace (enlace duro, montaje bind).
class ProjectRoot {
public:
    /// Abre la raíz: root (donde arrancó `moss code` o la indicada, en UTF-8) se vuelve absoluta
    /// y canónica, con los enlaces resueltos; tiene que existir, ser carpeta y no ser la raíz del
    /// sistema ni de una unidad. blocked son las carpetas de Moss (configuración y datos) ya
    /// calculadas por quien llama; se canonizan igual y pueden no existir todavía. Falla
    /// (ErrorKind::Config, en español para el usuario) si la raíz es o está dentro de una.
    [[nodiscard]] static Result<ProjectRoot> open(std::string_view root,
                                                  const std::vector<std::string>& blocked);

    /// La raíz canónica.
    [[nodiscard]] const std::filesystem::path& path() const { return root_; }

    /// true si dir (UTF-8; p. ej. la carpeta personal, que calcula quien llama) es la misma carpeta
    /// que la raíz, por identidad. Para avisar, no para rechazar. Barata: un stat.
    [[nodiscard]] bool is_same_directory(std::string_view dir) const;

    /// Resuelve una ruta que mandó el modelo (UTF-8, relativa a la raíz o absoluta): check_path_text
    /// y luego weakly_canonical contra la raíz, repetido hasta que no cambie (enlaces resueltos en el
    /// tramo que existe, "." y ".." normalizados en el resto; en Windows, ".." léxico como en Win32).
    /// No hace falta que exista (write y edit crean archivos). Error si queda fuera de la raíz
    /// ("Path is outside the project: <ruta>") o en una carpeta bloqueada, también por un enlace.
    [[nodiscard]] ResolvedPath resolve(std::string_view request) const;

    /// Lo mismo que resolve con una ruta que dio el sistema (una entrada de carpeta, quizá un
    /// enlace, o una ruta ya canónica), sin check_path_text.
    [[nodiscard]] ResolvedPath resolve_entry(const std::filesystem::path& entry) const;

    /// Recorre a mano la carpeta start (ya resuelta) y entrega cada archivo normal a visitor, en
    /// orden estable: los archivos de cada carpeta por nombre y luego sus subcarpetas por nombre.
    /// Cada entrada pasa por resolve_entry; nunca baja por un enlace ni por una unión (si la ruta
    /// resuelta no es carpeta/nombre, la salta: "bucle -> ." termina), ni a lo bloqueado o lo de
    /// fuera, ni a las carpetas cuyo nombre (UTF-8) acepte skip_directory (vacío: ninguna). Una
    /// subcarpeta que no se puede leer se salta. Se detiene si cancel se cancela o visitor devuelve
    /// false. Error solo si start no se puede usar.
    [[nodiscard]] std::optional<PathError> walk(
        const std::filesystem::path& start,
        const std::function<bool(std::string_view name)>& skip_directory,
        const std::function<bool(const WalkFile& file)>& visitor, const CancelToken& cancel) const;

    /// Lee un archivo ya resuelto (de resolve, resolve_entry o walk), hasta max_bytes. Sin seguir
    /// un enlace en el último tramo, sin colgarse con una FIFO (solo archivos normales) y
    /// comprobando, ya abierto, que la ruta real de ese descriptor sigue dentro de la raíz y fuera
    /// de lo bloqueado; lee de ese mismo descriptor. POSIX: O_NOFOLLOW | O_NONBLOCK y
    /// /proc/self/fd (sin /proc, la identidad contra un stat nuevo). Windows: el handle con
    /// GetFileType y GetFinalPathNameByHandleW.
    [[nodiscard]] ReadResult read_file(const std::filesystem::path& resolved,
                                       std::size_t max_bytes) const;

    /// Para mostrar (al modelo y en pantalla): target (canónica) relativa a la raíz y con "/", "."
    /// para la raíz misma; fuera de la raíz, la ruta completa. UTF-8 válido (U+FFFD en lo que no
    /// lo es) y controles escapados (\x0A); sin recortar, para que se pueda volver a pedir.
    [[nodiscard]] std::string display(const std::filesystem::path& target) const;

private:
    /// Identidad de una carpeta o archivo: POSIX, st_dev y st_ino; Windows, número de serie del
    /// volumen y FileId de 128 bits.
    struct Identity {
        std::uint64_t volume = 0;
        std::uint64_t high = 0;
        std::uint64_t low = 0;
        bool operator==(const Identity&) const = default;
    };
    struct BlockedDir {
        std::filesystem::path path;       ///< Canónica al abrir.
        std::optional<Identity> identity; ///< Si existía al abrir.
    };

    ProjectRoot(std::filesystem::path root, std::optional<Identity> root_identity,
                std::vector<BlockedDir> blocked)
        : root_(std::move(root)), root_identity_(root_identity), blocked_(std::move(blocked)) {}

    [[nodiscard]] ResolvedPath resolve_path(const std::filesystem::path& requested,
                                            std::string_view shown) const;
    /// Error si resolved (canónica) está bloqueada o fuera de la raíz.
    [[nodiscard]] std::optional<PathError> verify(const std::filesystem::path& resolved,
                                                  std::string_view shown) const;
    [[nodiscard]] bool is_blocked(const std::filesystem::path& resolved) const;
    /// false si se detuvo (cancelación o visitor).
    bool walk_directory(
        const std::filesystem::path& dir,
        const std::function<bool(std::string_view name)>& skip_directory,
        const std::function<bool(const WalkFile& file)>& visitor, const CancelToken& cancel) const;

    /// canonical repetido hasta que no cambie (tope de vueltas: error de ciclo).
    [[nodiscard]] static std::filesystem::path canonicalize(const std::filesystem::path& absolute,
                                                            std::error_code& error);
    /// true si child es parent o está dentro, componente por componente.
    [[nodiscard]] static bool is_within(const std::filesystem::path& child,
                                        const std::filesystem::path& parent, bool ignore_case);
    /// Identidades de target y de cada ancestro que existe.
    [[nodiscard]] static std::vector<Identity> ancestor_identities(
        const std::filesystem::path& target);
    /// El error que pone canonical en Windows al encontrar un enlace simbólico.
    [[nodiscard]] static std::error_code link_refused();

    // Lo que cambia por sistema: project_root_posix.cpp o project_root_windows.cpp (CMake elige).
    /// Una vuelta de canonización: en POSIX, weakly_canonical; en Windows, los tramos que existen
    /// mirados sin seguirlos (enlace simbólico: link_refused) y GetFinalPathNameByHandleW del
    /// último (resuelve uniones y da mayúsculas y nombres largos reales), más el resto.
    [[nodiscard]] static std::filesystem::path canonical(const std::filesystem::path& absolute,
                                                         std::error_code& error);
    /// Mismo nombre: bytes iguales en POSIX; en Windows, con ignore_case, CompareStringOrdinal
    /// sin mayúsculas.
    [[nodiscard]] static bool same_component(const std::filesystem::path& a,
                                             const std::filesystem::path& b, bool ignore_case);
    [[nodiscard]] static std::optional<Identity> identity(const std::filesystem::path& target);
    /// UTF-8 ↔ ruta sin pasar por la página de códigos ANSI (en Windows, por UTF-16). En POSIX,
    /// to_utf8 da los bytes tal cual (pueden no ser UTF-8).
    [[nodiscard]] static std::filesystem::path from_utf8(std::string_view text);
    [[nodiscard]] static std::string to_utf8(const std::filesystem::path& target);
    /// Abre, comprueba con verify la ruta real del descriptor y lee (read_file).
    [[nodiscard]] ReadResult open_and_read(const std::filesystem::path& target,
                                           std::size_t max_bytes, std::string_view shown) const;

    std::filesystem::path root_;
    std::optional<Identity> root_identity_;
    std::vector<BlockedDir> blocked_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_PROJECT_ROOT_H
