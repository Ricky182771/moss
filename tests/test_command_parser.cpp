#include <catch2/catch_test_macros.hpp>

#include "command_parser.h"

#include <string>

using namespace chatbot::cli;

namespace {

/// parse_command en el modo del chat (el modo no tiene valor por defecto).
ParsedCommand parse_chat(std::string_view input, const EndpointLookup& is_endpoint = {}) {
    return parse_command(input, is_endpoint, CommandMode::Chat);
}

} // namespace

TEST_CASE("parse_command reconoce /buscar con consulta", "[command_parser]") {
    const auto result = parse_chat("/buscar quién ganó el mundial");
    REQUIRE(result.type == ParsedCommand::Type::Search);
    REQUIRE(result.text == "quién ganó el mundial");
}

TEST_CASE("parse_command recorta espacios de la consulta", "[command_parser]") {
    const auto result = parse_chat("/buscar   consulta con espacios   ");
    REQUIRE(result.type == ParsedCommand::Type::Search);
    REQUIRE(result.text == "consulta con espacios");
}

TEST_CASE("parse_command detecta /buscar sin consulta", "[command_parser]") {
    SECTION("Sin espacio después") {
        const auto result = parse_chat("/buscar");
        REQUIRE(result.type == ParsedCommand::Type::SearchEmpty);
        REQUIRE(result.text.empty());
    }

    SECTION("Solo espacios después") {
        const auto result = parse_chat("/buscar    ");
        REQUIRE(result.type == ParsedCommand::Type::SearchEmpty);
        REQUIRE(result.text.empty());
    }
}

TEST_CASE("parse_command no reconoce comandos similares", "[command_parser]") {
    SECTION("/buscarx no es el comando") {
        const auto result = parse_chat("/buscarx algo");
        REQUIRE(result.type == ParsedCommand::Type::Normal);
        REQUIRE(result.text == "/buscarx algo");
    }

    SECTION("Texto normal") {
        const auto result = parse_chat("hola mundo");
        REQUIRE(result.type == ParsedCommand::Type::Normal);
        REQUIRE(result.text == "hola mundo");
    }

    SECTION("Otro comando con /") {
        const auto result = parse_chat("/otro comando");
        REQUIRE(result.type == ParsedCommand::Type::Normal);
        REQUIRE(result.text == "/otro comando");
    }
}

TEST_CASE("parse_command maneja entrada vacía", "[command_parser]") {
    const auto result = parse_chat("");
    REQUIRE(result.type == ParsedCommand::Type::Normal);
    REQUIRE(result.text.empty());
}

TEST_CASE("parse_command preserva consultas con caracteres especiales", "[command_parser]") {
    const auto result = parse_chat("/buscar ¿Cuál es la capital de México?");
    REQUIRE(result.type == ParsedCommand::Type::Search);
    REQUIRE(result.text == "¿Cuál es la capital de México?");
}

TEST_CASE("parse_command reconoce /copiar", "[command_parser]") {
    SECTION("Sin número: el último bloque") {
        const auto result = parse_chat("/copiar");
        REQUIRE(result.type == ParsedCommand::Type::Copy);
        CHECK_FALSE(result.block.has_value());
    }

    SECTION("Con número y espacios de más") {
        const auto result = parse_chat("  /copiar   3  ");
        REQUIRE(result.type == ParsedCommand::Type::Copy);
        REQUIRE(result.block == 3);
    }

    SECTION("Un número fuera de rango lo revisa quien lo usa") {
        const auto zero = parse_chat("/copiar 0");
        REQUIRE(zero.type == ParsedCommand::Type::Copy);
        CHECK(zero.block == 0);
        const auto big = parse_chat("/copiar 999999999");
        REQUIRE(big.type == ParsedCommand::Type::Copy);
        CHECK(big.block == 999999999);
    }
}

TEST_CASE("parse_command detecta errores de uso de /copiar", "[command_parser]") {
    for (const char* input : {"/copiar tres", "/copiar 3 4", "/copiar -1", "/copiar 3x",
                              "/copiar 1234567890"}) {
        INFO(input);
        const auto result = parse_chat(input);
        CHECK(result.type == ParsedCommand::Type::CopyUsage);
        CHECK_FALSE(result.block.has_value());
    }
}

TEST_CASE("parse_command reconoce /guardar", "[command_parser]") {
    SECTION("Sin argumentos") {
        const auto result = parse_chat("/guardar");
        REQUIRE(result.type == ParsedCommand::Type::Save);
        CHECK_FALSE(result.block.has_value());
        CHECK(result.file_name.empty());
    }

    SECTION("Con número") {
        const auto result = parse_chat("/guardar 3");
        REQUIRE(result.type == ParsedCommand::Type::Save);
        CHECK(result.block == 3);
        CHECK(result.file_name.empty());
    }

    SECTION("Con número y nombre") {
        const auto result = parse_chat("/guardar 3 suma.cpp");
        REQUIRE(result.type == ParsedCommand::Type::Save);
        CHECK(result.block == 3);
        CHECK(result.file_name == "suma.cpp");
    }

    SECTION("El nombre se pasa sin validar") {
        const auto result = parse_chat("/guardar 1 ../x");
        REQUIRE(result.type == ParsedCommand::Type::Save);
        CHECK(result.file_name == "../x");
    }
}

TEST_CASE("parse_command detecta errores de uso de /guardar", "[command_parser]") {
    for (const char* input : {"/guardar suma.cpp", "/guardar 3 suma.cpp extra",
                              "/guardar 3 mi archivo.cpp", "/guardar uno dos"}) {
        INFO(input);
        const auto result = parse_chat(input);
        CHECK(result.type == ParsedCommand::Type::SaveUsage);
        CHECK_FALSE(result.block.has_value());
        CHECK(result.file_name.empty());
    }
}

TEST_CASE("/copiar y /guardar con solo un espacio final son lo mismo que sin él",
          "[command_parser]") {
    // Tab en el menú de comandos los completa con un espacio ("/copiar "):
    // Enter justo después debe hacer lo mismo que el comando solo.
    for (const char* name : {"/copiar", "/guardar"}) {
        INFO(name);
        const auto plain = parse_chat(name);
        const auto spaced = parse_chat(std::string{name} + " ");
        CHECK(spaced.type == plain.type);
        CHECK(spaced.type != ParsedCommand::Type::Normal);
        CHECK(spaced.type != ParsedCommand::Type::CopyUsage);
        CHECK(spaced.type != ParsedCommand::Type::SaveUsage);
        CHECK(spaced.block == plain.block);
        CHECK_FALSE(spaced.block.has_value());
        CHECK(spaced.file_name == plain.file_name);
        CHECK(spaced.file_name.empty());
        CHECK(spaced.text == plain.text);
    }
}

TEST_CASE("parse_command reconoce /exportar", "[command_parser]") {
    CHECK(parse_chat("/exportar").type == ParsedCommand::Type::Export);
    CHECK(parse_chat("  /exportar  ").type == ParsedCommand::Type::Export);
    CHECK(parse_chat("/exportar todo").type == ParsedCommand::Type::ExportUsage);
}

TEST_CASE("parse_command manda como mensaje normal los comandos parecidos", "[command_parser]") {
    for (const char* input : {"/copiarx", "/copiarx 3", "/guardarlo", "/exportarlo", "/Copiar",
                              "copiar 3", "/copia"}) {
        INFO(input);
        const auto result = parse_chat(input);
        CHECK(result.type == ParsedCommand::Type::Normal);
        CHECK(result.text == input);
    }
}

TEST_CASE("parse_command acepta saltos de línea entre argumentos", "[command_parser]") {
    SECTION("/copiar y /guardar") {
        const auto copy = parse_chat("/copiar\n2");
        REQUIRE(copy.type == ParsedCommand::Type::Copy);
        REQUIRE(copy.block == 2);

        const auto save = parse_chat("/guardar 3\nnotas.txt\n");
        REQUIRE(save.type == ParsedCommand::Type::Save);
        REQUIRE(save.block == 3);
        REQUIRE(save.file_name == "notas.txt");

        REQUIRE(parse_chat("/guardar\n3\nuno\ndos").type == ParsedCommand::Type::SaveUsage);
    }
    SECTION("/exportar con un salto final sigue sin argumentos") {
        REQUIRE(parse_chat("/exportar\n").type == ParsedCommand::Type::Export);
        REQUIRE(parse_chat("/exportar\nalgo").type == ParsedCommand::Type::ExportUsage);
    }
    SECTION("/buscar seguido de un salto de línea") {
        const auto result = parse_chat("/buscar\nclima en Toluca");
        REQUIRE(result.type == ParsedCommand::Type::Search);
        REQUIRE(result.text == "clima en Toluca");
        REQUIRE(parse_chat("/buscar\n\n").type == ParsedCommand::Type::SearchEmpty);
    }
    SECTION("La consulta de /buscar va en una línea") {
        const auto result = parse_chat("/buscar receta de\nmole\npoblano\n");
        REQUIRE(result.type == ParsedCommand::Type::Search);
        REQUIRE(result.text == "receta de mole poblano");
    }
    SECTION("Un mensaje normal conserva sus saltos") {
        const auto result = parse_chat("hola\n\nmundo");
        REQUIRE(result.type == ParsedCommand::Type::Normal);
        REQUIRE(result.text == "hola\n\nmundo");
    }
}

TEST_CASE("parse_command reconoce /model en todas sus formas", "[command_parser]") {
    using Type = ParsedCommand::Type;
    SECTION("Sin argumentos abre el selector") {
        const auto result = parse_chat("/model");
        REQUIRE(result.type == Type::Model);
        CHECK_FALSE(result.provider.has_value());
        CHECK_FALSE(result.model.has_value());
    }
    SECTION("Una palabra que es un proveedor, sin distinguir mayúsculas") {
        const auto result = parse_chat("/model  OLLama ");
        REQUIRE(result.type == Type::Model);
        CHECK(result.provider == "ollama");
        CHECK_FALSE(result.model.has_value());
    }
    SECTION("Una palabra que no es un proveedor es el modelo, con / y :") {
        auto result = parse_chat("/model z-ai/glm5");
        REQUIRE(result.type == Type::Model);
        CHECK_FALSE(result.provider.has_value());
        CHECK(result.model == "z-ai/glm5");
        result = parse_chat("/model deepseek/deepseek-v4:free");
        CHECK(result.model == "deepseek/deepseek-v4:free");
        // El modelo se queda como se escribió.
        CHECK(parse_chat("/model Qwen3:8B").model == "Qwen3:8B");
    }
    SECTION("Proveedor y modelo") {
        const auto result = parse_chat("/model Gemini gemini-2.5-flash");
        REQUIRE(result.type == Type::Model);
        CHECK(result.provider == "gemini");
        CHECK(result.model == "gemini-2.5-flash");
    }
    SECTION("Saltos de línea como separadores") {
        const auto result = parse_chat("/model\nollama\n qwen3:8b\n");
        REQUIRE(result.type == Type::Model);
        CHECK(result.provider == "ollama");
        CHECK(result.model == "qwen3:8b");
    }
    SECTION("Dos palabras con un proveedor desconocido") {
        const auto result = parse_chat("/model nvidiaa z-ai/glm5");
        REQUIRE(result.type == Type::ModelUsage);
        CHECK(result.text == "Proveedor desconocido: nvidiaa. Escribe /model para ver la lista.");
    }
    SECTION("Tres palabras o más") {
        const auto result = parse_chat("/model ollama qwen3 extra");
        REQUIRE(result.type == Type::ModelUsage);
        CHECK(result.text == kModelUsage);
    }
    SECTION("/modelx no es el comando") {
        CHECK(parse_chat("/modelx a").type == Type::Normal);
        CHECK(parse_chat("/models").type == Type::Normal);
    }
}

TEST_CASE("Parser: /model con el id de un endpoint, solo o con custom:", "[command_parser][model]") {
    const chatbot::cli::EndpointLookup lookup = [](std::string_view id) {
        return id == "omniroute";
    };
    auto parsed = parse_chat("/model OmniRoute qwen3", lookup);
    CHECK(parsed.type == ParsedCommand::Type::Model);
    CHECK(parsed.provider == "custom:omniroute");
    CHECK(parsed.model == "qwen3");
    parsed = parse_chat("/model custom:OmniRoute qwen3", lookup);
    CHECK(parsed.provider == "custom:omniroute");
    CHECK(parsed.model == "qwen3");
    parsed = parse_chat("/model omniroute", lookup);
    CHECK(parsed.provider == "custom:omniroute");
    CHECK_FALSE(parsed.model.has_value());
    // "custom:" siempre es un proveedor (Session dice si existe).
    parsed = parse_chat("/model custom:otro");
    CHECK(parsed.provider == "custom:otro");
    // Sin endpoint con ese id, una palabra sola es el modelo; dos, un error de uso.
    parsed = parse_chat("/model omniroute");
    CHECK_FALSE(parsed.provider.has_value());
    CHECK(parsed.model == "omniroute");
    parsed = parse_chat("/model lab m", lookup);
    CHECK(parsed.type == ParsedCommand::Type::ModelUsage);
    CHECK(parsed.text == "Proveedor desconocido: lab. Escribe /model para ver la lista.");
}

TEST_CASE("parse_command en modo código no reconoce los comandos del chat", "[command_parser]") {
    for (const char* input : {"/buscar clima", "/copiar 2", "/guardar", "/exportar"}) {
        INFO(input);
        const auto chat = parse_chat(input);
        CHECK(chat.type != ParsedCommand::Type::Normal);
        const auto code = parse_command(input, {}, CommandMode::Code);
        CHECK(code.type == ParsedCommand::Type::Normal);
        CHECK(code.text == input);
    }
    CHECK(parse_command("  /copiar\n2 ", {}, CommandMode::Code).text == "/copiar\n2");
}

TEST_CASE("parse_command: /clear y /ayuda solo en modo código; /model en los dos",
          "[command_parser]") {
    using Type = ParsedCommand::Type;
    const auto code = [](std::string_view input) {
        return parse_command(input, {}, CommandMode::Code);
    };
    CHECK(code("/clear").type == Type::Clear);
    CHECK(code("  /clear \n").type == Type::Clear);
    CHECK(code("/clear todo").type == Type::ClearUsage);
    CHECK(code("/ayuda").type == Type::Help);
    CHECK(code("/ayuda teclas").type == Type::HelpUsage);
    CHECK(code("/clearx").type == Type::Normal);
    // En el chat no existen: van al modelo tal cual.
    CHECK(parse_chat("/clear").type == Type::Normal);
    CHECK(parse_chat("/ayuda").text == "/ayuda");
    // /model, igual en los dos.
    const ParsedCommand model = code("/model ollama llama3");
    CHECK(model.type == Type::Model);
    CHECK(model.provider == "ollama");
    CHECK(model.model == "llama3");
    CHECK(code("/model a b c").type == Type::ModelUsage);
}
