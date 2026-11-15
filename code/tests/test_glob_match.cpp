// El comparador de patrones glob (code/src/glob_match.h), puro.

#include "glob_match.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using chatbot::code::glob_match;

TEST_CASE("glob_match: * no cruza carpetas, ? es un carácter", "[glob_match]") {
    CHECK(glob_match("src/*.cpp", "src/a.cpp"));
    CHECK(glob_match("src/*.cpp", "src/.cpp"));
    CHECK_FALSE(glob_match("src/*.cpp", "src/sub/a.cpp"));
    CHECK_FALSE(glob_match("src/*", "src/a/b"));
    CHECK(glob_match("src/a?c", "src/abc"));
    CHECK_FALSE(glob_match("src/a?c", "src/ac"));
    CHECK_FALSE(glob_match("a?b", "a/b"));
    CHECK(glob_match("a*b*c", "a--b--c"));
    CHECK_FALSE(glob_match("a*b*c", "a--b--d"));
}

TEST_CASE("glob_match: ** al principio, en medio y al final", "[glob_match]") {
    CHECK(glob_match("**/*.cpp", "a.cpp"));
    CHECK(glob_match("**/*.cpp", "x/y/a.cpp"));
    CHECK_FALSE(glob_match("**/*.cpp", "x/y/a.hpp"));
    CHECK(glob_match("src/**/a.cpp", "src/a.cpp"));
    CHECK(glob_match("src/**/a.cpp", "src/x/y/a.cpp"));
    CHECK_FALSE(glob_match("src/**/a.cpp", "other/x/a.cpp"));
    CHECK(glob_match("src/**", "src/x/y/a.cpp"));
    CHECK(glob_match("src/**", "src/a"));
    CHECK_FALSE(glob_match("src/**", "lib/a"));
    CHECK(glob_match("**", "cualquier/cosa/aqui"));
}

TEST_CASE("glob_match: un patrón sin / mira el nombre a cualquier profundidad", "[glob_match]") {
    CHECK(glob_match("*.md", "README.md"));
    CHECK(glob_match("*.md", "docs/a.md"));
    CHECK(glob_match("*.md", "docs/deep/er/a.md"));
    CHECK_FALSE(glob_match("*.md", "docs/a.md.bak"));
    CHECK(glob_match("a.md", "x/y/a.md"));
    CHECK_FALSE(glob_match("docs/*.md", "x/docs/a.md")); // Con '/': ruta completa.
    CHECK(glob_match("./src/*.cpp", "src/a.cpp"));
}

TEST_CASE("glob_match: clases", "[glob_match]") {
    CHECK(glob_match("[abc].txt", "b.txt"));
    CHECK_FALSE(glob_match("[abc].txt", "d.txt"));
    CHECK(glob_match("[a-c].txt", "b.txt"));
    CHECK_FALSE(glob_match("[a-c].txt", "z.txt"));
    CHECK(glob_match("[!x].txt", "a.txt"));
    CHECK_FALSE(glob_match("[!x].txt", "x.txt"));
    CHECK(glob_match("[^x].txt", "a.txt"));
    CHECK(glob_match("[]a].txt", "].txt"));
    CHECK(glob_match("[a-]x", "-x"));
    CHECK_FALSE(glob_match("[a-c]", "/"));
    CHECK_FALSE(glob_match("[!a]", "/"));
}

TEST_CASE("glob_match: llaves, anidadas y mal cerradas", "[glob_match]") {
    CHECK(glob_match("**/*.{ts,tsx}", "src/a.ts"));
    CHECK(glob_match("**/*.{ts,tsx}", "src/a.tsx"));
    CHECK_FALSE(glob_match("**/*.{ts,tsx}", "src/a.js"));
    CHECK(glob_match("{src,lib}/**/*.c", "lib/x/a.c"));
    CHECK(glob_match("a{b,c{d,e}}f", "acef"));
    CHECK(glob_match("a{b,c{d,e}}f", "abf"));
    CHECK_FALSE(glob_match("a{b,c{d,e}}f", "acf"));
    CHECK(glob_match("a{,b}c", "ac"));
    CHECK(glob_match("a{b,c", "a{b,c")); // Sin cerrar: literal.
    CHECK_FALSE(glob_match("a{b,c", "ab"));
    CHECK(glob_match("a{b,c}}", "ab}"));
    CHECK(glob_match("}", "}"));
}

TEST_CASE("glob_match: clase mal cerrada y escapes", "[glob_match]") {
    CHECK(glob_match("a[bc", "a[bc"));
    CHECK_FALSE(glob_match("a[bc", "ab"));
    CHECK(glob_match("a\\*b", "a*b"));
    CHECK_FALSE(glob_match("a\\*b", "axb"));
    CHECK(glob_match("a\\", "a\\")); // Barra al final: literal.
}

TEST_CASE("glob_match: UTF-8 con acentos", "[glob_match]") {
    CHECK(glob_match("canción?.txt", "canciónA.txt"));
    CHECK(glob_match("?.txt", "ñ.txt")); // Un carácter, no un byte.
    CHECK_FALSE(glob_match("??.txt", "ñ.txt"));
    CHECK(glob_match("[á-é]", "é"));
    CHECK(glob_match("**/*ción*", "docs/información.md"));
    CHECK(glob_match("a?b", "a\xFF"
                            "b")); // Un byte inválido cuenta como un carácter.
}

TEST_CASE("glob_match: mayúsculas solo si se pide", "[glob_match]") {
    CHECK_FALSE(glob_match("*.CPP", "a.cpp"));
    CHECK(glob_match("*.CPP", "a.cpp", true));
    CHECK(glob_match("[a-c]x", "Bx", true));
    CHECK(glob_match("SRC/**", "src/a", true));
}

TEST_CASE("glob_match: un patrón hostil termina rápido", "[glob_match]") {
    const std::string name(4000, 'a');
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(glob_match("*a*a*a*a*a*a*a*a*a*b", name));
    CHECK_FALSE(glob_match("**/*a*/**/*a*/**/*a*/**/*a*b", name));
    std::string braces;
    for (int i = 0; i < 40; ++i) {
        braces += "{a,b}";
    }
    CHECK_FALSE(glob_match(braces + "c", std::string(40, 'a')));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds{5});
}

TEST_CASE("glob_match: topes de patrón y de ruta", "[glob_match]") {
    CHECK_FALSE(glob_match(std::string(600, 'a'), std::string(600, 'a')));
    CHECK_FALSE(glob_match("**", std::string(5000, 'a')));
    CHECK(glob_match("**", std::string(4000, 'a')));
}
