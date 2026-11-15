// ProjectRoot en POSIX (CMake elige este archivo o project_root_windows.cpp): las rutas son
// bytes y los nombres distinguen mayúsculas.

#include "chatbot/code/project_root.h"

#include "chatbot/utf8.h"

namespace chatbot::code {

namespace fs = std::filesystem;

fs::path ProjectRoot::canonical(const fs::path& absolute, std::error_code& error) {
    return fs::weakly_canonical(absolute, error);
}

bool ProjectRoot::same_component(const fs::path& a, const fs::path& b) {
    return a.native() == b.native();
}

fs::path ProjectRoot::from_utf8(std::string_view text) {
    return fs::path{std::string{text}};
}

std::string ProjectRoot::to_utf8(const fs::path& target) {
    // Un nombre de archivo puede no ser UTF-8: U+FFFD por cada byte inválido.
    const std::string& text = target.native();
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t begin = i;
        if (!utf8::next_code_point(text, i)) {
            out += utf8::kReplacement;
            ++i;
            continue;
        }
        out.append(text, begin, i - begin);
    }
    return out;
}

} // namespace chatbot::code
