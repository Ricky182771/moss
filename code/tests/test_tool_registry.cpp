// El registro de herramientas: specs, etiquetas, resumen, y execute con sus fallos, hide_secret y
// el tope de salida.

#include "tool_fixture.hpp"
#include "tool_internal.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using chatbot::code::Tool;
using chatbot::code::ToolContext;
using chatbot::code::ToolRegistry;
using tool_test::ToolFixture;
using tool_test::ToolResult;
using tool_test::ToolStatus;

namespace {

/// Herramienta de prueba sin argumentos que devuelve lo que diga output (o lanza).
std::shared_ptr<const Tool> fake_tool(std::string name, std::string output, bool throws = false) {
    return std::make_shared<const Tool>(Tool{
        std::move(name), "Fake.", nlohmann::json::parse(R"({"type": "object"})"), "Falsa", {},
        [output, throws](const nlohmann::json&, ToolContext&) {
            if (throws) {
                throw std::runtime_error("falla");
            }
            return ToolResult{output, "hecho", ToolStatus::Ok};
        }});
}

} // namespace

TEST_CASE("El registro estándar ofrece read con su esquema en inglés", "[tool_registry]") {
    const ToolRegistry registry = ToolRegistry::standard();
    const std::vector<chatbot::ToolSpec> specs = registry.specs();
    REQUIRE(specs.size() == 4);
    CHECK(specs[0].name == "read");
    CHECK(specs[1].name == "glob");
    CHECK(specs[2].name == "grep");
    CHECK(specs[3].name == "todo_write");
    CHECK(specs[0].description.starts_with("Reads a text file"));
    const nlohmann::json schema = nlohmann::json::parse(specs[0].parameters);
    CHECK(schema["type"] == "object");
    CHECK(schema["required"] == nlohmann::json::array({"path"}));
    CHECK(schema["properties"].contains("offset"));
    CHECK(registry.label("read") == "Leer");
    CHECK_FALSE(registry.label("nada").has_value());
}

TEST_CASE("summarize resume la llamada en español antes de ejecutar", "[tool_registry]") {
    const ToolRegistry registry = ToolRegistry::standard();
    CHECK(registry.summarize("read", R"({"path": "cli/src/main.cpp"})") ==
          "Leer cli/src/main.cpp");
    CHECK(registry.summarize("read", R"({"path": "cli/src/main.cpp", "offset": 1, "limit": 200})") ==
          "Leer cli/src/main.cpp (1–200)");
    CHECK(registry.summarize("read", R"({"path": "a.cpp", "offset": "100"})") ==
          "Leer a.cpp (100–2099)");
    CHECK(registry.summarize("read", R"({"path": "a.cpp", "limit": 9223372036854775807})") ==
          "Leer a.cpp (1–9223372036854775807)");
    CHECK(registry.summarize("read",
                             R"({"path": "a.cpp", "offset": 2, "limit": 9223372036854775807})") ==
          "Leer a.cpp (2–…)");
    CHECK(registry.summarize("read", R"({"path": )") == "Leer");
    CHECK(registry.summarize("read", R"({"path": 3})") == "Leer");
    CHECK(registry.summarize("borrar", "{}") == "Herramienta desconocida: borrar");
    const std::string long_name(500, 'x');
    CHECK(registry.summarize(long_name, "{}") ==
          "Herramienta desconocida: " + std::string(200, 'x') + "…");
}

TEST_CASE("execute: argumentos inválidos y herramienta desconocida", "[tool_registry]") {
    ToolFixture fixture;
    const auto invalid = [&](const std::string& arguments) {
        const ToolResult result = fixture.run("read", arguments);
        CHECK(result.status == ToolStatus::InvalidArguments);
        CHECK(result.is_error());
        CHECK(result.display == "Argumentos inválidos");
        return result.text;
    };
    CHECK(invalid(R"({"path": )") ==
          "Invalid arguments for read: Arguments are not valid JSON; send a JSON object that "
          "matches the tool's schema.");
    CHECK(invalid("[\"f.txt\"]") ==
          "Invalid arguments for read: Arguments must be a JSON object.");
    CHECK(invalid("\"f.txt\"") == "Invalid arguments for read: Arguments must be a JSON object.");
    CHECK(invalid("{}") == "Invalid arguments for read: Missing required argument: path.");
    CHECK(invalid("") == "Invalid arguments for read: Missing required argument: path.");
    CHECK(invalid(R"({"path": 3})") ==
          "Invalid arguments for read: Argument 'path' must be a string.");
    CHECK(invalid(R"({"path": "f.txt", "limit": "mucho"})") ==
          "Invalid arguments for read: Argument 'limit' must be an integer.");

    const ToolResult unknown = fixture.run("borrar", "{}");
    CHECK(unknown.status == ToolStatus::UnknownTool);
    CHECK(unknown.text == "Unknown tool: borrar. Available tools: read, glob, grep, todo_write.");
    CHECK(unknown.display == "Herramienta desconocida: borrar");
    CHECK(fixture.state.reads.empty());
}

TEST_CASE("execute oculta los secretos en el texto y en el display", "[tool_registry]") {
    ToolFixture fixture;
    const std::string key = "nvapi-SECRETO-1234567890";
    fixture.context.secrets = {key, "tvly-OTRA-KEY-123"};
    fixture.file("env.txt", "API=" + key + "\nSEARCH=tvly-OTRA-KEY-123\n");
    const ToolResult read = fixture.run("read", R"({"path": "env.txt"})");
    CHECK(read.status == ToolStatus::Ok);
    CHECK(read.text == "     1\tAPI=[key oculta]\n     2\tSEARCH=[key oculta]");

    const ToolResult missing = fixture.run("read", R"({"path": ")" + key + R"(.txt"})");
    CHECK(missing.status == ToolStatus::Error);
    CHECK(missing.text.find(key) == std::string::npos);
    CHECK(missing.text.find("[key oculta]") != std::string::npos);

    const ToolResult unknown = fixture.run(key, "{}");
    CHECK(unknown.text.find(key) == std::string::npos);
    CHECK(unknown.display == "Herramienta desconocida: [key oculta]");
}

TEST_CASE("execute aplica el tope, pone texto si no hay salida y no deja salir excepciones",
          "[tool_registry]") {
    ToolFixture fixture;
    std::string big;
    for (int i = 0; i < 20000; ++i) {
        big += "linea " + std::to_string(i) + "\n";
    }
    const ToolRegistry registry{
        {fake_tool("big", big), fake_tool("quiet", ""), fake_tool("boom", "", true)}};

    const ToolResult capped = registry.execute("big", "{}", fixture.context);
    CHECK(capped.status == ToolStatus::Ok);
    CHECK(capped.text.size() < 31000);
    CHECK(capped.text.find("lines omitted") != std::string::npos);
    CHECK(capped.text.starts_with("linea 0\n"));

    CHECK(registry.execute("quiet", "{}", fixture.context).text == "(no output)");

    const ToolResult thrown = registry.execute("boom", "{}", fixture.context);
    CHECK(thrown.status == ToolStatus::Error);
    CHECK(thrown.text == "Internal error while running the tool.");

    CHECK(registry.execute("read", "{}", fixture.context).text ==
          "Unknown tool: read. Available tools: big, quiet, boom.");
}
