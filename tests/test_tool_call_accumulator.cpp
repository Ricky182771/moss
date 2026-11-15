#include "tool_call_accumulator.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using chatbot::Message;
using chatbot::Role;
using chatbot::ToolCall;
using chatbot::ToolCallAccumulator;
using chatbot::sse::ToolCallChunk;

ToolCallChunk piece(std::optional<std::size_t> index, std::string id, std::string name,
                    std::string arguments) {
    return ToolCallChunk{index, std::move(id), std::move(name), std::move(arguments)};
}

/// Lo que se anunciaría ahora (posición y nombre), en orden.
std::vector<std::pair<std::size_t, std::string>> announced(ToolCallAccumulator& calls) {
    std::vector<std::pair<std::size_t, std::string>> out;
    while (const std::optional<ToolCallAccumulator::Started> started = calls.next_started()) {
        out.emplace_back(started->position, started->name);
    }
    return out;
}

bool same(const std::vector<ToolCall>& got, const std::vector<ToolCall>& want) {
    if (got.size() != want.size()) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i].id != want[i].id || got[i].name != want[i].name ||
            got[i].arguments != want[i].arguments) {
            return false;
        }
    }
    return true;
}

using Announced = std::vector<std::pair<std::size_t, std::string>>;

} // namespace

TEST_CASE("Acumulador: la llamada completa en un solo pedazo", "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_1", "read", R"({"path":"a"})"));
    CHECK(announced(calls) == Announced{{0, "read"}});
    CHECK(announced(calls).empty()); // Una sola vez.
    CHECK(same(calls.finish({}), {{"call_1", "read", R"({"path":"a"})"}}));
}

TEST_CASE("Acumulador: varias llamadas intercaladas por index", "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_a", "read", ""));
    calls.add(piece(1, "call_b", "glob", R"({"p":)"));
    calls.add(piece(0, "", "", R"({"path":)"));
    calls.add(piece(1, "", "", R"("*"})"));
    calls.add(piece(0, "", "", R"("x"})"));
    CHECK(announced(calls) == Announced{{0, "read"}, {1, "glob"}});
    CHECK(same(calls.finish({}),
               {{"call_a", "read", R"({"path":"x"})"}, {"call_b", "glob", R"({"p":"*"})"}}));
}

TEST_CASE("Acumulador: sin index, un id nuevo abre llamada y sin id se suma a la última",
          "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(std::nullopt, "call_a", "read", "{"));
    calls.add(piece(std::nullopt, "", "", "}"));
    calls.add(piece(std::nullopt, "call_b", "glob", R"({"p")"));
    calls.add(piece(std::nullopt, "", "", R"(:"*"})"));
    calls.add(piece(std::nullopt, "call_a", "", "")); // Id ya visto: no abre otra.
    CHECK(announced(calls) == Announced{{0, "read"}, {1, "glob"}});
    CHECK(same(calls.finish({}), {{"call_a", "read", "{}"}, {"call_b", "glob", R"({"p":"*"})"}}));
}

TEST_CASE("Acumulador: arguments vacío al terminar queda {}", "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_1", "list", ""));
    CHECK(same(calls.finish({}), {{"call_1", "list", "{}"}}));
}

TEST_CASE("Acumulador: los arguments no se validan", "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_1", "read", R"({"path":)"));
    CHECK(same(calls.finish({}), {{"call_1", "read", R"({"path":)"}}));
}

TEST_CASE("Acumulador: id que nunca llegó usa el primer call_moss_<n> libre",
          "[herramientas][acumulador]") {
    const std::vector<Message> history{
        Message{Role::User, "Hola"},
        Message{Role::Assistant, "", {ToolCall{"call_moss_1", "read", "{}"}}},
        Message{Role::Tool, "contenido", {}, "call_moss_1"},
        Message{Role::User, "sigue"},
    };
    ToolCallAccumulator calls;
    calls.add(piece(0, "", "read", ""));
    calls.add(piece(1, "call_moss_2", "glob", ""));
    calls.add(piece(2, "", "list", ""));
    CHECK(same(calls.finish(history), {{"call_moss_3", "read", "{}"},
                                       {"call_moss_2", "glob", "{}"},
                                       {"call_moss_4", "list", "{}"}}));
    // Sin historial ni ids del servidor, desde 1.
    ToolCallAccumulator plain;
    plain.add(piece(0, "", "read", ""));
    plain.add(piece(1, "", "glob", ""));
    CHECK(same(plain.finish({}), {{"call_moss_1", "read", "{}"}, {"call_moss_2", "glob", "{}"}}));
}

TEST_CASE("Acumulador: id del servidor que ya está en el historial se renombra",
          "[herramientas][acumulador]") {
    // El servidor numera por respuesta: call_0 otra vez.
    const std::vector<Message> history{
        Message{Role::User, "Hola"},
        Message{Role::Assistant, "", {ToolCall{"call_0", "read", "{}"}}},
        Message{Role::Tool, "contenido", {}, "call_0"},
        Message{Role::User, "sigue"},
    };
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_0", "read", "{}"));
    calls.add(piece(1, "call_1", "glob", "{}"));
    CHECK(same(calls.finish(history), {{"call_moss_1", "read", "{}"}, {"call_1", "glob", "{}"}}));
}

TEST_CASE("Acumulador: dos llamadas con el mismo id, la segunda se renombra",
          "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_x", "read", "{}"));
    calls.add(piece(1, "call_x", "glob", "{}"));
    CHECK(same(calls.finish({}), {{"call_x", "read", "{}"}, {"call_moss_1", "glob", "{}"}}));
}

TEST_CASE("assign_free_ids: un id generado no le quita el suyo a una llamada posterior",
          "[herramientas][acumulador]") {
    std::vector<ToolCall> calls{{"", "read", "{}"}, {"call_moss_1", "glob", "{}"}};
    chatbot::assign_free_ids(calls, {});
    CHECK(same(calls, {{"call_moss_2", "read", "{}"}, {"call_moss_1", "glob", "{}"}}));
}

TEST_CASE("Acumulador: name que llega tarde o partido toma el primero no vacío",
          "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_1", "", ""));
    CHECK(announced(calls).empty());
    calls.add(piece(0, "", "re", R"({"a":1})"));
    CHECK(announced(calls) == Announced{{0, "re"}});
    calls.add(piece(0, "", "ad", ""));
    CHECK(announced(calls).empty());
    CHECK(same(calls.finish({}), {{"call_1", "re", R"({"a":1})"}}));
}

TEST_CASE("Acumulador: una llamada sin name se entrega vacía y detiene los anuncios siguientes",
          "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(0, "call_1", "", "{}"));
    calls.add(piece(1, "call_2", "glob", "{}"));
    CHECK(announced(calls).empty()); // En orden de aparición: espera a la 0.
    CHECK(same(calls.finish({}), {{"call_1", "", "{}"}, {"call_2", "glob", "{}"}}));
}

TEST_CASE("Acumulador: un pedazo sin nada no abre llamada", "[herramientas][acumulador]") {
    ToolCallAccumulator calls;
    calls.add(piece(std::nullopt, "", "", ""));
    CHECK(calls.finish({}).empty());
    // Solo con index sí abre (el nombre puede llegar después).
    calls.add(piece(0, "", "", ""));
    CHECK(same(calls.finish({}), {{"call_moss_1", "", "{}"}}));
}
