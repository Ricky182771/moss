// Tope del resultado de una herramienta (output_cap.h): puro, sin red.

#include "chatbot/code/output_cap.h"

#include "chatbot/utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

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

// Lo que la marca y sus dos saltos de línea pueden sumar al tope.
constexpr std::size_t kMarkerRoom = 40;

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
