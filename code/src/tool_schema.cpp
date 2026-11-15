#include "tool_schema.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string_view>
#include <system_error>

namespace chatbot::code {

namespace {

using json = nlohmann::json;

std::string qualified(const std::string& parent, const std::string& key) {
    return parent.empty() ? key : parent + "." + key;
}

std::string argument(const std::string& name) {
    return "Argument '" + name + "'";
}

std::string_view schema_type(const json& schema) {
    const auto found = schema.find("type");
    if (found == schema.end() || !found->is_string()) {
        return {};
    }
    return found->get_ref<const std::string&>();
}

std::optional<std::int64_t> parse_integer(const std::string& text) {
    std::int64_t value = 0;
    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

/// value como entero (normalizado en sitio) o el error.
std::optional<std::string> check_integer(const json& schema, json& value, const std::string& name) {
    const std::string must = argument(name) + " must be an integer";
    if (value.is_number_unsigned()) {
        if (value.get<std::uint64_t>() >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return argument(name) + " is too large";
        }
        value = value.get<std::int64_t>();
    } else if (value.is_number_float()) {
        const double number = value.get<double>();
        // 2^63 es exacto en double; lo que queda debajo cabe en int64.
        if (!std::isfinite(number) || std::trunc(number) != number || number < -9.2e18 ||
            number > 9.2e18) {
            return must;
        }
        value = static_cast<std::int64_t>(number);
    } else if (value.is_string()) {
        const std::optional<std::int64_t> number = parse_integer(value.get<std::string>());
        if (!number) {
            return must;
        }
        value = *number;
    } else if (!value.is_number_integer()) {
        return must;
    }
    const auto minimum = schema.find("minimum");
    if (minimum != schema.end() && minimum->is_number_integer() &&
        value.get<std::int64_t>() < minimum->get<std::int64_t>()) {
        return argument(name) + " must be at least " + std::to_string(minimum->get<std::int64_t>());
    }
    return std::nullopt;
}

std::optional<std::string> check(const json& schema, json& value, const std::string& name);

std::optional<std::string> check_object(const json& schema, json& value, const std::string& name) {
    if (!value.is_object()) {
        return name.empty() ? std::string{"Arguments must be a JSON object"}
                            : argument(name) + " must be an object";
    }
    const auto required = schema.find("required");
    if (required != schema.end() && required->is_array()) {
        for (const json& key : *required) {
            if (!key.is_string()) {
                continue;
            }
            const auto found = value.find(key.get<std::string>());
            if (found == value.end() || found->is_null()) {
                return "Missing required argument: " + qualified(name, key.get<std::string>());
            }
        }
    }
    const auto properties = schema.find("properties");
    if (properties == schema.end() || !properties->is_object()) {
        return std::nullopt;
    }
    for (const auto& [key, property] : properties->items()) {
        const auto found = value.find(key);
        if (found == value.end()) {
            continue;
        }
        if (found->is_null()) {
            value.erase(found); // Opcional (las obligatorias ya se revisaron): como si no viniera.
            continue;
        }
        if (std::optional<std::string> error = check(property, *found, qualified(name, key))) {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<std::string> check(const json& schema, json& value, const std::string& name) {
    const std::string_view type = schema_type(schema);
    if (type == "object") {
        if (std::optional<std::string> error = check_object(schema, value, name)) {
            return error;
        }
    } else if (type == "string") {
        if (!value.is_string()) {
            return argument(name) + " must be a string";
        }
    } else if (type == "integer") {
        if (std::optional<std::string> error = check_integer(schema, value, name)) {
            return error;
        }
    } else if (type == "boolean") {
        if (value.is_string() && (value == "true" || value == "false")) {
            value = value == "true";
        }
        if (!value.is_boolean()) {
            return argument(name) + " must be a boolean (true or false)";
        }
    } else if (type == "array") {
        if (!value.is_array()) {
            return argument(name) + " must be an array";
        }
        const auto items = schema.find("items");
        if (items != schema.end()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (std::optional<std::string> error =
                        check(*items, value[i], name + "[" + std::to_string(i) + "]")) {
                    return error;
                }
            }
        }
    }
    const auto allowed = schema.find("enum");
    if (allowed != schema.end() && allowed->is_array() &&
        std::find(allowed->begin(), allowed->end(), value) == allowed->end()) {
        std::string list;
        for (const json& option : *allowed) {
            list += list.empty() ? "" : ", ";
            list += option.is_string() ? option.get<std::string>() : option.dump();
        }
        return argument(name) + " must be one of: " + list;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> validate_arguments(const json& schema, json& arguments) {
    try {
        return check(schema, arguments, {});
    } catch (const std::exception&) {
        return "Arguments could not be validated";
    }
}

} // namespace chatbot::code
