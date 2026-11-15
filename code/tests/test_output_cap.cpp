// Tope del resultado de una herramienta (output_cap.h): puro, sin red.

#include "output_cap.h"

#include "chatbot/utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <string_view>

using chatbot::code::cap_tool_output;

namespace {

std::size_t chars(const std::string& text) {
    std::size_t count = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Caracteres como los cuenta el tope: un punto de código válido o un byte inválido.
std::size_t code_points(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++count) {
        if (!chatbot::utf8::next_code_point(text, i)) {
            ++i;
        }
    }
    return count;
}

// Lo que la marca y sus dos saltos de línea pueden sumar al tope.
constexpr std::size_t kMarkerRoom = 40;

/// Cabeza y cola de un resultado recortado (lo de antes y lo de después de la marca); nullopt si
/// no hay marca.
struct Parts {
    std::string head;
    std::string tail;
};
std::optional<Parts> split_capped(const std::string& capped) {
    const std::size_t mark = capped.find("[\xE2\x80\xA6 ");
    if (mark == std::string::npos) {
        return std::nullopt;
    }
    const std::string end = " lines omitted \xE2\x80\xA6]\n";
    const std::size_t close = capped.find(end, mark);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    return Parts{capped.substr(0, mark), capped.substr(close + end.size())};
}

/// Lo que cap_tool_output promete para cualquier entrada: el tope (más la marca), cabeza y cola
/// sacadas del texto sin encimarse, y UTF-8 válido si la entrada lo es.
void check_cap(const std::string& text, std::size_t cap) {
    const std::string capped = cap_tool_output(text, cap);
    CHECK(code_points(capped) <= cap + kMarkerRoom);
    if (code_points(text) <= cap) {
        CHECK(capped == text);
        return;
    }
    const std::optional<Parts> parts = split_capped(capped);
    REQUIRE(parts.has_value());
    // La cabeza puede llevar un '\n' de más (cuando el texto no tenía saltos donde cortó).
    std::string head = parts->head;
    if (!text.starts_with(head) && head.ends_with('\n')) {
        head.pop_back();
    }
    CHECK(text.starts_with(head));
    CHECK(text.ends_with(parts->tail));
    CHECK(head.size() + parts->tail.size() <= text.size());
    if (chatbot::utf8::is_valid(text)) {
        CHECK(chatbot::utf8::is_valid(capped));
    }
}

std::string numbered_lines(int count) {
    std::string text;
    for (int i = 1; i <= count; ++i) {
        text += "linea " + std::to_string(i) + "\n";
    }
    return text;
}

} // namespace

TEST_CASE("cap_tool_output: un texto corto queda igual", "[code][tope]") {
    CHECK(cap_tool_output("hola\nmundo") == "hola\nmundo");
    CHECK(cap_tool_output("") == "");
}

TEST_CASE("cap_tool_output: justo en el tope no se toca", "[code][tope]") {
    const std::string exact(100, 'a');
    CHECK(cap_tool_output(exact, 100) == exact);
    // Cuenta caracteres, no bytes: 100 "é" son 200 bytes.
    std::string accents;
    for (int i = 0; i < 100; ++i) {
        accents += "é";
    }
    CHECK(cap_tool_output(accents, 100) == accents);
    CHECK(cap_tool_output(exact + "a", 100) != exact + "a");
}

TEST_CASE("cap_tool_output: muchas líneas dejan cabeza, marca y cola", "[code][tope]") {
    const std::string text = numbered_lines(1000);
    const std::string capped = cap_tool_output(text, 200);

    CHECK(starts_with(capped, "linea 1\nlinea 2\n"));
    CHECK(ends_with(capped, "linea 999\nlinea 1000\n"));
    CHECK(chars(capped) <= 200 + kMarkerRoom);

    const std::size_t mark = capped.find("[… ");
    REQUIRE(mark != std::string::npos);
    const std::size_t end = capped.find(" lines omitted …]\n", mark);
    REQUIRE(end != std::string::npos);
    const std::size_t omitted = std::stoul(capped.substr(mark + std::string{"[… "}.size()));

    // Cabeza y cola son líneas completas: lo que falta es exactamente N líneas.
    const std::string head = capped.substr(0, mark);
    const std::string tail = capped.substr(end + std::string{" lines omitted …]\n"}.size());
    const auto count_lines = [](const std::string& part) {
        std::size_t n = 0;
        for (const char c : part) {
            n += c == '\n' ? 1U : 0U;
        }
        return n;
    };
    CHECK(count_lines(head) + omitted + count_lines(tail) == 1000);
    CHECK(omitted > 900);
    // La cola sigue en orden justo después de lo quitado.
    CHECK(starts_with(text, head));
    CHECK(ends_with(text, tail));
}

TEST_CASE("cap_tool_output: una sola línea enorme se recorta por caracteres", "[code][tope]") {
    std::string text(5000, 'x');
    text.replace(0, 4, "HEAD");
    text.replace(text.size() - 4, 4, "TAIL");
    const std::string capped = cap_tool_output(text, 100);

    CHECK(starts_with(capped, "HEAD"));
    CHECK(ends_with(capped, "TAIL"));
    CHECK(capped.find("[… 1 lines omitted …]") != std::string::npos);
    CHECK(chars(capped) <= 100 + kMarkerRoom);
    CHECK(capped.size() < 200);
}

TEST_CASE("cap_tool_output: el corte nunca parte un carácter UTF-8", "[code][tope]") {
    // Emoji (4 bytes) y acentos (2 bytes) por todo el texto, en una línea.
    std::string text;
    for (int i = 0; i < 400; ++i) {
        text += "á😀é";
    }
    for (const std::size_t cap : {7U, 10U, 11U, 33U, 100U}) {
        const std::string capped = cap_tool_output(text, cap);
        CHECK(chatbot::utf8::is_valid(capped));
        CHECK(chars(capped) <= cap + kMarkerRoom);
    }
    const std::string capped = cap_tool_output(text, 10);
    CHECK(starts_with(capped, "á😀éá😀"));
    CHECK(ends_with(capped, "é"));
}

TEST_CASE("cap_tool_output: con líneas largas y UTF-8 sigue siendo válido y acotado", "[code][tope]") {
    std::string text;
    for (int i = 0; i < 300; ++i) {
        text += "ñandú 🐧 línea número " + std::to_string(i) + "\n";
    }
    const std::string capped = cap_tool_output(text, 250);
    CHECK(chatbot::utf8::is_valid(capped));
    CHECK(chars(capped) <= 250 + kMarkerRoom);
    CHECK(starts_with(capped, "ñandú 🐧 línea número 0\n"));
    CHECK(ends_with(capped, "línea número 299\n"));
}

TEST_CASE("cap_tool_output: el tope por defecto es de 30000 caracteres", "[code][tope]") {
    const std::string at_cap(30000, 'a');
    CHECK(cap_tool_output(at_cap) == at_cap);
    CHECK(chars(cap_tool_output(at_cap + "a")) <= 30000 + kMarkerRoom);
}

TEST_CASE("cap_tool_output: 2000 líneas de 2000 bytes de continuación sueltos", "[code][tope]") {
    // Cada 0x80 es un byte inválido: un carácter. Antes la cola retrocedía cada racha entera como
    // si fuera uno y salía casi todo el texto.
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text += std::string(2000, '\x80') + "\n";
    }
    const std::string capped = cap_tool_output(text);
    CHECK(code_points(capped) <= chatbot::code::kDefaultToolOutputCap + kMarkerRoom);
    CHECK(capped.size() <= chatbot::code::kDefaultToolOutputCap + kMarkerRoom);
    check_cap(text, chatbot::code::kDefaultToolOutputCap);
}

TEST_CASE("cap_tool_output: rachas de continuación sueltas", "[code][tope]") {
    // Rachas de 1 a 6 bytes de continuación, solas o detrás de un inicio de secuencia (válida o
    // no), con y sin saltos de línea; topes que caen en todas partes.
    for (const std::string_view lead : {"", "a", "\xC3", "\xE2", "\xF0", "\xE2\x82"}) {
        for (std::size_t run = 1; run <= 6; ++run) {
            for (const std::string_view separator : {"", "\n"}) {
                std::string text;
                for (int i = 0; i < 30; ++i) {
                    text += lead;
                    text += std::string(run, '\xAC');
                    text += 'x';
                    text += separator;
                }
                for (std::size_t cap = 1; cap < 40; ++cap) {
                    INFO("lead " << lead.size() << ", run " << run << ", cap " << cap);
                    check_cap(text, cap);
                }
            }
        }
    }
}

TEST_CASE("cap_tool_output: entradas de bytes al azar respetan el tope", "[code][tope]") {
    std::mt19937 random{20261115U}; // Semilla fija: siempre las mismas entradas.
    const auto pick = [&random](std::size_t below) {
        return std::uniform_int_distribution<std::size_t>{0, below - 1}(random);
    };
    // Bytes con más peso para lo que importa: continuaciones, inicios de secuencia y saltos.
    const std::string bytes_pool = std::string{"\n\n\naaa\x80\x8F\xBF\xC3\xE2\xF0\xF4\xFF"} + '\0';
    for (int round = 0; round < 300; ++round) {
        std::string text;
        const std::size_t size = pick(400);
        for (std::size_t i = 0; i < size; ++i) {
            text += pick(4) == 0 ? static_cast<char>(pick(256)) : bytes_pool[pick(bytes_pool.size())];
        }
        const std::size_t cap = 1 + pick(120);
        INFO("ronda " << round << ", cap " << cap);
        check_cap(text, cap);
    }
    // UTF-8 válido al azar: el resultado también es válido.
    const std::string_view pieces[] = {"a", "\n", "ñ", "€", "😀", "línea\n", " "};
    for (int round = 0; round < 300; ++round) {
        std::string text;
        const std::size_t size = pick(300);
        for (std::size_t i = 0; i < size; ++i) {
            text += pieces[pick(std::size(pieces))];
        }
        const std::size_t cap = 1 + pick(120);
        INFO("válido, ronda " << round << ", cap " << cap);
        REQUIRE(chatbot::utf8::is_valid(text));
        check_cap(text, cap);
    }
}

TEST_CASE("shown_line: U+FFFD por cada byte inválido, sin \\r final y recortada", "[code][tope]") {
    using chatbot::code::shown_line;
    CHECK(shown_line("a\xFF" "b\xC3\r", 10) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD");
    CHECK(shown_line("ñandú", 10) == "ñandú");
    CHECK(shown_line("abcdef", 3) == "abc [\xE2\x80\xA6 line truncated]");
    CHECK(shown_line("abc", 3) == "abc");
    CHECK(chatbot::utf8::is_valid(shown_line(std::string(50, '\x80'), 10)));
}
