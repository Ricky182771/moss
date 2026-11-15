// La herramienta read (guias/18-moss-code.md, sección 4), por el registro.

#include "tool_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

using tool_test::ToolFixture;
using tool_test::ToolResult;
using tool_test::ToolStatus;

namespace {

/// count líneas "linea <n>", cada una con su salto.
std::string numbered_lines(int count) {
    std::string text;
    for (int i = 1; i <= count; ++i) {
        text += "linea " + std::to_string(i) + "\n";
    }
    return text;
}

} // namespace

TEST_CASE("read numera como cat -n y anota la lectura", "[read]") {
    ToolFixture fixture;
    fixture.file("f.txt", "uno\ndos\ntres\n");
    const ToolResult result = fixture.run("read", R"({"path": "f.txt"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(result.text == "     1\tuno\n     2\tdos\n     3\ttres");
    CHECK(result.display == "3 líneas");
    REQUIRE(fixture.was_read("f.txt"));
    CHECK(fixture.state.reads.begin()->second.size == 13);
    CHECK(fixture.state.reads.begin()->second.modified != 0);
}

TEST_CASE("read: última línea sin salto, una sola línea", "[read]") {
    ToolFixture fixture;
    fixture.file("f.txt", "solo");
    const ToolResult result = fixture.run("read", R"({"path": "f.txt"})");
    CHECK(result.text == "     1\tsolo");
    CHECK(result.display == "1 línea");
}

TEST_CASE("read con offset y limit dice el rango y cómo seguir", "[read]") {
    ToolFixture fixture;
    fixture.file("f.txt", numbered_lines(10));
    const ToolResult middle = fixture.run("read", R"({"path": "f.txt", "offset": 3, "limit": "2"})");
    CHECK(middle.status == ToolStatus::Ok);
    CHECK(middle.text == "     3\tlinea 3\n     4\tlinea 4\n\n"
                         "(Showing lines 3-4 of 10. Use offset=5 to continue.)");
    CHECK(middle.display == "2 líneas (3–4 de 10)");

    const ToolResult tail = fixture.run("read", R"({"path": "f.txt", "offset": 9})");
    CHECK(tail.text.ends_with("    10\tlinea 10\n\n"
                              "(Showing lines 9-10 of 10. Use offset to read the earlier lines.)"));
    CHECK(tail.display == "2 líneas (9–10 de 10)");
}

TEST_CASE("read muestra 2000 líneas por defecto", "[read]") {
    ToolFixture fixture;
    fixture.file("f.txt", numbered_lines(2500));
    const ToolResult result = fixture.run("read", R"({"path": "f.txt"})");
    CHECK(result.text.find("  2000\tlinea 2000\n") != std::string::npos);
    CHECK(result.text.find("2001\t") == std::string::npos);
    CHECK(result.text.ends_with("(Showing lines 1-2000 of 2500. Use offset=2001 to continue.)"));
    CHECK(result.display == "2000 líneas (1–2000 de 2500)");
}

TEST_CASE("read recorta las líneas de más de 2000 caracteres sin partir UTF-8", "[read]") {
    ToolFixture fixture;
    std::string accents;
    for (int i = 0; i < 2001; ++i) {
        accents += "é";
    }
    fixture.file("f.txt", std::string(2500, 'a') + "\n" + accents + "\ncorta\n");
    const ToolResult result = fixture.run("read", R"({"path": "f.txt"})");
    CHECK(result.text.find("     1\t" + std::string(2000, 'a') + " [… line truncated]\n") !=
          std::string::npos);
    CHECK(result.text.find(std::string(2001, 'a')) == std::string::npos);
    CHECK(result.text.find("     2\t" + accents.substr(0, 4000) + " [… line truncated]\n") !=
          std::string::npos);
    CHECK(result.text.ends_with("     3\tcorta"));
}

TEST_CASE("read: CRLF sin el \\r, y UTF-8 con acentos tal cual", "[read]") {
    ToolFixture fixture;
    fixture.file("crlf.txt", "a\r\nb\r\n");
    CHECK(fixture.run("read", R"({"path": "crlf.txt"})").text == "     1\ta\n     2\tb");
    fixture.file("acentos.txt", "canción ñandú\n");
    CHECK(fixture.run("read", R"({"path": "acentos.txt"})").text == "     1\tcanción ñandú");
    fixture.file("año.txt", "x");
    CHECK(fixture.run("read", R"({"path": "año.txt"})").status == ToolStatus::Ok);
}

TEST_CASE("read: archivo vacío", "[read]") {
    ToolFixture fixture;
    fixture.file("vacio.txt", "");
    const ToolResult result = fixture.run("read", R"({"path": "vacio.txt"})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(result.text == "The file vacio.txt is empty.");
    CHECK(result.display == "Archivo vacío");
    CHECK(fixture.was_read("vacio.txt"));
}

TEST_CASE("read: un byte NUL es binario, y no se anota", "[read]") {
    ToolFixture fixture;
    fixture.file("bin.dat", std::string{"ab\0cd", 5});
    const ToolResult result = fixture.run("read", R"({"path": "bin.dat"})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text == "Cannot read binary file: bin.dat");
    CHECK(result.display == "Archivo binario");
    CHECK(fixture.state.reads.empty());
}

TEST_CASE("read: offset más allá del final dice cuántas líneas hay", "[read]") {
    ToolFixture fixture;
    fixture.file("f.txt", numbered_lines(3));
    const ToolResult result = fixture.run("read", R"({"path": "f.txt", "offset": 4})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text == "Offset 4 is past the end of f.txt, which has 3 lines.");
    CHECK(result.display == "Más allá del final (3 líneas)");
    CHECK(fixture.state.reads.empty());
}

TEST_CASE("read: archivo de más de 10 MiB sugiere grep", "[read]") {
    ToolFixture fixture;
    fixture.file("enorme.txt", std::string(10 * 1024 * 1024 + 1, 'x'));
    const ToolResult result = fixture.run("read", R"({"path": "enorme.txt"})");
    CHECK(result.status == ToolStatus::Error);
    CHECK(result.text == "File is too large to read: enorme.txt (10485761 bytes; the limit is "
                         "10 MiB). Use grep to find the lines you need.");
    CHECK(result.display == "Archivo demasiado grande");
    CHECK(fixture.state.reads.empty());
}

TEST_CASE("read: errores de ruta tal cual, sin anotar", "[read]") {
    ToolFixture fixture;
    const auto failed = [&](const std::string& arguments, const std::string& display) {
        const ToolResult result = fixture.run("read", arguments);
        CHECK(result.status == ToolStatus::Error);
        CHECK(result.display == display);
        CHECK_FALSE(result.text.empty());
        return result.text;
    };
    CHECK(failed(R"({"path": "../fuera/secreto.txt"})", "Fuera del proyecto")
              .starts_with("Path is outside the project"));
    const nlohmann::json absolute = {
        {"path", project_root_test::utf8(fixture.project.outside / "secreto.txt")}};
    failed(absolute.dump(), "Fuera del proyecto");
    CHECK(failed(R"({"path": "datos-moss/credentials.json"})", "Carpeta de Moss: bloqueada")
              .find("\"k\"") == std::string::npos);
    failed(R"({"path": "src"})", "No es un archivo");
    failed(R"({"path": "no-existe.txt"})", "No se pudo leer");
    failed(R"({"path": ""})", "Ruta inválida");
    CHECK(fixture.state.reads.empty());
}
