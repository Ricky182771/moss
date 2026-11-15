// ProjectRoot en Windows (CMake elige este archivo o project_root_posix.cpp). std::filesystem de
// MinGW no reconoce enlaces simbólicos ni uniones (su lstat es stat) ni corrige mayúsculas o
// nombres cortos (PROGRA~1): cada tramo se mira con FindFirstFileW sin seguirlo y la ruta canónica
// sale de GetFinalPathNameByHandleW. Llamadas W con UTF-16 explícito, sin la página de códigos
// ANSI.

#include "chatbot/code/project_root.h"

#include "chatbot/platform_windows.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace chatbot::code {

namespace fs = std::filesystem;

namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const { ::CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

/// CreateFileW con compartición amplia; nullptr si falla (GetLastError dice por qué).
UniqueHandle open_handle(const fs::path& target, DWORD access, DWORD flags) {
    HANDLE raw = ::CreateFileW(target.c_str(), access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, flags, nullptr);
    return UniqueHandle{raw == INVALID_HANDLE_VALUE ? nullptr : raw};
}

std::error_code windows_error(DWORD code) {
    // Nunca 0: un error_code en 0 se leería como éxito.
    return {static_cast<int>(code != 0 ? code : ERROR_GEN_FAILURE), std::system_category()};
}

bool is_separator(wchar_t c) {
    return c == L'\\' || c == L'/';
}

/// "\\?\C:\x" → "C:\x"; "\\?\UNC\servidor\recurso\x" → "\\servidor\recurso\x".
std::wstring without_extended_prefix(std::wstring_view text) {
    constexpr std::wstring_view unc = L"\\\\?\\UNC\\";
    constexpr std::wstring_view local = L"\\\\?\\";
    if (text.starts_with(unc)) {
        return L"\\\\" + std::wstring{text.substr(unc.size())};
    }
    if (text.starts_with(local)) {
        return std::wstring{text.substr(local.size())};
    }
    return std::wstring{text};
}

/// Ruta final de un archivo abierto: uniones resueltas, con las mayúsculas y los nombres largos
/// que tiene en el disco.
std::wstring final_path(HANDLE handle, std::error_code& error) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = ::GetFinalPathNameByHandleW(handle, buffer.data(),
                                               static_cast<DWORD>(buffer.size()), flags);
    if (length >= buffer.size()) {
        buffer.resize(length); // No cupo: length ya cuenta el NUL final.
        length = ::GetFinalPathNameByHandleW(handle, buffer.data(),
                                             static_cast<DWORD>(buffer.size()), flags);
    }
    if (length == 0) {
        error = windows_error(::GetLastError());
        return {};
    }
    if (length >= buffer.size()) {
        error = windows_error(ERROR_INSUFFICIENT_BUFFER);
        return {};
    }
    buffer.resize(length);
    return without_extended_prefix(buffer);
}

/// true si es un punto de reparación que lleva a otra ruta y no es una unión: un enlace simbólico
/// (de Win32 o de WSL) o uno que no conocemos.
bool is_refused_reparse(DWORD attributes, DWORD tag) {
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && tag != IO_REPARSE_TAG_MOUNT_POINT &&
           IsReparseTagNameSurrogate(tag) != 0;
}

PathError read_error(DWORD code, std::string_view shown) {
    std::string reason = "Cannot read file: ";
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
        reason = "File not found: ";
    } else if (code == ERROR_ACCESS_DENIED) {
        reason = "Permission denied: ";
    } else if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
        reason = "File is in use by another program: ";
    }
    return PathError{PathErrorKind::ReadFailed, reason + std::string{shown}};
}

} // namespace

fs::path ProjectRoot::canonical(const fs::path& absolute, std::error_code& error) {
    error.clear();
    // Win32 quita los ".." antes de mirar el disco (GetFullPathNameW): aquí igual.
    fs::path normal = absolute.lexically_normal();
    normal.make_preferred();
    if (!normal.has_filename() && normal.has_relative_path()) {
        normal = normal.parent_path();
    }

    // Los tramos que existen, de la raíz hacia abajo, mirados sin seguirlos (FindFirstFileW da
    // la etiqueta del punto de reparación en dwReserved0): un enlace hacia \\servidor\x nunca se
    // abre, porque abrirlo ya manda las credenciales de Windows a ese servidor.
    // shortcut: en Windows no se siguen enlaces simbólicos (solo uniones, que son locales);
    // ampliar leyendo el destino con FSCTL_GET_REPARSE_POINT y rechazando solo los UNC.
    fs::path existing = normal.root_path();
    const fs::path relative = normal.relative_path();
    auto part = relative.begin();
    // En \\servidor\recurso\x el recurso es parte de la raíz: FindFirstFileW no lo ve.
    const std::wstring root_name = normal.root_name().native();
    if (root_name.size() > 2 && is_separator(root_name[0]) && is_separator(root_name[1]) &&
        part != relative.end()) {
        existing /= *part;
        ++part;
    }
    for (; part != relative.end(); ++part) {
        const fs::path next = existing / *part;
        WIN32_FIND_DATAW data{};
        HANDLE find = ::FindFirstFileW(next.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                break; // De aquí en adelante no existe.
            }
            error = windows_error(code);
            return {};
        }
        ::FindClose(find);
        if (is_refused_reparse(data.dwFileAttributes, data.dwReserved0)) {
            error = link_refused();
            return {};
        }
        existing = next;
    }

    // Abrirlo sigue las uniones (locales); una rota no se abre y da error.
    const UniqueHandle handle = open_handle(existing, 0, FILE_FLAG_BACKUP_SEMANTICS);
    if (!handle) {
        error = windows_error(::GetLastError());
        return {};
    }
    fs::path result{final_path(handle.get(), error)};
    if (error) {
        return {};
    }
    for (; part != relative.end(); ++part) {
        result /= *part;
    }
    return result;
}

bool ProjectRoot::same_component(const fs::path& a, const fs::path& b, bool ignore_case) {
    const std::wstring& left = a.native();
    const std::wstring& right = b.native();
    if (!ignore_case) {
        return left == right;
    }
    return ::CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::optional<ProjectRoot::Identity> ProjectRoot::identity(const fs::path& target) {
    const UniqueHandle handle = open_handle(target, 0, FILE_FLAG_BACKUP_SEMANTICS);
    if (!handle) {
        return std::nullopt;
    }
    FILE_ID_INFO info{};
    if (::GetFileInformationByHandleEx(handle.get(), FileIdInfo, &info,
                                       static_cast<DWORD>(sizeof info)) != 0) {
        Identity id;
        id.volume = info.VolumeSerialNumber;
        std::memcpy(&id.high, info.FileId.Identifier, sizeof id.high);
        std::memcpy(&id.low, info.FileId.Identifier + sizeof id.high, sizeof id.low);
        return id;
    }
    // Sin FileIdInfo (FAT, algunos recursos de red): el índice de 64 bits.
    BY_HANDLE_FILE_INFORMATION basic{};
    if (::GetFileInformationByHandle(handle.get(), &basic) == 0) {
        return std::nullopt;
    }
    return Identity{basic.dwVolumeSerialNumber, 0,
                    (std::uint64_t{basic.nFileIndexHigh} << 32) | basic.nFileIndexLow};
}

fs::path ProjectRoot::from_utf8(std::string_view text) {
    return fs::path{utf8_to_utf16(text)};
}

std::string ProjectRoot::to_utf8(const fs::path& target) {
    return utf16_to_utf8(target.native());
}

ReadResult ProjectRoot::open_and_read(const fs::path& target, std::size_t max_bytes,
                                      std::string_view shown) const {
    // FILE_FLAG_OPEN_REPARSE_POINT: si el último tramo se volvió un enlace después de resolverlo,
    // se abre el enlace y no su destino (que podría ser \\servidor\x).
    const UniqueHandle file =
        open_handle(target, GENERIC_READ,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
                        FILE_FLAG_SEQUENTIAL_SCAN);
    if (!file) {
        return read_error(::GetLastError(), shown);
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (::GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &tag,
                                       static_cast<DWORD>(sizeof tag)) == 0) {
        return read_error(::GetLastError(), shown);
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return PathError{PathErrorKind::WrongType,
                         "Is a directory, not a file: " + std::string{shown}};
    }
    if (is_refused_reparse(tag.FileAttributes, tag.ReparseTag)) {
        return PathError{PathErrorKind::ReadFailed,
                         "Path changed while it was being opened: " + std::string{shown}};
    }
    if (::GetFileType(file.get()) != FILE_TYPE_DISK) {
        return PathError{PathErrorKind::WrongType,
                         "Not a regular file (device or pipe): " + std::string{shown}};
    }

    // La ruta real de este handle, no la que se pidió: una unión cambiada entre resolver y abrir
    // se nota aquí.
    std::error_code error;
    const fs::path real{final_path(file.get(), error)};
    if (error) {
        return read_error(static_cast<DWORD>(error.value()), shown);
    }
    if (std::optional<PathError> problem = verify(real, shown)) {
        return *std::move(problem);
    }

    FileContent content;
    LARGE_INTEGER size{};
    FILETIME written{};
    if (::GetFileSizeEx(file.get(), &size) == 0 ||
        ::GetFileTime(file.get(), nullptr, nullptr, &written) == 0) {
        return read_error(::GetLastError(), shown);
    }
    content.size = static_cast<std::uint64_t>(size.QuadPart);
    content.modified = static_cast<std::int64_t>(
        (std::uint64_t{written.dwHighDateTime} << 32) | written.dwLowDateTime);

    // Un byte de más para saber si el archivo pasa del tope.
    const std::size_t limit = max_bytes < std::string::npos - 1 ? max_bytes + 1 : max_bytes;
    std::string& bytes = content.bytes;
    std::size_t used = 0;
    while (used < limit) {
        if (used == bytes.size()) {
            bytes.resize(std::min(limit, std::max<std::size_t>(64 * 1024, bytes.size() * 2)));
        }
        const DWORD want =
            static_cast<DWORD>(std::min<std::size_t>(bytes.size() - used, std::size_t{1} << 30));
        DWORD got = 0;
        if (::ReadFile(file.get(), bytes.data() + used, want, &got, nullptr) == 0) {
            return read_error(::GetLastError(), shown);
        }
        if (got == 0) {
            break;
        }
        used += got;
    }
    bytes.resize(used);
    if (bytes.size() > max_bytes) {
        bytes.resize(max_bytes);
        content.truncated = true;
    }
    return content;
}

} // namespace chatbot::code
