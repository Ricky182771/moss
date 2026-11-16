// Validador de argumentos (code/src/tool_schema.h): tipos, errores y tolerancias.

#include "tool_schema.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace {

using chatbot::code::validate_arguments;
using nlohmann::json;

const json kSchema = json::parse(R"({
    "type": "object",
    "properties": {
        "path": {"type": "string"},
        "offset": {"type": "integer", "minimum": 1},
        "ignore_case": {"type": "boolean"},
        "output_mode": {"type": "string", "enum": ["files", "content", "count"]},
        "paths": {"type": "array", "items": {"type": "string"}},
        "todos": {"type": "array", "items": {
            "type": "object",
            "properties": {
                "content": {"type": "string"},
                "status": {"type": "string", "enum": ["pending", "in_progress", "completed"]}
            },
            "required": ["content", "status"]
        }}
    },
    "required": ["path"]
})");

/// El error de validar text, o "" si pasa; arguments queda normalizado.
std::string error_of(const std::string& text, json* normalized = nullptr) {
    json arguments = json::parse(text);
    const std::optional<std::string> error = validate_arguments(kSchema, arguments);
    if (normalized != nullptr) {
        *normalized = arguments;
    }
    return error.value_or("");
}

} // namespace

TEST_CASE("validate_arguments acepta argumentos correctos e ignora los de más", "[tool_schema]") {
    json arguments;
    CHECK(error_of(R"({"path": "a", "offset": 3, "ignore_case": false, "output_mode": "count",
                       "paths": ["x", "y"], "todos": [{"content": "c", "status": "pending"}],
                       "extra": {"lo": "que sea"}})",
                   &arguments)
              .empty());
    CHECK(arguments["offset"] == 3);
}

TEST_CASE("validate_arguments: objeto y obligatorios", "[tool_schema]") {
    CHECK(error_of("[1]") == "Arguments must be a JSON object");
    CHECK(error_of("{}") == "Missing required argument: path");
    CHECK(error_of(R"({"path": null})") == "Missing required argument: path");
}

TEST_CASE("validate_arguments: null en una opcional cuenta como ausente", "[tool_schema]") {
    json arguments;
    CHECK(error_of(R"({"path": "a", "offset": null})", &arguments).empty());
    CHECK_FALSE(arguments.contains("offset"));
}

TEST_CASE("validate_arguments: cadenas y enum", "[tool_schema]") {
    CHECK(error_of(R"({"path": 3})") == "Argument 'path' must be a string");
    CHECK(error_of(R"({"path": "a", "output_mode": "all"})") ==
          "Argument 'output_mode' must be one of: files, content, count");
}

TEST_CASE("validate_arguments: enteros, tolerancias y mínimo", "[tool_schema]") {
    json arguments;
    CHECK(error_of(R"({"path": "a", "offset": "100"})", &arguments).empty());
    CHECK(arguments["offset"].is_number_integer());
    CHECK(arguments["offset"] == 100);
    CHECK(error_of(R"({"path": "a", "offset": 7.0})", &arguments).empty());
    CHECK(arguments["offset"].is_number_integer());
    CHECK(arguments["offset"] == 7);

    const std::string must = "Argument 'offset' must be an integer";
    CHECK(error_of(R"({"path": "a", "offset": "abc"})") == must);
    CHECK(error_of(R"({"path": "a", "offset": "10 "})") == must);
    CHECK(error_of(R"({"path": "a", "offset": 1.5})") == must);
    CHECK(error_of(R"({"path": "a", "offset": true})") == must);
    CHECK(error_of(R"({"path": "a", "offset": [1]})") == must);
    CHECK(error_of(R"({"path": "a", "offset": 0})") == "Argument 'offset' must be at least 1");
    CHECK(error_of(R"({"path": "a", "offset": "-5"})") == "Argument 'offset' must be at least 1");
    CHECK(error_of(R"({"path": "a", "offset": 18446744073709551615})") ==
          "Argument 'offset' is too large");
}

TEST_CASE("validate_arguments: booleanos", "[tool_schema]") {
    json arguments;
    CHECK(error_of(R"({"path": "a", "ignore_case": "true"})", &arguments).empty());
    CHECK(arguments["ignore_case"] == true);
    CHECK(error_of(R"({"path": "a", "ignore_case": "false"})", &arguments).empty());
    CHECK(arguments["ignore_case"] == false);
    CHECK(error_of(R"({"path": "a", "ignore_case": "yes"})") ==
          "Argument 'ignore_case' must be a boolean (true or false)");
    CHECK(error_of(R"({"path": "a", "ignore_case": 1})") ==
          "Argument 'ignore_case' must be a boolean (true or false)");
}

TEST_CASE("validate_arguments: arreglos de cadenas y de objetos", "[tool_schema]") {
    CHECK(error_of(R"({"path": "a", "paths": "x"})") == "Argument 'paths' must be an array");
    CHECK(error_of(R"({"path": "a", "paths": ["x", 2]})") ==
          "Argument 'paths[1]' must be a string");
    CHECK(error_of(R"({"path": "a", "todos": ["x"]})") == "Argument 'todos[0]' must be an object");
    CHECK(error_of(R"({"path": "a", "todos": [{"status": "pending"}]})") ==
          "Missing required argument: todos[0].content");
    CHECK(error_of(R"({"path": "a", "todos": [{"content": "c", "status": "pending"},
                                              {"content": "d", "status": "done"}]})") ==
          "Argument 'todos[1].status' must be one of: pending, in_progress, completed");
}
