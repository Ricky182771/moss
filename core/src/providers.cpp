#include "chatbot/providers.h"

#include "chatbot/utf8.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace chatbot {

namespace {

// "/models": confirmado en la documentación de nvidia, openai, gemini (con
// prefijo "models/"), openrouter, mistral, deepseek, together y ollama; no
// documentado en anthropic ni confirmado en groq, xai y cerebras. La app lo
// intenta igual y, si falla, deja escribir el modelo a mano.
constexpr std::array kProviders{
    ProviderInfo{"nvidia", "NVIDIA NIM", "https://integrate.api.nvidia.com/v1", true},
    ProviderInfo{"openai", "OpenAI", "https://api.openai.com/v1", true},
    ProviderInfo{"gemini", "Google Gemini",
                 "https://generativelanguage.googleapis.com/v1beta/openai", true},
    ProviderInfo{"anthropic", "Anthropic (Claude)", "https://api.anthropic.com/v1", true},
    ProviderInfo{"groq", "Groq", "https://api.groq.com/openai/v1", true},
    ProviderInfo{"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", true},
    ProviderInfo{"mistral", "Mistral", "https://api.mistral.ai/v1", true},
    ProviderInfo{"deepseek", "DeepSeek", "https://api.deepseek.com", true},
    ProviderInfo{"together", "Together AI", "https://api.together.ai/v1", true},
    ProviderInfo{"xai", "xAI (Grok)", "https://api.x.ai/v1", true},
    ProviderInfo{"cerebras", "Cerebras", "https://api.cerebras.ai/v1", true},
    ProviderInfo{"ollama", "Ollama (local)", "http://localhost:11434/v1", false},
};

/// Largo máximo del id de un endpoint.
constexpr std::size_t kMaxEndpointId = 32;

/// Minúscula de cp en ASCII y Latin-1 (U+00C0–U+00DE, salvo ×).
std::uint32_t fold(std::uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) {
        return cp + 0x20;
    }
    return cp;
}

/// Letra o dígito ASCII para el id (ya en minúscula), o '-'.
char id_char(std::uint32_t cp) {
    // U+00E0–U+00FF sin acento; lo que no es una letra con acento, '-'.
    static constexpr std::string_view kLatin1 = "aaaaaa-ceeeeiiiidnooooo-ouuuuy-y";
    cp = fold(cp);
    if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
        return static_cast<char>(cp);
    }
    if (cp >= 0xE0 && cp <= 0xFF) {
        return kLatin1[cp - 0xE0];
    }
    return '-';
}

bool taken(std::string_view id, std::span<const Endpoint> existing) {
    return !valid_endpoint_id(id) ||
           std::any_of(existing.begin(), existing.end(),
                       [id](const Endpoint& endpoint) { return endpoint.id == id; });
}

} // namespace

std::span<const ProviderInfo> providers() { return kProviders; }

std::optional<std::size_t> provider_index(std::string_view id) {
    for (std::size_t i = 0; i < kProviders.size(); ++i) {
        if (kProviders[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::string endpoint_provider(std::string_view id) {
    return std::string{kEndpointPrefix} + std::string{id};
}

std::optional<std::string_view> endpoint_id(std::string_view provider) {
    if (!provider.starts_with(kEndpointPrefix)) {
        return std::nullopt;
    }
    return provider.substr(kEndpointPrefix.size());
}

bool valid_endpoint_id(std::string_view id) {
    const auto allowed = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (id.empty() || id.size() > kMaxEndpointId || !allowed(id.front()) || id == "custom" ||
        provider_index(id).has_value()) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [&](char c) { return allowed(c) || c == '-'; });
}

std::optional<std::string> endpoint_name_problem(std::string_view name) {
    if (name.empty()) {
        return "Escribe un nombre para el endpoint.";
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < name.size(); ++count) {
        const std::optional<std::uint32_t> cp = utf8::next_code_point(name, i);
        if (!cp.has_value() || *cp < 0x20 || (*cp >= 0x7F && *cp <= 0x9F)) {
            return "El nombre del endpoint tiene caracteres no válidos.";
        }
    }
    if (count > kMaxEndpointName) {
        return "El nombre del endpoint puede tener hasta " + std::to_string(kMaxEndpointName) +
               " caracteres.";
    }
    return std::nullopt;
}

bool same_endpoint_name(std::string_view a, std::string_view b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const std::optional<std::uint32_t> x = utf8::next_code_point(a, i);
        const std::optional<std::uint32_t> y = utf8::next_code_point(b, j);
        if (!x.has_value() || !y.has_value()) {
            return a == b; // UTF-8 inválido: solo iguales byte a byte.
        }
        if (fold(*x) != fold(*y)) {
            return false;
        }
    }
    return i == a.size() && j == b.size();
}

std::string make_endpoint_id(std::string_view name, std::span<const Endpoint> existing) {
    std::string base;
    for (std::size_t i = 0; i < name.size();) {
        const std::optional<std::uint32_t> cp = utf8::next_code_point(name, i);
        if (!cp.has_value()) {
            ++i; // Byte inválido: cuenta como separador.
        }
        const char c = cp.has_value() ? id_char(*cp) : '-';
        if (c != '-' || (!base.empty() && base.back() != '-')) {
            base.push_back(c);
        }
    }
    const auto trim = [](std::string& text) {
        while (!text.empty() && text.back() == '-') {
            text.pop_back();
        }
    };
    trim(base);
    if (base.size() > kMaxEndpointId) {
        base.resize(kMaxEndpointId);
        trim(base);
    }
    if (base.empty()) {
        base = "endpoint";
    }
    if (!taken(base, existing)) {
        return base;
    }
    for (std::size_t n = 2;; ++n) {
        const std::string suffix = "-" + std::to_string(n);
        std::string candidate = base.substr(0, kMaxEndpointId - suffix.size());
        trim(candidate);
        candidate += suffix;
        if (!taken(candidate, existing)) {
            return candidate;
        }
    }
}

} // namespace chatbot
