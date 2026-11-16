#include "chatbot/code/code_limits.h"

#include "chatbot/config.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace chatbot::code {

namespace {

struct Field {
    const char* key;
    std::size_t CodeLimits::*member;
    std::size_t min;
    std::size_t max;
};

constexpr Field kFields[] = {
    {"max_steps", &CodeLimits::max_steps, 1, 1000},
    {"max_repeated_calls", &CodeLimits::max_repeated_calls, 2, 100},
    {"max_invalid_calls", &CodeLimits::max_invalid_calls, 1, 100},
    {"history_limit_bytes", &CodeLimits::history_limit_bytes, 0, 100000000},
};

} // namespace

LoadedCodeLimits load_code_limits(const std::string& config_path) {
    LoadedCodeLimits loaded;
    try {
        const Result<std::optional<std::string>> section = load_config_section(config_path, "code");
        if (section.is_error()) {
            loaded.warnings.push_back(section.error().message +
                                      " Moss Code usa sus límites predeterminados.");
            return loaded;
        }
        if (!section.value().has_value()) {
            return loaded;
        }
        const nlohmann::json code = nlohmann::json::parse(*section.value(), nullptr, false);
        if (!code.is_object()) {
            loaded.warnings.push_back("\"code\" en config.json no es un objeto: Moss Code usa sus "
                                      "límites predeterminados.");
            return loaded;
        }
        for (const Field& field : kFields) {
            const auto it = code.find(field.key);
            if (it == code.end()) {
                continue;
            }
            // Un entero no negativo llega como number_unsigned; negativos, decimales y demás no.
            if (it->is_number_unsigned()) {
                const auto value = it->get<std::uint64_t>();
                if (value >= field.min && value <= field.max) {
                    loaded.limits.*field.member = static_cast<std::size_t>(value);
                    continue;
                }
            }
            loaded.warnings.push_back(
                "\"code." + std::string{field.key} + "\" en config.json tiene que ser un entero entre " +
                std::to_string(field.min) + " y " + std::to_string(field.max) + "; se usa " +
                std::to_string(CodeLimits{}.*field.member) + ".");
        }
    } catch (...) {
        loaded = LoadedCodeLimits{};
        loaded.warnings.push_back("No se pudo leer \"code\" de config.json: Moss Code usa sus "
                                  "límites predeterminados.");
    }
    return loaded;
}

} // namespace chatbot::code
