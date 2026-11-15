// La herramienta todo_write, por el registro con JSON crudo.

#include "tool_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using chatbot::code::TodoStatus;
using tool_test::ToolFixture;
using tool_test::ToolResult;
using tool_test::ToolStatus;

namespace {

constexpr const char* kThree = R"({"todos": [
    {"content": "Leer el parser", "status": "completed", "active_form": "Leyendo el parser"},
    {"content": "Arreglar el bug", "status": "in_progress", "active_form": "Arreglando el bug"},
    {"content": "Probar", "status": "pending", "active_form": "Probando"}
]})";

} // namespace

TEST_CASE("todo_write reemplaza la lista completa", "[todo_write]") {
    ToolFixture fixture;
    const ToolResult first = fixture.run("todo_write", kThree);
    CHECK(first.status == ToolStatus::Ok);
    CHECK(first.text == "Todo list updated: 3 tasks (1 pending, 1 in progress, 1 completed).");
    CHECK(first.display == "3 tareas · 1 en curso");
    REQUIRE(fixture.state.todos.size() == 3);
    CHECK(fixture.state.todos[1].content == "Arreglar el bug");
    CHECK(fixture.state.todos[1].status == TodoStatus::InProgress);
    CHECK(fixture.state.todos[1].active_form == "Arreglando el bug");

    const ToolResult second = fixture.run(
        "todo_write",
        R"({"todos": [{"content": "Solo una", "status": "pending", "active_form": "Haciendo"}]})");
    CHECK(second.display == "1 tarea");
    REQUIRE(fixture.state.todos.size() == 1);
    CHECK(fixture.state.todos[0].status == TodoStatus::Pending);
}

TEST_CASE("todo_write con lista vacía la borra", "[todo_write]") {
    ToolFixture fixture;
    static_cast<void>(fixture.run("todo_write", kThree));
    const ToolResult result = fixture.run("todo_write", R"({"todos": []})");
    CHECK(result.status == ToolStatus::Ok);
    CHECK(result.text == "Todo list cleared.");
    CHECK(fixture.state.todos.empty());
}

TEST_CASE("todo_write valida estados, campos y content vacío sin tocar la lista", "[todo_write]") {
    ToolFixture fixture;
    static_cast<void>(fixture.run("todo_write", kThree));

    const ToolResult bad_status = fixture.run(
        "todo_write", R"({"todos": [{"content": "x", "status": "hecho", "active_form": "y"}]})");
    CHECK(bad_status.status == ToolStatus::InvalidArguments);
    CHECK(bad_status.text.find("must be one of: pending, in_progress, completed") !=
          std::string::npos);

    CHECK(fixture.run("todo_write",
                      R"({"todos": [{"content": "x", "status": "pending"}]})")
              .status == ToolStatus::InvalidArguments);
    CHECK(fixture.run("todo_write", R"({"todos": "nada"})").status == ToolStatus::InvalidArguments);
    CHECK(fixture.run("todo_write", "{}").status == ToolStatus::InvalidArguments);

    const ToolResult empty = fixture.run(
        "todo_write",
        R"({"todos": [{"content": "ok", "status": "pending", "active_form": "a"},
                      {"content": "  ", "status": "pending", "active_form": "b"}]})");
    CHECK(empty.status == ToolStatus::InvalidArguments);
    CHECK(empty.text.find("todos[1].content must not be empty") != std::string::npos);
    CHECK(fixture.state.todos.size() == 3); // Sigue la lista anterior.
}

TEST_CASE("todo_write: etiqueta y resumen de la llamada", "[todo_write]") {
    ToolFixture fixture;
    CHECK(fixture.registry.label("todo_write") == "Lista de tareas");
    CHECK(fixture.registry.summarize("todo_write", kThree) == "Lista de tareas (3)");
    CHECK(fixture.registry.summarize("todo_write", R"({"todos": []})") == "Lista de tareas (0)");
}
