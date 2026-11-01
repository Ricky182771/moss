#include "chatbot/model_params.h"

#include "chatbot/error.h"
#include "chatbot/utf8.h"
#include "model_params_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot {
namespace {

using ordered = nlohmann::ordered_json;

/// Llaves que pone Moss: el JSON avanzado no las puede tocar.
constexpr std::array<std::string_view, 6> kReservedKeys{
    "model", "messages", "stream", "stream_options", "tools", "tool_choice"};

/// Margen de salida que se suma al presupuesto de Anthropic cuando "Máx.
/// tokens" está en Predeterminado (max_tokens debe pasar de budget_tokens).
constexpr std::int64_t kAnthropicOutputMargin = 4096;

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// El extra vacío, solo espacios o "{}" no cambia nada.
bool extra_is_empty(std::string_view text) {
    const std::string_view trimmed = trim(text);
    if (trimmed.empty()) {
        return true;
    }
    const ordered value = ordered::parse(trimmed, nullptr, false);
    return !value.is_discarded() && value.is_object() && value.empty();
}

/// 0, 2, -2, 0.5, 1000000: sin ceros de más.
std::string format_number(double value) {
    if (std::floor(value) == value && std::abs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value));
    }
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string{buffer.data(), result.ptr};
}

std::optional<double> numeric_value(const ModelParams& params, std::string_view field) {
    const auto as_double = [](const std::optional<std::int64_t>& value) {
        return value.has_value() ? std::optional<double>{static_cast<double>(*value)}
                                 : std::optional<double>{};
    };
    if (field == "temperature") {
        return params.temperature;
    }
    if (field == "top_p") {
        return params.top_p;
    }
    if (field == "top_k") {
        return as_double(params.top_k);
    }
    if (field == "max_tokens") {
        return as_double(params.max_tokens);
    }
    if (field == "frequency_penalty") {
        return params.frequency_penalty;
    }
    if (field == "presence_penalty") {
        return params.presence_penalty;
    }
    if (field == "seed") {
        return as_double(params.seed);
    }
    return std::nullopt;
}

std::string limit_message(const ParamLimit& limit) {
    if (limit.min_exclusive) {
        return "Debe ser mayor que " + format_number(limit.min) + " y como máximo " +
               format_number(limit.max) + ".";
    }
    return std::string{limit.integer ? "Debe ser un número entero entre " : "Debe ser un número entre "} +
           format_number(limit.min) + " y " + format_number(limit.max) + ".";
}

bool within(const ParamLimit& limit, double value) {
    if (!std::isfinite(value) || value > limit.max) {
        return false;
    }
    return limit.min_exclusive ? value > limit.min : value >= limit.min;
}

/// Línea y columna (en puntos de código, desde 1) del byte offset de text.
std::pair<std::size_t, std::size_t> line_and_column(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < offset; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\n') {
            ++line;
            column = 1;
        } else if ((byte & 0xC0U) != 0x80U) {
            ++column;
        }
    }
    return {line, column};
}

std::optional<std::string> extra_problem(std::string_view text) {
    if (trim(text).empty()) {
        return std::nullopt;
    }
    if (text.size() > kMaxExtraBytes) {
        return "El JSON avanzado pasa de 8 KiB.";
    }
    if (!utf8::is_valid(text)) {
        return "El JSON avanzado no es UTF-8 válido.";
    }
    ordered value;
    try {
        value = ordered::parse(text);
    } catch (const ordered::parse_error& error) {
        // byte: posición (desde 1) del carácter donde falló.
        const auto [line, column] = line_and_column(text, error.byte > 0 ? error.byte - 1 : 0);
        return "El JSON avanzado no es válido (línea " + std::to_string(line) + ", columna " +
               std::to_string(column) + ").";
    } catch (const ordered::out_of_range&) {
        return "El JSON avanzado tiene un número demasiado grande."; // 1e999
    } catch (const std::exception&) {
        return "El JSON avanzado no es válido.";
    }
    if (!value.is_object()) {
        return std::string{"El JSON avanzado tiene que ser un objeto: {…}."};
    }
    for (const std::string_view key : kReservedKeys) {
        if (value.contains(std::string{key})) {
            return "«" + std::string{key} +
                   "» la pone Moss; no se puede cambiar desde el JSON avanzado.";
        }
    }
    return std::nullopt;
}

std::string_view reasoning_label(Reasoning reasoning) {
    switch (reasoning) {
    case Reasoning::Low:
        return "bajo";
    case Reasoning::Medium:
        return "medio";
    case Reasoning::High:
        return "alto";
    case Reasoning::Default:
    case Reasoning::Off:
        break;
    }
    return "";
}

ordered reasoning_object(std::string_view provider, Reasoning reasoning) {
    if (reasoning == Reasoning::Default) {
        return ordered::object();
    }
    const bool off = reasoning == Reasoning::Off;
    // "none" apaga el razonamiento en reasoning_effort y reasoning.effort.
    const std::string effort = off ? "none" : std::string{reasoning_name(reasoning)};
    if (provider == "openrouter") {
        return {{"reasoning", {{"effort", effort}}}};
    }
    if (provider == "nvidia") {
        // NIM: los tres niveles solo lo encienden.
        return {{"chat_template_kwargs", {{"enable_thinking", !off}}}};
    }
    if (provider == "anthropic") {
        if (off) {
            return ordered::object(); // Sin "thinking", Anthropic no razona.
        }
        return {{"thinking", {{"type", "enabled"}, {"budget_tokens", *anthropic_budget(reasoning)}}}};
    }
    // openai, gemini, los demás de la tabla y los endpoints custom:<id>.
    return {{"reasoning_effort", effort}};
}

} // namespace

bool ModelParams::empty() const {
    return reasoning == Reasoning::Default && !temperature && !top_p && !top_k && !max_tokens &&
           !frequency_penalty && !presence_penalty && !seed && extra_is_empty(extra);
}

ReasoningStyle reasoning_style(std::string_view provider) {
    if (provider == "nvidia") {
        return ReasoningStyle::Toggle;
    }
    if (provider == "anthropic") {
        return ReasoningStyle::Budget;
    }
    return ReasoningStyle::Effort;
}

std::string_view reasoning_style_name(ReasoningStyle style) {
    switch (style) {
    case ReasoningStyle::Toggle:
        return "toggle";
    case ReasoningStyle::Budget:
        return "budget";
    case ReasoningStyle::Effort:
        break;
    }
    return "effort";
}

std::string_view reasoning_name(Reasoning reasoning) {
    switch (reasoning) {
    case Reasoning::Off:
        return "off";
    case Reasoning::Low:
        return "low";
    case Reasoning::Medium:
        return "medium";
    case Reasoning::High:
        return "high";
    case Reasoning::Default:
        break;
    }
    return "";
}

std::optional<Reasoning> parse_reasoning(std::string_view name) {
    for (const Reasoning reasoning :
         {Reasoning::Off, Reasoning::Low, Reasoning::Medium, Reasoning::High}) {
        if (name == reasoning_name(reasoning)) {
            return reasoning;
        }
    }
    return std::nullopt;
}

std::string reasoning_fields(std::string_view provider, Reasoning reasoning) {
    return reasoning_object(provider, reasoning).dump();
}

std::optional<std::int64_t> anthropic_budget(Reasoning reasoning) {
    switch (reasoning) {
    case Reasoning::Low:
        return 1024;
    case Reasoning::Medium:
        return 4096;
    case Reasoning::High:
        return 16384;
    case Reasoning::Default:
    case Reasoning::Off:
        break;
    }
    return std::nullopt;
}

std::vector<ParamLimit> model_param_limits(std::string_view provider) {
    const double max_temperature = provider == "anthropic" ? 1 : 2;
    return {
        {"temperature", 0, max_temperature, false, false},
        {"top_p", 0, 1, true, false},
        {"top_k", 1, 1000, false, true},
        {"max_tokens", 1, 1000000, false, true},
        {"frequency_penalty", -2, 2, false, false},
        {"presence_penalty", -2, 2, false, false},
        {"seed", 0, 2147483647, false, true},
    };
}

std::vector<FieldProblem> validate_model_params(const ModelParams& params,
                                                std::string_view provider) {
    std::vector<FieldProblem> problems;
    for (const ParamLimit& limit : model_param_limits(provider)) {
        const std::optional<double> value = numeric_value(params, limit.field);
        if (value.has_value() && !within(limit, *value)) {
            problems.push_back({std::string{limit.field}, limit_message(limit)});
        }
    }
    if (reasoning_style(provider) == ReasoningStyle::Budget) {
        const std::optional<std::int64_t> budget = anthropic_budget(params.reasoning);
        const bool max_ok = std::none_of(problems.begin(), problems.end(),
                                         [](const FieldProblem& p) { return p.field == "max_tokens"; });
        if (budget.has_value() && params.max_tokens.has_value() && max_ok &&
            *params.max_tokens <= *budget) {
            problems.push_back({"max_tokens", "Con el razonamiento " +
                                                  std::string{reasoning_label(params.reasoning)} +
                                                  ", el máximo de salida debe pasar de " +
                                                  std::to_string(*budget) + " tokens."});
        }
    }
    if (std::optional<std::string> problem = extra_problem(params.extra)) {
        problems.push_back({"extra", std::move(*problem)});
    }
    return problems;
}

ParsedModelParams parse_params_object(const ordered& object) {
    ParsedModelParams parsed;
    ModelParams& params = parsed.params;
    const auto real = [&parsed](const std::string& key, const ordered& value,
                                std::optional<double>& out) {
        if (value.is_number()) {
            out = value.get<double>();
        } else if (!value.is_null()) {
            parsed.invalid_fields.push_back(key);
        }
    };
    const auto integer = [&parsed](const std::string& key, const ordered& value,
                                   std::optional<std::int64_t>& out) {
        const bool fits = value.is_number_integer() &&
                          (!value.is_number_unsigned() ||
                           value.get<std::uint64_t>() <=
                               static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
        if (fits) {
            out = value.get<std::int64_t>();
        } else if (!value.is_null()) {
            parsed.invalid_fields.push_back(key);
        }
    };
    for (const auto& [key, value] : object.items()) {
        if (key == "reasoning") {
            const std::optional<Reasoning> reasoning =
                value.is_string() ? parse_reasoning(value.get<std::string>()) : std::nullopt;
            if (reasoning.has_value()) {
                params.reasoning = *reasoning;
            } else if (!value.is_null()) {
                parsed.invalid_fields.push_back(key);
            }
        } else if (key == "temperature") {
            real(key, value, params.temperature);
        } else if (key == "top_p") {
            real(key, value, params.top_p);
        } else if (key == "top_k") {
            integer(key, value, params.top_k);
        } else if (key == "max_tokens") {
            integer(key, value, params.max_tokens);
        } else if (key == "frequency_penalty") {
            real(key, value, params.frequency_penalty);
        } else if (key == "presence_penalty") {
            real(key, value, params.presence_penalty);
        } else if (key == "seed") {
            integer(key, value, params.seed);
        } else if (key == "extra") {
            if (value.is_object()) {
                params.extra = value.empty() ? std::string{}
                                             : value.dump(-1, ' ', false,
                                                          ordered::error_handler_t::replace);
            } else if (!value.is_null()) {
                parsed.invalid_fields.push_back(key);
            }
        } else {
            parsed.unknown_fields.push_back(key);
        }
    }
    return parsed;
}

ordered params_object(const ModelParams& params) {
    ordered object = ordered::object();
    if (params.reasoning != Reasoning::Default) {
        object["reasoning"] = std::string{reasoning_name(params.reasoning)};
    }
    const auto put = [&object](const char* key, const auto& value) {
        if (value.has_value()) {
            object[key] = *value;
        }
    };
    put("temperature", params.temperature);
    put("top_p", params.top_p);
    put("top_k", params.top_k);
    put("max_tokens", params.max_tokens);
    put("frequency_penalty", params.frequency_penalty);
    put("presence_penalty", params.presence_penalty);
    put("seed", params.seed);
    if (!extra_is_empty(params.extra)) {
        const ordered extra = ordered::parse(params.extra, nullptr, false);
        if (extra.is_object()) {
            object["extra"] = extra;
        }
    }
    return object;
}

std::string model_params_to_json(const ModelParams& params) {
    return params_object(params).dump(-1, ' ', false, ordered::error_handler_t::replace);
}

std::string format_extra(std::string_view extra) {
    if (extra_is_empty(extra)) {
        return {};
    }
    const ordered value = ordered::parse(extra, nullptr, false);
    if (value.is_discarded()) {
        return std::string{extra};
    }
    return value.dump(2, ' ', false, ordered::error_handler_t::replace);
}

Result<ParsedModelParams> model_params_from_json(std::string_view json) {
    const ordered value = ordered::parse(json, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        return ChatError{ErrorKind::Config, 0, "Los ajustes del modelo tienen que ser un objeto JSON.",
                         std::nullopt};
    }
    return parse_params_object(value);
}

ordered request_json(std::string_view model, bool stream, ordered messages,
                     std::string_view provider, const ModelParams& params) {
    ordered body = ordered::object();
    body["model"] = std::string{model};
    body["stream"] = stream;
    body["messages"] = std::move(messages);
    const auto put = [&body](const char* key, const auto& value) {
        if (value.has_value()) {
            body[key] = *value;
        }
    };
    put("temperature", params.temperature);
    put("top_p", params.top_p);
    put("top_k", params.top_k);
    std::optional<std::int64_t> max_tokens = params.max_tokens;
    if (!max_tokens.has_value() && reasoning_style(provider) == ReasoningStyle::Budget) {
        if (const std::optional<std::int64_t> budget = anthropic_budget(params.reasoning)) {
            max_tokens = *budget + kAnthropicOutputMargin;
        }
    }
    put(provider == "openai" ? "max_completion_tokens" : "max_tokens", max_tokens);
    put("frequency_penalty", params.frequency_penalty);
    put("presence_penalty", params.presence_penalty);
    put("seed", params.seed);
    const ordered reasoning = reasoning_object(provider, params.reasoning);
    for (const auto& [key, value] : reasoning.items()) {
        body[key] = value;
    }
    if (!extra_is_empty(params.extra)) {
        // Ya validado al cargar o al guardar; si no fuera un objeto, se ignora.
        const ordered patch = ordered::parse(params.extra, nullptr, false);
        if (patch.is_object()) {
            body.merge_patch(patch);
        }
    }
    return body;
}

std::vector<std::string> request_param_keys(std::string_view provider,
                                            const ModelParams& params) {
    std::vector<std::string> keys;
    const ordered body = request_json("", false, ordered::array(), provider, params);
    for (const auto& item : body.items()) {
        if (item.key() != "model" && item.key() != "stream" && item.key() != "messages") {
            keys.push_back(item.key());
        }
    }
    return keys;
}

std::string request_preview(std::string_view model, std::string_view provider,
                            const ModelParams& params, std::size_t message_count) {
    const std::string messages = "[…" + std::to_string(message_count) +
                                 (message_count == 1 ? " mensaje…]" : " mensajes…]");
    // stream: true, como lo manda la app (complete_stream).
    return request_json(model, true, messages, provider, params)
        .dump(2, ' ', false, ordered::error_handler_t::replace);
}

} // namespace chatbot
