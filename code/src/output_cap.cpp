#include "output_cap.h"

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

// Inicio del carácter que termina en end (end > 0), contado igual que advance: una secuencia
// válida que acaba justo en end (como mucho 3 bytes de continuación), o si no, un byte.
std::size_t retreat(std::string_view text, std::size_t end) {
    std::size_t start = end - 1;
    for (int back = 0; back < 3 && start > 0 && is_continuation(text[start]); ++back) {
        --start;
    }
    if (start + 1 < end && advance(text, start) == end) {
        return start;
    }
    return end - 1;
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
    // siguiente línea completa si queda algo después. Se cuenta igual que la
    // cabeza: como el texto tiene más de max_chars, la cola empieza donde
    // termina la cabeza o después.
    std::size_t tail_start = text.size();
    for (std::size_t i = 0; i < tail_chars && tail_start > 0; ++i) {
        tail_start = retreat(text, tail_start);
    }
    tail_start = std::max(tail_start, head_end);
    if (tail_start > 0 && text[tail_start - 1] != '\n') {
        const std::size_t newline = text.find('\n', tail_start);
        if (newline != std::string_view::npos && newline + 1 < text.size()) {
            tail_start = newline + 1;
        }
    }

    // Líneas que toca lo quitado, [head_end, tail_start): sus saltos, sin el
    // último si cierra la zona, más una.
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

std::string shown_line(std::string_view line, std::size_t max_chars) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < line.size(); ++chars) {
        if (chars == max_chars) {
            out += " [\xE2\x80\xA6 line truncated]";
            break;
        }
        const std::size_t start = i;
        if (utf8::next_code_point(line, i)) {
            out += line.substr(start, i - start);
        } else {
            out += utf8::kReplacement;
            ++i;
        }
    }
    return out;
}

} // namespace chatbot::code
