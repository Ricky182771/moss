// ProjectRoot en Windows (CMake elige este archivo o project_root_posix.cpp). std::filesystem de
// MinGW no reconoce enlaces simbólicos ni uniones (su lstat es stat) ni corrige mayúsculas o
// nombres cortos (PROGRA~1): la ruta canónica sale de GetFinalPathNameByHandleW. Llamadas W con
// UTF-16 explícito, sin la página de códigos ANSI.

#include "chatbot/code/project_root.h"

#include "chatbot/platform_windows.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace chatbot::code {

namespace fs = std::filesystem;

namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const { ::CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::error_code windows_error(DWORD code) {
    // Nunca 0: un error_code en 0 se leería como éxito.
    return {static_cast<int>(code != 0 ? code : ERROR_GEN_FAILURE), std::system_category()};
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

/// Ruta final de un archivo abierto: enlaces y uniones resueltos, con las mayúsculas y los
/// nombres largos que tiene en el disco.
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

} // namespace

fs::path ProjectRoot::canonical(const fs::path& absolute, std::error_code& error) {
    error.clear();
    // Win32 quita los ".." antes de mirar el disco (GetFullPathNameW): aquí igual.
    fs::path existing = absolute.lexically_normal();
    existing.make_preferred();
    if (!existing.has_filename() && existing.has_relative_path()) {
        existing = existing.parent_path();
    }
    std::vector<fs::path> missing; // Los tramos que no existen, del último al primero.
    UniqueHandle handle;
    while (true) {
        HANDLE raw = ::CreateFileW(existing.c_str(), 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (raw != INVALID_HANDLE_VALUE) {
            handle.reset(raw);
            break;
        }
        const DWORD code = ::GetLastError();
        const bool not_found = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
        // Existe pero no se abre: un enlace roto (o algo que no se deja ver). No se adivina
        // adónde lleva.
        if (!not_found || ::GetFileAttributesW(existing.c_str()) != INVALID_FILE_ATTRIBUTES ||
            !existing.has_relative_path()) {
            error = windows_error(code);
            return {};
        }
        missing.push_back(existing.filename());
        existing = existing.parent_path();
    }
    fs::path result{final_path(handle.get(), error)};
    if (error) {
        return {};
    }
    for (auto part = missing.rbegin(); part != missing.rend(); ++part) {
        result /= *part;
    }
    return result;
}

bool ProjectRoot::same_component(const fs::path& a, const fs::path& b) {
    const std::wstring& left = a.native();
    const std::wstring& right = b.native();
    return ::CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

fs::path ProjectRoot::from_utf8(std::string_view text) {
    return fs::path{utf8_to_utf16(text)};
}

std::string ProjectRoot::to_utf8(const fs::path& target) {
    return utf16_to_utf8(target.native());
}

} // namespace chatbot::code
