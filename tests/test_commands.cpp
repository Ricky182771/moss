#include <catch2/catch_test_macros.hpp>

#include "command_parser.h"
#include "commands.h"

#include <string>
#include <string_view>
#include <vector>

using chatbot::cli::CommandAvailability;
using chatbot::cli::CommandContext;
using chatbot::cli::CommandInfo;
using chatbot::cli::kCommands;
using chatbot::cli::ParsedCommand;

namespace {

/// Disponibilidad del comando name (que debe estar en el registro).
CommandAvailability availability_of(std::string_view name, const CommandContext& context) {
    const std::vector<CommandAvailability> all = chatbot::cli::command_availability(context);
    REQUIRE(all.size() == kCommands.size());
    for (std::size_t i = 0; i < kCommands.size(); ++i) {
        if (kCommands[i].name == name) {
            return all[i];
        }
    }
    FAIL("No está en el registro: " << name);
    return {};
}

/// Todo disponible.
CommandContext ready() { return CommandContext{true, true, true, true, false, false}; }

} // namespace

TEST_CASE("Cada comando del registro lo reconoce parse_command", "[commands]") {
    for (const CommandInfo& command : kCommands) {
        INFO(command.name);
        CHECK(command.name.front() == '/');
        // En su modo (los de los dos, en el del chat).
        const chatbot::cli::CommandMode mode = command.mode == chatbot::cli::CommandMode::Both
                                                   ? chatbot::cli::CommandMode::Chat
                                                   : command.mode;
        const ParsedCommand parsed = chatbot::cli::parse_command(command.name, {}, mode);
        CHECK(parsed.type != ParsedCommand::Type::Normal);
        CHECK(chatbot::cli::command_name(parsed.type) == command.name);
        // Con un argumento también es el mismo comando (bien o mal usado).
        const ParsedCommand with_argument =
            chatbot::cli::parse_command(std::string{command.name} + " 1", {}, mode);
        CHECK(chatbot::cli::command_name(with_argument.type) == command.name);
    }
}

TEST_CASE("Ningún tipo de ParsedCommand queda sin entrada en el registro", "[commands]") {
    using Type = ParsedCommand::Type;
    for (const Type type : {Type::Search, Type::SearchEmpty, Type::Copy, Type::CopyUsage,
                            Type::Save, Type::SaveUsage, Type::Export, Type::ExportUsage,
                            Type::Model, Type::ModelUsage, Type::Clear, Type::ClearUsage,
                            Type::Help, Type::HelpUsage}) {
        INFO(static_cast<int>(type));
        CHECK(chatbot::cli::find_command(chatbot::cli::command_name(type)) != nullptr);
    }
    CHECK(chatbot::cli::command_name(Type::Normal).empty());
}

TEST_CASE("Los argumentos del registro coinciden con los avisos de uso", "[commands]") {
    const auto usage = [](std::string_view name) {
        const CommandInfo* command = chatbot::cli::find_command(name);
        REQUIRE(command != nullptr);
        return "Uso: " + std::string{command->name} + " " + std::string{command->arguments};
    };
    CHECK(usage("/buscar") == chatbot::cli::kSearchUsage);
    CHECK(usage("/copiar") == chatbot::cli::kCopyUsage);
    CHECK(usage("/guardar") == chatbot::cli::kSaveUsage);
    CHECK(usage("/model") == chatbot::cli::kModelUsage);
    for (const char* name : {"/exportar", "/clear", "/ayuda"}) {
        const CommandInfo* command = chatbot::cli::find_command(name);
        REQUIRE(command != nullptr);
        CHECK(command->arguments.empty());
    }
    CHECK(chatbot::cli::kClearUsage == "Uso: /clear (sin argumentos)");
    CHECK(chatbot::cli::kHelpUsage == "Uso: /ayuda (sin argumentos)");
}

TEST_CASE("Registro: nombres en minúsculas, sin repetir y descripciones cortas", "[commands]") {
    for (std::size_t i = 0; i < kCommands.size(); ++i) {
        const CommandInfo& command = kCommands[i];
        INFO(command.name);
        for (const char c : command.name) {
            CHECK((c == '/' || (c >= 'a' && c <= 'z')));
        }
        CHECK(!command.description.empty());
        // Unas 50 columnas (las descripciones son de caracteres angostos;
        // cada acento ocupa 2 bytes).
        CHECK(command.description.size() <= 56);
        for (std::size_t j = i + 1; j < kCommands.size(); ++j) {
            CHECK(command.name != kCommands[j].name);
        }
    }
    CHECK(chatbot::cli::find_command("/buscar")->needs_argument);
    CHECK_FALSE(chatbot::cli::find_command("/copiar")->needs_argument);
    CHECK_FALSE(chatbot::cli::find_command("/guardar")->needs_argument);
    CHECK_FALSE(chatbot::cli::find_command("/exportar")->needs_argument);
    CHECK_FALSE(chatbot::cli::find_command("/model")->needs_argument);
    CHECK(chatbot::cli::find_command("/BUSCAR") == nullptr);
    CHECK(chatbot::cli::find_command("/bus") == nullptr);
}

TEST_CASE("Disponibilidad de los comandos", "[commands]") {
    SECTION("Con todo listo, todos disponibles y sin nota") {
        for (const CommandAvailability& state : chatbot::cli::command_availability(ready())) {
            CHECK(state.available);
            CHECK(state.note.empty());
        }
    }
    SECTION("/buscar sin proveedor o sin key") {
        CommandContext context = ready();
        context.has_search_key = false;
        CHECK_FALSE(availability_of("/buscar", context).available);
        CHECK(availability_of("/buscar", context).note == "falta la key de búsqueda (F2)");
        context.configured = false;
        CHECK(availability_of("/buscar", context).note == "falta configurar el proveedor (F2)");
        // Los demás no necesitan proveedor ni key.
        CHECK(availability_of("/copiar", context).available);
        CHECK(availability_of("/exportar", context).available);
    }
    SECTION("/copiar y /guardar sin bloques de código") {
        CommandContext context = ready();
        context.has_code_blocks = false;
        CHECK(availability_of("/copiar", context).note == "no hay bloques de código");
        CHECK(availability_of("/guardar", context).note == "no hay bloques de código");
        CHECK(availability_of("/exportar", context).available);
    }
    SECTION("/model: sin configurar sí; con CHAT_MODEL o en curso, no") {
        CommandContext context = ready();
        context.configured = false;
        context.has_search_key = false;
        context.has_code_blocks = false;
        context.has_turns = false;
        CHECK(availability_of("/model", context).available);
        context.busy = true;
        CHECK(availability_of("/model", context).note == "espera a que termine la respuesta");
        context.model_locked = true;
        CHECK(availability_of("/model", context).note == "fijado por CHAT_MODEL");
        context.busy = false;
        CHECK_FALSE(availability_of("/model", context).available);
    }
    SECTION("/exportar con la conversación vacía") {
        CommandContext context = ready();
        context.has_turns = false;
        CHECK(availability_of("/exportar", context).note == "la conversación está vacía");
    }
    SECTION("Con una respuesta en curso, ninguno salvo /ayuda") {
        CommandContext context = ready();
        context.busy = true;
        const std::vector<CommandAvailability> all = chatbot::cli::command_availability(context);
        for (std::size_t i = 0; i < kCommands.size(); ++i) {
            INFO(kCommands[i].name);
            if (kCommands[i].name == "/ayuda") {
                CHECK(all[i].available); // Solo agrega una entrada: sirve durante el turno.
                continue;
            }
            CHECK_FALSE(all[i].available);
            CHECK(all[i].note == "espera a que termine la respuesta");
        }
    }
    SECTION("Modo código: /clear espera al turno; /model también y respeta CHAT_MODEL") {
        // Lo que llena CodeMode::command_context: configured, busy y model_locked.
        CommandContext context{};
        context.configured = true;
        CHECK(availability_of("/clear", context).available);
        CHECK(availability_of("/model", context).available);
        CHECK(availability_of("/ayuda", context).available);
        context.busy = true;
        CHECK(availability_of("/clear", context).note == "espera a que termine la respuesta");
        CHECK(availability_of("/model", context).note == "espera a que termine la respuesta");
        context.model_locked = true;
        CHECK(availability_of("/model", context).note == "fijado por CHAT_MODEL");
        CHECK(availability_of("/clear", context).note == "espera a que termine la respuesta");
        context.busy = false;
        CHECK(availability_of("/clear", context).available);
        CHECK_FALSE(availability_of("/model", context).available);
    }
    SECTION("Lo que no se arregla esperando va antes que la respuesta en curso") {
        CommandContext context{};
        context.busy = true;
        CHECK(availability_of("/buscar", context).note == "falta configurar el proveedor (F2)");
        CHECK(availability_of("/copiar", context).note == "no hay bloques de código");
        CHECK(availability_of("/exportar", context).note == "la conversación está vacía");
    }
}

TEST_CASE("Los comandos de cada modo, en el orden del registro", "[commands]") {
    using chatbot::cli::CommandMode;
    const auto names_in = [](CommandMode mode) {
        std::vector<std::string_view> names;
        for (const CommandInfo& command : kCommands) {
            if (chatbot::cli::command_in_mode(command.mode, mode)) {
                names.push_back(command.name);
            }
        }
        return names;
    };
    CHECK(names_in(CommandMode::Chat) ==
          std::vector<std::string_view>{"/buscar", "/copiar", "/guardar", "/exportar", "/model"});
    CHECK(names_in(CommandMode::Code) ==
          std::vector<std::string_view>{"/model", "/clear", "/ayuda"});
    CHECK(chatbot::cli::command_in_mode(CommandMode::Both, CommandMode::Chat));
    CHECK(chatbot::cli::command_in_mode(CommandMode::Both, CommandMode::Code));
    CHECK_FALSE(chatbot::cli::command_in_mode(CommandMode::Code, CommandMode::Chat));
}
