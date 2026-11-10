// Partes de la capa de plataforma que son iguales en todos los sistemas
// (funciones puras): se compilan siempre, junto con platform_posix.cpp o
// platform_windows.cpp.

#include "chatbot/platform.h"

#include "platform_internal.h"

#include <string_view>

namespace chatbot {

std::string without_deleted_suffix(std::string target,
                                   const std::function<bool(const std::string& path)>& exists) {
    constexpr std::string_view suffix = " (deleted)";
    if (target.size() > suffix.size() && target.ends_with(suffix) && !exists(target)) {
        std::string original = target.substr(0, target.size() - suffix.size());
        if (exists(original)) {
            return original;
        }
    }
    return target;
}

unsigned interactive_console_input_mode(unsigned previous) {
    return previous & ~kConsoleProcessedInput;
}

std::string join_windows_path(std::string_view base, std::string_view tail) {
    while (!base.empty() && (base.back() == '\\' || base.back() == '/')) {
        base.remove_suffix(1);
    }
    return std::string{base} + "\\" + std::string{tail};
}

} // namespace chatbot
