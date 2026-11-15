// Límites del turno desde config.json → "code", y el PermissionEngine de C1.

#include "chatbot/code/code_limits.h"
#include "chatbot/code/permissions.h"

#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using chatbot::code::CodeLimits;
using chatbot::code::LoadedCodeLimits;
using chatbot::code::load_code_limits;
using chatbot::code::Permission;
using chatbot::code::PermissionEngine;

namespace {

LoadedCodeLimits load_with(const chatbot_test::ScopedTempDir& temp, const std::string& text) {
    const std::filesystem::path path = temp.path() / "config.json";
    {
        std::ofstream file{path, std::ios::binary};
        file << text;
    }
    return load_code_limits(path.string());
}

void check_defaults(const CodeLimits& limits) {
    CHECK(limits.max_steps == 50);
    CHECK(limits.max_repeated_calls == 3);
    CHECK(limits.max_invalid_calls == 3);
    CHECK(limits.history_limit_bytes == 300000);
}

} // namespace

TEST_CASE("code limits: sin archivo ni sección, los valores por defecto sin aviso",
          "[code_limits]") {
    const chatbot_test::ScopedTempDir temp;
    const LoadedCodeLimits missing = load_code_limits((temp.path() / "no-existe.json").string());
    check_defaults(missing.limits);
    CHECK(missing.warnings.empty());

    const LoadedCodeLimits no_section = load_with(temp, R"({"model": "m"})");
    check_defaults(no_section.limits);
    CHECK(no_section.warnings.empty());
}

TEST_CASE("code limits: valores válidos y llaves ajenas", "[code_limits]") {
    const chatbot_test::ScopedTempDir temp;
    const LoadedCodeLimits loaded = load_with(
        temp, R"({"code": {"max_steps": 7, "max_repeated_calls": 2, "max_invalid_calls": 1,
                           "history_limit_bytes": 0, "otra": true}})");
    CHECK(loaded.limits.max_steps == 7);
    CHECK(loaded.limits.max_repeated_calls == 2);
    CHECK(loaded.limits.max_invalid_calls == 1);
    CHECK(loaded.limits.history_limit_bytes == 0);
    CHECK(loaded.warnings.empty());
}

TEST_CASE("code limits: tipo equivocado o fuera de rango usa el de defecto con aviso",
          "[code_limits]") {
    const chatbot_test::ScopedTempDir temp;
    const LoadedCodeLimits loaded = load_with(
        temp, R"({"code": {"max_steps": "10", "max_repeated_calls": 1, "max_invalid_calls": -2,
                           "history_limit_bytes": 2.5}})");
    check_defaults(loaded.limits);
    REQUIRE(loaded.warnings.size() == 4);
    CHECK(loaded.warnings[0] ==
          "\"code.max_steps\" en config.json tiene que ser un entero entre 1 y 1000; se usa 50.");
    CHECK(loaded.warnings[1].find("entre 2 y 100; se usa 3.") != std::string::npos);

    const LoadedCodeLimits partial = load_with(temp, R"({"code": {"max_steps": 0, "max_invalid_calls": 5}})");
    CHECK(partial.limits.max_steps == 50);
    CHECK(partial.limits.max_invalid_calls == 5);
    CHECK(partial.warnings.size() == 1);
}

TEST_CASE("code limits: \"code\" que no es objeto o config.json roto", "[code_limits]") {
    const chatbot_test::ScopedTempDir temp;
    const LoadedCodeLimits not_object = load_with(temp, R"({"code": [1, 2]})");
    check_defaults(not_object.limits);
    REQUIRE(not_object.warnings.size() == 1);
    CHECK(not_object.warnings[0].find("no es un objeto") != std::string::npos);

    const LoadedCodeLimits broken = load_with(temp, "{roto");
    check_defaults(broken.limits);
    REQUIRE(broken.warnings.size() == 1);
    CHECK(broken.warnings[0].find("límites predeterminados") != std::string::npos);
}

TEST_CASE("permisos de C1: lectura sin preguntar, lo demás pregunta, permitir siempre",
          "[permissions]") {
    PermissionEngine engine;
    for (const char* name : {"read", "glob", "grep", "todo_write"}) {
        CHECK(engine.decide(name, "{}") == Permission::Allow);
    }
    CHECK(engine.decide("edit", "{}") == Permission::Ask);
    engine.allow_always("edit");
    CHECK(engine.decide("edit", "{}") == Permission::Allow);
    CHECK(engine.decide("bash", "{}") == Permission::Ask);

    PermissionEngine denying{[](std::string_view, std::string_view) { return Permission::Deny; }};
    denying.allow_always("read");
    CHECK(denying.decide("read", "{}") == Permission::Deny);
}
