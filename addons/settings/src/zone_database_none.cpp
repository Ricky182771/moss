#include "zone_database.h"

#include "chatbot/error.h"

#include <optional>
#include <string>
#include <vector>

namespace chatbot::addon_settings::detail {

bool zone_database_available() {
    return false;
}

std::vector<std::string> zone_names() {
    return {};
}

Result<DayFunction> named_zone_day(std::string_view /*name*/) {
    return ChatError{ErrorKind::Config, 0,
                     "esta compilación no tiene la base de zonas horarias de C++20 (p. ej. Clang "
                     "con libc++)",
                     std::nullopt};
}

} // namespace chatbot::addon_settings::detail
