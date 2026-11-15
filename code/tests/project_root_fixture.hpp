#ifndef CHATBOT_CODE_TESTS_PROJECT_ROOT_FIXTURE_HPP
#define CHATBOT_CODE_TESTS_PROJECT_ROOT_FIXTURE_HPP

// Ayudas de las pruebas de ProjectRoot (test_project_root.cpp y test_project_root_windows.cpp).

#include "chatbot/cancel_token.h"
#include "chatbot/code/project_root.h"

#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace project_root_test {

namespace fs = std::filesystem;
using chatbot::code::FileContent;
using chatbot::code::PathError;
using chatbot::code::PathErrorKind;
using chatbot::code::ProjectRoot;
using chatbot::code::ReadResult;
using chatbot::code::ResolvedPath;
using chatbot::code::WalkFile;

inline std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

inline fs::path from_utf8(std::string_view text) {
    return fs::path{std::u8string{text.begin(), text.end()}};
}

inline void write_file(const fs::path& path, const std::string& text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
    REQUIRE(file.good());
}

inline ProjectRoot open_root(const fs::path& root, const std::vector<fs::path>& blocked = {}) {
    std::vector<std::string> texts;
    for (const fs::path& dir : blocked) {
        texts.push_back(utf8(dir));
    }
    const chatbot::Result<ProjectRoot> result = ProjectRoot::open(utf8(root), texts);
    if (result.is_error()) {
        UNSCOPED_INFO(result.error().message);
    }
    REQUIRE(result.is_ok());
    return result.value();
}

inline fs::path resolved(const ResolvedPath& result) {
    if (const auto* error = std::get_if<PathError>(&result)) {
        UNSCOPED_INFO(error->message);
    }
    REQUIRE(std::holds_alternative<fs::path>(result));
    return std::get<fs::path>(result);
}

inline fs::path resolved(const ProjectRoot& root, std::string_view request) {
    return resolved(root.resolve(request));
}

inline PathError failure(const ResolvedPath& result) {
    REQUIRE(std::holds_alternative<PathError>(result));
    return std::get<PathError>(result);
}

inline PathErrorKind failure_kind(const ProjectRoot& root, std::string_view request) {
    return failure(root.resolve(request)).kind;
}

inline FileContent read_ok(const ProjectRoot& root, const fs::path& path, std::size_t max_bytes) {
    const ReadResult result = root.read_file(path, max_bytes);
    if (const auto* error = std::get_if<PathError>(&result)) {
        UNSCOPED_INFO(error->message);
    }
    REQUIRE(std::holds_alternative<FileContent>(result));
    return std::get<FileContent>(result);
}

inline PathError read_failure(const ProjectRoot& root, const fs::path& path) {
    const ReadResult result = root.read_file(path, 1024);
    REQUIRE(std::holds_alternative<PathError>(result));
    return std::get<PathError>(result);
}

/// Lo que entrega walk desde start, en orden; saltando .git, node_modules y build*.
inline std::vector<std::string> walk_all(const ProjectRoot& root, const fs::path& start) {
    const chatbot::CancelToken cancel;
    std::vector<std::string> files;
    const std::optional<PathError> error = root.walk(
        start,
        [](std::string_view name) {
            return name == ".git" || name == "node_modules" || name.starts_with("build");
        },
        [&](const WalkFile& file) {
            files.push_back(file.display);
            return true;
        },
        cancel);
    REQUIRE_FALSE(error.has_value());
    return files;
}

/// temp/proy (con src/a.txt) es la raíz; temp/fuera (con secreto.txt) queda fuera, y
/// temp/config-moss (con credentials.json) hace de carpeta de Moss.
struct Project {
    chatbot_test::ScopedTempDir temp;
    fs::path proy = temp.path() / "proy";
    fs::path outside = temp.path() / "fuera";
    fs::path moss = temp.path() / "config-moss";

    Project() {
        fs::create_directories(proy / "src");
        fs::create_directories(outside);
        fs::create_directories(moss);
        write_file(proy / "src" / "a.txt", "a");
        write_file(outside / "secreto.txt", "secreto");
        write_file(moss / "credentials.json", "{\"clave\": \"no-debe-salir\"}");
    }
};

} // namespace project_root_test

#endif // CHATBOT_CODE_TESTS_PROJECT_ROOT_FIXTURE_HPP
