// ProjectRoot: lo común a todos los sistemas. Lo que cambia (canonizar, comparar nombres y
// convertir a UTF-8) está en project_root_posix.cpp o project_root_windows.cpp.

#include "chatbot/code/project_root.h"

#include "chatbot/utf8.h"

#include <utility>

namespace chatbot::code {

namespace fs = std::filesystem;

namespace {

PathError path_error(PathErrorKind kind, std::string message) {
    return PathError{kind, std::move(message)};
}

ChatError config_error(std::string message) {
    return ChatError{ErrorKind::Config, 0, std::move(message), std::nullopt};
}

bool is_separator(char c) {
    return c == '/' || c == '\\';
}

bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// true si name es un dispositivo de Windows (CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM0 a COM9,
/// LPT0 a LPT9 y COM/LPT con ¹ ² ³) sin distinguir mayúsculas, también con extensión: Windows
/// mira lo que va antes del primer punto, sin los espacios del final ("nul .txt").
bool is_reserved_device(std::string_view name) {
    std::string_view stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') {
        stem.remove_suffix(1);
    }
    std::string device{stem};
    for (char& c : device) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (device == "con" || device == "prn" || device == "aux" || device == "nul" ||
        device == "conin$" || device == "conout$") {
        return true;
    }
    if (device.compare(0, 3, "com") != 0 && device.compare(0, 3, "lpt") != 0) {
        return false;
    }
    const std::string_view number = std::string_view{device}.substr(3); // size() >= 3 aquí.
    return (number.size() == 1 && number[0] >= '0' && number[0] <= '9') ||
           number == "\xC2\xB9" || number == "\xC2\xB2" || number == "\xC2\xB3";
}

std::optional<PathError> check_windows_text(std::string_view path) {
    const std::string shown{path};
    if (path.size() >= 3 && is_separator(path[0]) && is_separator(path[1]) &&
        (path[2] == '?' || path[2] == '.') && (path.size() == 3 || is_separator(path[3]))) {
        return path_error(PathErrorKind::Invalid,
                          "Invalid path: \\\\?\\ and \\\\.\\ paths are not supported: " + shown);
    }
    std::string_view rest = path;
    if (path.size() >= 2 && path[1] == ':' && is_ascii_letter(path[0])) {
        if (path.size() == 2 || !is_separator(path[2])) {
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
        if (is_reserved_device(part)) {
            return path_error(PathErrorKind::Invalid, "Invalid path: " + std::string{part} +
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

/// true si algún tramo de target es un enlace. Después de canonizar solo puede serlo uno roto:
/// weakly_canonical no lo sigue (su destino no existe), pero abrirlo para escribir crearía el
/// destino, quizá fuera de la raíz.
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

} // namespace

std::optional<PathError> check_path_text(std::string_view path, Os os) {
    if (path.empty()) {
        return path_error(PathErrorKind::Invalid, "Invalid path: the path is empty.");
    }
    if (path.find('\0') != std::string_view::npos) {
        return path_error(PathErrorKind::Invalid, "Invalid path: it contains a NUL byte.");
    }
    if (!utf8::is_valid(path)) {
        return path_error(PathErrorKind::Invalid, "Invalid path: it is not valid UTF-8.");
    }
    if (os == Os::Windows) {
        return check_windows_text(path);
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
        return without_trailing_separator(canonical(absolute, error));
    };

    if (!is_valid_text(root)) {
        return config_error("La carpeta del proyecto no es una ruta válida (vacía, con un byte "
                            "NUL o sin UTF-8 válido).");
    }
    const std::string shown{root};
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

    std::vector<fs::path> blocked_paths;
    for (const std::string& dir : blocked) {
        if (!is_valid_text(dir)) {
            return config_error("Una carpeta de Moss no es una ruta válida (vacía, con un byte "
                                "NUL o sin UTF-8 válido).");
        }
        fs::path blocked_path = canonize(dir, error);
        if (error) {
            return config_error("No se pudo resolver la carpeta de Moss " + dir + ": " +
                                error.message());
        }
        if (is_within(root_path, blocked_path)) {
            return config_error("No se puede usar " + shown +
                                " como carpeta del proyecto: es o está dentro de " + dir +
                                ", la carpeta de Moss con tu configuración o tus datos.");
        }
        blocked_paths.push_back(std::move(blocked_path));
    }
    return ProjectRoot{root_path, std::move(blocked_paths)};
}

ResolvedPath ProjectRoot::resolve(std::string_view request) const {
    if (std::optional<PathError> problem = check_path_text(request, current_os())) {
        return *std::move(problem);
    }
    return resolve_path(from_utf8(request), request);
}

ResolvedPath ProjectRoot::resolve_entry(const fs::path& entry) const {
    return resolve_path(entry, display(entry));
}

ResolvedPath ProjectRoot::resolve_path(const fs::path& requested, std::string_view shown) const {
    const std::string outside = "Path is outside the project: " + std::string{shown};
    // Otra unidad u otro servidor (Windows) queda fuera sin tocarlo: abrir \\servidor\x ya
    // conecta con él (y le manda las credenciales de Windows).
    if (requested.has_root_name() && !same_component(requested.root_name(), root_.root_name())) {
        return path_error(PathErrorKind::Outside, outside);
    }
    std::error_code error;
    fs::path resolved = without_trailing_separator(canonical(root_ / requested, error));
    if (error) {
        return unresolvable(error, shown);
    }
    if (has_symlink(resolved)) {
        return path_error(PathErrorKind::Unresolvable,
                          "Cannot resolve path (broken symbolic link): " + std::string{shown});
    }
    for (const fs::path& blocked : blocked_) {
        if (is_within(resolved, blocked)) {
            return path_error(PathErrorKind::Blocked,
                              "Path is not allowed (Moss configuration or data folder): " +
                                  std::string{shown});
        }
    }
    if (!is_within(resolved, root_)) {
        return path_error(PathErrorKind::Outside, outside);
    }
    return resolved;
}

std::string ProjectRoot::display(const fs::path& target) const {
    if (!is_within(target, root_)) {
        return to_utf8(target);
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
    return text.empty() ? "." : text;
}

bool ProjectRoot::is_within(const fs::path& child, const fs::path& parent) {
    auto part = child.begin();
    for (const fs::path& expected : parent) {
        if (part == child.end() || !same_component(*part, expected)) {
            return false;
        }
        ++part;
    }
    return true;
}

} // namespace chatbot::code
