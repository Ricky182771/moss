// ProjectRoot en POSIX (CMake elige este archivo o project_root_windows.cpp): las rutas son
// bytes y los nombres distinguen mayúsculas.

#include "chatbot/code/project_root.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <string>

namespace chatbot::code {

namespace fs = std::filesystem;

namespace {

/// Cierra el descriptor al salir del ámbito.
class UniqueFd {
public:
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    [[nodiscard]] int get() const { return fd_; }

private:
    int fd_;
};

PathError read_error(int code, std::string_view shown) {
    std::string reason = "Cannot read file: ";
    if (code == ENOENT || code == ENOTDIR) {
        reason = "File not found: ";
    } else if (code == EACCES || code == EPERM) {
        reason = "Permission denied: ";
    } else if (code == ELOOP) {
        // O_NOFOLLOW: el último tramo se volvió un enlace después de resolverlo.
        reason = "Path changed while it was being opened: ";
    }
    return PathError{PathErrorKind::ReadFailed, reason + std::string{shown}};
}

} // namespace

fs::path ProjectRoot::canonical(const fs::path& absolute, std::error_code& error) {
    return fs::weakly_canonical(absolute, error);
}

bool ProjectRoot::same_component(const fs::path& a, const fs::path& b, bool /*ignore_case*/) {
    return a.native() == b.native();
}

std::optional<ProjectRoot::Identity> ProjectRoot::identity(const fs::path& target) {
    struct stat info {};
    if (::stat(target.c_str(), &info) != 0) {
        return std::nullopt;
    }
    return Identity{static_cast<std::uint64_t>(info.st_dev), 0,
                    static_cast<std::uint64_t>(info.st_ino)};
}

fs::path ProjectRoot::from_utf8(std::string_view text) {
    return fs::path{std::string{text}};
}

std::string ProjectRoot::to_utf8(const fs::path& target) {
    return target.native();
}

ReadResult ProjectRoot::open_and_read(const fs::path& target, std::size_t max_bytes,
                                      std::string_view shown) const {
    // O_NONBLOCK: abrir una FIFO no espera a quien escriba (después se rechaza por no ser un
    // archivo normal).
    const UniqueFd file{::open(target.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (file.get() < 0) {
        return read_error(errno, shown);
    }
    struct stat info {};
    if (::fstat(file.get(), &info) != 0) {
        return read_error(errno, shown);
    }
    if (S_ISDIR(info.st_mode)) {
        return PathError{PathErrorKind::WrongType,
                         "Is a directory, not a file: " + std::string{shown}};
    }
    if (!S_ISREG(info.st_mode)) {
        return PathError{PathErrorKind::WrongType,
                         "Not a regular file (device, FIFO or socket): " + std::string{shown}};
    }

    // La ruta real de este descriptor, no la que se pidió: un enlace cambiado entre resolver y
    // abrir (en cualquier tramo) se nota aquí.
    std::error_code error;
    const fs::path real = fs::read_symlink("/proc/self/fd/" + std::to_string(file.get()), error);
    if (!error) {
        if (std::optional<PathError> problem = verify(real, shown)) {
            return *std::move(problem);
        }
    } else {
        // Sin /proc: la ruta resuelta tiene que seguir llevando a este mismo archivo.
        struct stat again {};
        if (::stat(target.c_str(), &again) != 0 || again.st_dev != info.st_dev ||
            again.st_ino != info.st_ino) {
            return read_error(ELOOP, shown);
        }
    }

    FileContent content;
    content.size = static_cast<std::uint64_t>(info.st_size);
    content.modified = static_cast<std::int64_t>(info.st_mtim.tv_sec) * 1'000'000'000 +
                       static_cast<std::int64_t>(info.st_mtim.tv_nsec);
    // Un byte de más para saber si el archivo pasa del tope.
    const std::size_t limit = max_bytes < std::string::npos - 1 ? max_bytes + 1 : max_bytes;
    std::string& bytes = content.bytes;
    std::size_t used = 0;
    while (used < limit) {
        if (used == bytes.size()) {
            bytes.resize(std::min(limit, std::max<std::size_t>(64 * 1024, bytes.size() * 2)));
        }
        const ssize_t got = ::read(file.get(), bytes.data() + used, bytes.size() - used);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return read_error(errno, shown);
        }
        if (got == 0) {
            break;
        }
        used += static_cast<std::size_t>(got);
    }
    bytes.resize(used);
    if (bytes.size() > max_bytes) {
        bytes.resize(max_bytes);
        content.truncated = true;
    }
    return content;
}

} // namespace chatbot::code
