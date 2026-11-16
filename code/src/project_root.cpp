// ProjectRoot: lo común a todos los sistemas. Lo que cambia (canonizar, comparar nombres,
// identidad, convertir a UTF-8 y abrir para leer) está en project_root_posix.cpp o
// project_root_windows.cpp.

#include "chatbot/code/project_root.h"

#include "chatbot/utf8.h"

#include <algorithm>
#include <utility>

namespace chatbot::code {

namespace fs = std::filesystem;

namespace {

/// Vueltas de canonize antes de darlo por ciclo (como SYMLOOP_MAX de Linux).
constexpr int kMaxCanonicalRounds = 40;

PathError path_error(PathErrorKind kind, std::string message) {
    return PathError{kind, std::move(message)};
}

ChatError config_error(std::string message) {
    return ChatError{ErrorKind::Config, 0, std::move(message), std::nullopt};
}

bool is_separator(char c, Os os) {
    return c == '/' || (os == Os::Windows && c == '\\');
}

bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// Texto que se puede mostrar: U+FFFD por cada byte que no es UTF-8, controles (C0, DEL y C1)
/// escapados como \x0A o \u0085, y como mucho max_chars caracteres (con "…" si se corta).
std::string printable(std::string_view text, std::size_t max_chars) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(std::min(text.size(), max_chars) + 8);
    std::size_t chars = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (chars == max_chars) {
            out += "\xE2\x80\xA6"; // …
            break;
        }
        ++chars;
        const std::size_t begin = i;
        const std::optional<std::uint32_t> code = utf8::next_code_point(text, i);
        if (!code) {
            out += utf8::kReplacement;
            ++i;
        } else if (*code < 0x20 || *code == 0x7F || (*code >= 0x80 && *code <= 0x9F)) {
            out += *code < 0x80 ? "\\x" : "\\u00";
            out += hex[(*code >> 4) & 0xFU];
            out += hex[*code & 0xFU];
        } else {
            out.append(text.substr(begin, i - begin));
        }
    }
    return out;
}

std::string shown_path(std::string_view text) {
    return printable(text, kMaxShownPathChars);
}

std::optional<PathError> check_windows_text(std::string_view path, const std::string& shown) {
    if (path.size() >= 3 && is_separator(path[0], Os::Windows) &&
        is_separator(path[1], Os::Windows) && (path[2] == '?' || path[2] == '.') &&
        (path.size() == 3 || is_separator(path[3], Os::Windows))) {
        return path_error(PathErrorKind::Invalid,
                          "Invalid path: \\\\?\\ and \\\\.\\ paths are not supported: " + shown);
    }
    std::string_view rest = path;
    if (path.size() >= 2 && path[1] == ':' && is_ascii_letter(path[0])) {
        if (path.size() == 2 || !is_separator(path[2], Os::Windows)) {
            return path_error(PathErrorKind::Invalid,
                              "Invalid path: drive-relative paths like C:file are not supported; "
                              "use a path relative to the project or a full path: " + shown);
        }
        rest.remove_prefix(2);
    }
    while (!rest.empty()) {
        const std::size_t end = rest.find_first_of("/\\");
        const std::string_view part = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (part.empty() || part == "." || part == "..") {
            continue;
        }
        for (const char c : part) {
            if (static_cast<unsigned char>(c) < 0x20U ||
                std::string_view{"<>:\"|?*"}.find(c) != std::string_view::npos) {
                return path_error(PathErrorKind::Invalid,
                                  "Invalid path: Windows does not allow < > : \" | ? * or control "
                                  "characters in names (':' would open an alternate data "
                                  "stream): " + shown);
            }
        }
        if (part.back() == '.' || part.back() == ' ') {
            return path_error(PathErrorKind::Invalid,
                              "Invalid path: Windows names cannot end with a dot or a space: " +
                                  shown);
        }
        if (is_reserved_windows_name(part)) {
            return path_error(PathErrorKind::Invalid, "Invalid path: " + shown_path(part) +
                                                          " is a reserved device name on Windows: " +
                                                          shown);
        }
    }
    return std::nullopt;
}

/// Sin el separador final ("a/b/" → "a/b"), salvo en la raíz del sistema.
fs::path without_trailing_separator(fs::path target) {
    if (!target.has_filename() && target.has_relative_path()) {
        return target.parent_path();
    }
    return target;
}

/// true si algún tramo de target es un enlace. Ya canonizada hasta que no cambia, solo puede
/// serlo uno roto: weakly_canonical no lo sigue (su destino no existe), pero abrirlo para escribir
/// crearía el destino, quizá fuera de la raíz.
bool has_symlink(const fs::path& target) {
    fs::path prefix;
    for (const fs::path& part : target) {
        prefix /= part;
        std::error_code error;
        const fs::file_status status = fs::symlink_status(prefix, error);
        if (fs::is_symlink(status)) {
            return true;
        }
        if (error) {
            return false; // No existe: lo que sigue tampoco.
        }
    }
    return false;
}

PathError unresolvable(const std::error_code& error, std::string_view shown) {
    std::string reason = "the system could not resolve it";
    if (error == std::errc::too_many_symbolic_link_levels) {
        reason = "symbolic link loop";
    } else if (error == std::errc::filename_too_long) {
        reason = "path too long";
    } else if (error == std::errc::permission_denied) {
        reason = "permission denied";
    }
    return path_error(PathErrorKind::Unresolvable,
                      "Cannot resolve path (" + reason + "): " + std::string{shown});
}

bool is_valid_text(std::string_view text) {
    return !text.empty() && text.find('\0') == std::string_view::npos && utf8::is_valid(text);
}

/// Categoría del error de link_refused: solo la crea project_root_windows.cpp.
class LinkRefusedCategory final : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "moss.project_root"; }
    [[nodiscard]] std::string message(int /*value*/) const override {
        return "symbolic links are not followed";
    }
};

} // namespace

std::optional<PathError> check_path_text(std::string_view path, Os os) {
    if (path.empty()) {
        return path_error(PathErrorKind::Invalid, "Invalid path: the path is empty.");
    }
    const std::string shown = shown_path(path);
    if (path.size() > kMaxPathBytes) {
        return path_error(PathErrorKind::Invalid,
                          "Invalid path: longer than " + std::to_string(kMaxPathBytes) +
                              " bytes: " + shown);
    }
    if (path.find('\0') != std::string_view::npos) {
        return path_error(PathErrorKind::Invalid, "Invalid path: it contains a NUL byte: " + shown);
    }
    if (!utf8::is_valid(path)) {
        return path_error(PathErrorKind::Invalid, "Invalid path: it is not valid UTF-8: " + shown);
    }
    std::size_t components = 0;
    bool in_component = false;
    for (const char c : path) {
        const bool separator = is_separator(c, os);
        if (!separator && !in_component) {
            ++components;
        }
        in_component = !separator;
    }
    if (components > kMaxPathComponents) {
        return path_error(PathErrorKind::Invalid,
                          "Invalid path: more than " + std::to_string(kMaxPathComponents) +
                              " components: " + shown);
    }
    if (os == Os::Windows) {
        return check_windows_text(path, shown);
    }
    return std::nullopt;
}

Result<ProjectRoot> ProjectRoot::open(std::string_view root,
                                      const std::vector<std::string>& blocked) {
    // Absoluta (contra la carpeta actual) y canónica.
    const auto canonize = [](std::string_view text, std::error_code& error) {
        const fs::path absolute = fs::absolute(from_utf8(text), error);
        if (error) {
            return fs::path{};
        }
        return canonicalize(absolute, error);
    };

    if (!is_valid_text(root)) {
        return config_error("La carpeta del proyecto no es una ruta válida (vacía, con un byte "
                            "NUL o sin UTF-8 válido).");
    }
    const std::string shown = printable(root, kMaxShownPathChars);
    std::error_code error;
    const fs::path root_path = canonize(root, error);
    if (error) {
        return config_error("No se pudo resolver la carpeta del proyecto " + shown + ": " +
                            error.message());
    }
    const fs::file_status status = fs::status(root_path, error);
    if (status.type() == fs::file_type::not_found) {
        return config_error("La carpeta del proyecto no existe: " + shown);
    }
    if (error) {
        return config_error("No se pudo revisar la carpeta del proyecto " + shown + ": " +
                            error.message());
    }
    if (!fs::is_directory(status)) {
        return config_error("La ruta del proyecto no es una carpeta: " + shown);
    }
    if (!root_path.has_relative_path()) {
        return config_error("No se puede usar " + shown +
                            " como carpeta del proyecto: es la raíz del sistema de archivos o de "
                            "una unidad, y el modelo podría leer todo el disco. Abre moss code en "
                            "la carpeta de tu proyecto.");
    }

    const std::vector<Identity> root_ancestors = ancestor_identities(root_path);
    std::vector<BlockedDir> blocked_dirs;
    for (const std::string& dir : blocked) {
        if (!is_valid_text(dir)) {
            return config_error("Una carpeta de Moss no es una ruta válida (vacía, con un byte "
                                "NUL o sin UTF-8 válido).");
        }
        const std::string dir_shown = printable(dir, kMaxShownPathChars);
        BlockedDir blocked_dir{canonize(dir, error), std::nullopt};
        if (error) {
            return config_error("No se pudo resolver la carpeta de Moss " + dir_shown + ": " +
                                error.message());
        }
        blocked_dir.identity = identity(blocked_dir.path);
        if (is_within(root_path, blocked_dir.path, true) ||
            (blocked_dir.identity &&
             std::find(root_ancestors.begin(), root_ancestors.end(), *blocked_dir.identity) !=
                 root_ancestors.end())) {
            return config_error("No se puede usar " + shown +
                                " como carpeta del proyecto: es o está dentro de " + dir_shown +
                                ", la carpeta de Moss con tu configuración o tus datos.");
        }
        blocked_dirs.push_back(std::move(blocked_dir));
    }
    return ProjectRoot{root_path, identity(root_path), std::move(blocked_dirs)};
}

bool ProjectRoot::is_same_directory(std::string_view dir) const {
    if (!is_valid_text(dir) || !root_identity_) {
        return false;
    }
    std::error_code error;
    const fs::path absolute = fs::absolute(from_utf8(dir), error);
    const std::optional<Identity> other = error ? std::nullopt : identity(absolute);
    return other && *other == *root_identity_;
}

ResolvedPath ProjectRoot::resolve(std::string_view request) const {
    if (std::optional<PathError> problem = check_path_text(request, current_os())) {
        return *std::move(problem);
    }
    return resolve_path(from_utf8(request), shown_path(request));
}

ResolvedPath ProjectRoot::resolve_entry(const fs::path& entry) const {
    return resolve_path(entry, shown_path(display(entry)));
}

ResolvedPath ProjectRoot::resolve_path(const fs::path& requested, std::string_view shown) const {
    // Otra unidad u otro servidor (Windows) queda fuera sin tocarlo: abrir \\servidor\x ya
    // conecta con él (y le manda las credenciales de Windows).
    if (requested.has_root_name() &&
        !same_component(requested.root_name(), root_.root_name(), true)) {
        return path_error(PathErrorKind::Outside,
                          "Path is outside the project: " + std::string{shown});
    }
    std::error_code error;
    fs::path resolved = canonicalize(root_ / requested, error);
    if (error == link_refused()) {
        return path_error(PathErrorKind::Unresolvable,
                          "Symbolic links are not followed on Windows: " + std::string{shown});
    }
    if (error) {
        return unresolvable(error, shown);
    }
    if (has_symlink(resolved)) {
        return path_error(PathErrorKind::Unresolvable,
                          "Cannot resolve path (broken symbolic link): " + std::string{shown});
    }
    if (std::optional<PathError> problem = verify(resolved, shown)) {
        return *std::move(problem);
    }
    return resolved;
}

std::optional<PathError> ProjectRoot::verify(const fs::path& resolved,
                                             std::string_view shown) const {
    // Lo bloqueado primero: con permisos (C3), lo de fuera preguntará y esto nunca.
    if (is_blocked(resolved)) {
        return path_error(PathErrorKind::Blocked,
                          "Path is not allowed (Moss configuration or data folder): " +
                              std::string{shown});
    }
    if (!is_within(resolved, root_, false)) {
        return path_error(PathErrorKind::Outside,
                          "Path is outside the project: " + std::string{shown});
    }
    return std::nullopt;
}

bool ProjectRoot::is_blocked(const fs::path& resolved) const {
    std::optional<std::vector<Identity>> ancestors; // Se calculan una vez, si hacen falta.
    for (const BlockedDir& dir : blocked_) {
        if (is_within(resolved, dir.path, true)) {
            return true;
        }
        std::optional<Identity> id = dir.identity;
        if (!id) {
            // No existía al abrir: puede existir ahora, quizá por otro camino (un enlace nuevo
            // en su ruta).
            std::error_code error;
            const fs::path now = canonicalize(dir.path, error);
            if (error) {
                continue;
            }
            if (is_within(resolved, now, true)) {
                return true;
            }
            id = identity(now);
            if (!id) {
                continue;
            }
        }
        if (!ancestors) {
            ancestors = ancestor_identities(resolved);
        }
        if (std::find(ancestors->begin(), ancestors->end(), *id) != ancestors->end()) {
            return true;
        }
    }
    return false;
}

std::optional<PathError> ProjectRoot::walk(
    const fs::path& start, const std::function<bool(std::string_view name)>& skip_directory,
    const std::function<bool(const WalkFile& file)>& visitor, const CancelToken& cancel) const {
    ResolvedPath resolved = resolve_entry(start);
    if (auto* problem = std::get_if<PathError>(&resolved)) {
        return std::move(*problem);
    }
    const fs::path& dir = std::get<fs::path>(resolved);
    std::error_code error;
    if (!fs::is_directory(fs::symlink_status(dir, error))) {
        return path_error(PathErrorKind::WrongType,
                          "Not a directory: " + shown_path(display(dir)));
    }
    walk_directory(dir, skip_directory, visitor, cancel);
    return std::nullopt;
}

bool ProjectRoot::walk_directory(const fs::path& dir,
                                 const std::function<bool(std::string_view name)>& skip_directory,
                                 const std::function<bool(const WalkFile& file)>& visitor,
                                 const CancelToken& cancel) const {
    if (cancel.is_cancelled()) {
        return false;
    }
    std::vector<fs::path> names;
    std::error_code error;
    // Sin permiso o con un error a la mitad: se usa lo que se alcanzó a leer.
    for (fs::directory_iterator entry{dir, fs::directory_options::skip_permission_denied, error};
         !error && entry != fs::directory_iterator{}; entry.increment(error)) {
        names.push_back(entry->path().filename());
    }
    std::sort(names.begin(), names.end());

    std::vector<fs::path> subdirectories;
    for (const fs::path& name : names) {
        if (cancel.is_cancelled()) {
            return false;
        }
        const fs::path entry = dir / name;
        const ResolvedPath resolved = resolve_entry(entry);
        const fs::path* real = std::get_if<fs::path>(&resolved);
        if (real == nullptr || *real != entry) {
            continue; // Bloqueada, fuera, rota, o un enlace o una unión: no se sigue.
        }
        const fs::file_status status = fs::symlink_status(entry, error);
        if (fs::is_directory(status)) {
            if (!skip_directory || !skip_directory(to_utf8(name))) {
                subdirectories.push_back(entry);
            }
        } else if (fs::is_regular_file(status)) {
            if (!visitor(WalkFile{entry, display(entry)})) {
                return false;
            }
        }
    }
    for (const fs::path& subdirectory : subdirectories) {
        if (!walk_directory(subdirectory, skip_directory, visitor, cancel)) {
            return false;
        }
    }
    return true;
}

ReadResult ProjectRoot::read_file(const fs::path& resolved, std::size_t max_bytes) const {
    const std::string shown = shown_path(display(resolved));
    if (std::optional<PathError> problem = verify(resolved, shown)) {
        return *std::move(problem);
    }
    return open_and_read(resolved, max_bytes, shown);
}

std::vector<std::string> ProjectRoot::blocked_within() const {
    std::vector<std::string> inside;
    for (const BlockedDir& dir : blocked_) {
        std::error_code error;
        const fs::path now = canonicalize(dir.path, error);
        for (const fs::path& candidate : {dir.path, error ? dir.path : now}) {
            if (!is_within(candidate, root_, false)) {
                continue;
            }
            auto part = candidate.begin();
            for (auto skip = root_.begin(); skip != root_.end(); ++skip) {
                ++part;
            }
            std::string text;
            for (; part != candidate.end(); ++part) {
                if (!part->empty()) {
                    text += text.empty() ? "" : "/";
                    text += to_utf8(*part);
                }
            }
            if (!text.empty() && std::find(inside.begin(), inside.end(), text) == inside.end()) {
                inside.push_back(std::move(text));
            }
        }
    }
    return inside;
}

std::string ProjectRoot::display(const fs::path& target) const {
    constexpr std::size_t unlimited = std::string::npos;
    if (!is_within(target, root_, false)) {
        return printable(to_utf8(target), unlimited);
    }
    auto part = target.begin();
    for (auto skip = root_.begin(); skip != root_.end(); ++skip) {
        ++part;
    }
    std::string text;
    for (; part != target.end(); ++part) {
        if (part->empty()) {
            continue;
        }
        if (!text.empty()) {
            text += '/';
        }
        text += to_utf8(*part);
    }
    return text.empty() ? "." : printable(text, unlimited);
}

fs::path ProjectRoot::canonicalize(const fs::path& absolute, std::error_code& error) {
    // Una vuelta deja sin resolver un enlace que queda a la vista al quitar un ".." del tramo que
    // no existe ("nuevo/../atajo/a.txt"): se repite hasta que no cambia.
    fs::path current = absolute;
    for (int round = 0; round < kMaxCanonicalRounds; ++round) {
        fs::path next = without_trailing_separator(canonical(current, error));
        if (error) {
            return {};
        }
        if (next == current) {
            return next;
        }
        current = std::move(next);
    }
    error = std::make_error_code(std::errc::too_many_symbolic_link_levels);
    return {};
}

bool ProjectRoot::is_within(const fs::path& child, const fs::path& parent, bool ignore_case) {
    auto part = child.begin();
    for (const fs::path& expected : parent) {
        if (part == child.end() || !same_component(*part, expected, ignore_case)) {
            return false;
        }
        ++part;
    }
    return true;
}

std::vector<ProjectRoot::Identity> ProjectRoot::ancestor_identities(const fs::path& target) {
    std::vector<Identity> identities;
    fs::path current = target;
    while (true) {
        if (std::optional<Identity> id = identity(current)) {
            identities.push_back(*id);
        }
        if (!current.has_relative_path()) {
            return identities;
        }
        current = current.parent_path();
    }
}

std::error_code ProjectRoot::link_refused() {
    static const LinkRefusedCategory category{};
    return {1, category};
}

} // namespace chatbot::code
