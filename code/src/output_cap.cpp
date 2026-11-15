#include "chatbot/code/output_cap.h"

#include "chatbot/utf8.h"

#include <algorithm>

namespace chatbot::code {

namespace {

// Un carácter por punto de código; un byte inválido cuenta como uno.
std::size_t advance(std::string_view text, std::size_t i) {
    std::size_t next = i;
    if (!utf8::next_code_point(text, next)) {
        return i + 1;
    }
    return next;
}

std::size_t count_chars(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); i = advance(text, i)) {
        ++count;
    }
    return count;
}

bool is_continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

} // namespace

std::string cap_tool_output(std::string_view text, std::size_t max_chars) {
    if (count_chars(text) <= max_chars) {
        return std::string{text};
    }
    const std::size_t head_chars = max_chars / 2;
    const std::size_t tail_chars = max_chars - head_chars;

    // Cabeza: head_chars caracteres, retrocediendo a la última línea completa.
    std::size_t head_end = 0;
    for (std::size_t i = 0; i < head_chars; ++i) {
        head_end = advance(text, head_end);
    }
    if (head_end > 0) {
        const std::size_t newline = text.rfind('\n', head_end - 1);
        if (newline != std::string_view::npos) {
            head_end = newline + 1;
        }
    }

    // Cola: tail_chars caracteres contados desde el final, avanzando a la
    // siguiente línea completa si queda algo después.
    std::size_t tail_start = text.size();
    for (std::size_t i = 0; i < tail_chars && tail_start > 0; ++i) {
        --tail_start;
        while (tail_start > 0 && is_continuation(text[tail_start])) {
            --tail_start;
        }
    }
    if (tail_start > 0 && text[tail_start - 1] != '\n') {
        const std::size_t newline = text.find('\n', tail_start);
        if (newline != std::string_view::npos && newline + 1 < text.size()) {
            tail_start = newline + 1;
        }
    }

    // Líneas que toca lo quitado, [head_end, tail_start): sus saltos, sin el
    // último si cierra la zona, más una. Cabeza y cola no se tocan: suman
    // menos de max_chars y el texto tiene más.
    const std::string_view omitted = text.substr(head_end, tail_start - head_end);
    const std::size_t inner = omitted.empty() ? 0 : omitted.size() - 1;
    const auto lines = static_cast<std::size_t>(
        std::count(omitted.begin(), omitted.begin() + static_cast<std::ptrdiff_t>(inner), '\n')) + 1;

    std::string result{text.substr(0, head_end)};
    if (!result.empty() && result.back() != '\n') {
        result += '\n';
    }
    result += "[… " + std::to_string(lines) + " lines omitted …]\n";
    result += text.substr(tail_start);
    return result;
}

} // namespace chatbot::code
