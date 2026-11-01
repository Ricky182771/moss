#include "model_params_form.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace chatbot::cli {
namespace {

/// Los campos numéricos de ModelParams, en el orden de model_param_limits.
std::optional<double>* real_field(ModelParams& params, std::size_t index) {
    switch (index) {
    case 0:
        return &params.temperature;
    case 1:
        return &params.top_p;
    case 4:
        return &params.frequency_penalty;
    case 5:
        return &params.presence_penalty;
    default:
        return nullptr;
    }
}

std::optional<std::int64_t>* integer_field(ModelParams& params, std::size_t index) {
    switch (index) {
    case 2:
        return &params.top_k;
    case 3:
        return &params.max_tokens;
    case 6:
        return &params.seed;
    default:
        return nullptr;
    }
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string number_text(double value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string{buffer.data(), result.ptr};
}

/// Todo el texto es el número (from_chars no acepta "+" ni espacios dentro).
template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::string reasoning_help(std::string_view provider) {
    switch (reasoning_style(provider)) {
    case ReasoningStyle::Toggle:
        return "En NIM, Bajo, Medio y Alto solo lo encienden (enable_thinking); Apagado lo "
               "apaga.";
    case ReasoningStyle::Budget:
        return "Bajo, Medio y Alto dan 1024, 4096 y 16384 tokens para pensar; Apagado no "
               "manda nada.";
    case ReasoningStyle::Effort:
        break;
    }
    if (provider == "openrouter") {
        return "Manda reasoning.effort (Apagado: \"none\").";
    }
    return "Manda reasoning_effort (Apagado: \"none\"). No todos los modelos lo aceptan.";
}

std::string_view param_label(std::string_view field) {
    if (field == "temperature") {
        return "Temperatura";
    }
    if (field == "top_p") {
        return "Top P";
    }
    if (field == "top_k") {
        return "Top K";
    }
    if (field == "max_tokens") {
        return "Máx. tokens";
    }
    if (field == "frequency_penalty") {
        return "Pen. frecuencia";
    }
    if (field == "presence_penalty") {
        return "Pen. presencia";
    }
    if (field == "seed") {
        return "Semilla";
    }
    return field;
}

std::string limit_hint(const ParamLimit& limit) {
    const auto text = [&limit](double value) {
        return limit.integer ? std::to_string(static_cast<long long>(value)) : number_text(value);
    };
    if (limit.min_exclusive) {
        return "más de " + text(limit.min) + ", hasta " + text(limit.max);
    }
    return text(limit.min) + " a " + text(limit.max);
}

ModelParamsForm::ModelParamsForm(const ModelParams& saved) {
    ModelParams copy = saved;
    for (std::size_t i = 0; i < kNumberFields; ++i) {
        if (const std::optional<double>* real = real_field(copy, i); real && real->has_value()) {
            numbers[i] = number_text(**real);
        } else if (const std::optional<std::int64_t>* integer = integer_field(copy, i);
                   integer && integer->has_value()) {
            numbers[i] = std::to_string(**integer);
        }
    }
    reasoning = static_cast<int>(saved.reasoning);
    extra = format_extra(saved.extra);
    mark_saved();
}

ModelParams ModelParamsForm::draft(std::vector<FieldProblem>* problems) const {
    ModelParams params;
    params.reasoning = static_cast<Reasoning>(
        std::clamp(reasoning, 0, static_cast<int>(kReasoningChoices.size()) - 1));
    const std::vector<ParamLimit> limits = model_param_limits("");
    for (std::size_t i = 0; i < kNumberFields; ++i) {
        std::string text{trim(numbers[i])};
        if (text.empty()) {
            continue;
        }
        std::replace(text.begin(), text.end(), ',', '.'); // "0,5" también.
        bool ok = false;
        if (std::optional<double>* real = real_field(params, i)) {
            *real = parse_number<double>(text);
            ok = real->has_value();
        } else if (std::optional<std::int64_t>* integer = integer_field(params, i)) {
            *integer = parse_number<std::int64_t>(text);
            ok = integer->has_value();
        }
        if (!ok && problems != nullptr) {
            problems->push_back({std::string{limits[i].field},
                                 limits[i].integer ? "Escribe un número entero." : "Escribe un número."});
        }
    }
    params.extra = extra;
    return params;
}

bool ModelParamsForm::dirty() const {
    return numbers != saved_numbers_ || reasoning != saved_reasoning_ || extra != saved_extra_;
}

void ModelParamsForm::restore_all() {
    numbers.fill(std::string{});
    reasoning = 0;
    extra.clear();
}

void ModelParamsForm::mark_saved() {
    saved_numbers_ = numbers;
    saved_reasoning_ = reasoning;
    saved_extra_ = extra;
}

} // namespace chatbot::cli
