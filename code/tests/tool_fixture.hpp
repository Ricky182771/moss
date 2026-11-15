#ifndef CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP
#define CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP

// Ayudas de las pruebas de herramientas: un proyecto temporal (project_root_fixture.hpp) con una
// carpeta de Moss dentro de la raíz, y el registro listo para ejecutar.

#include "chatbot/cancel_token.h"
#include "chatbot/code/tool.h"

#include "project_root_fixture.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace tool_test {

namespace fs = std::filesystem;
using chatbot::code::ToolContext;
using chatbot::code::ToolRegistry;
using chatbot::code::ToolResult;
using chatbot::code::ToolState;
using chatbot::code::ToolStatus;

struct ToolFixture {
    project_root_test::Project project;
    fs::path moss_inside = project.proy / "datos-moss";
    chatbot::code::ProjectRoot root = make_root();
    chatbot::CancelToken cancel;
    ToolState state;
    ToolContext context{root, cancel, state};
    ToolRegistry registry = ToolRegistry::standard();

    ToolResult run(std::string_view name, std::string_view arguments) {
        return registry.execute(name, arguments, context);
    }

    /// Escribe proy/<name> y devuelve su ruta.
    fs::path file(const std::string& name, const std::string& text) {
        const fs::path path = project.proy / project_root_test::from_utf8(name);
        project_root_test::write_file(path, text);
        return path;
    }

    [[nodiscard]] bool was_read(const std::string& name) const {
        return state.reads.contains(fs::canonical(project.proy / project_root_test::from_utf8(name)));
    }

private:
    chatbot::code::ProjectRoot make_root() {
        fs::create_directories(moss_inside);
        project_root_test::write_file(moss_inside / "credentials.json", "{\"k\": \"secreto\"}");
        return project_root_test::open_root(project.proy, {project.moss, moss_inside});
    }
};

} // namespace tool_test

#endif // CHATBOT_CODE_TESTS_TOOL_FIXTURE_HPP
