#ifndef CHATBOT_CLI_COMMAND_MODE_H
#define CHATBOT_CLI_COMMAND_MODE_H

namespace chatbot::cli {

/// El modo de la app en que existe un comando (o desde el que se parsea).
enum class CommandMode { Chat, Code, Both };

/// true si un comando de modo declared existe en el modo current (el de la
/// pantalla): el mismo, o Both.
[[nodiscard]] constexpr bool command_in_mode(CommandMode declared, CommandMode current) {
    return declared == CommandMode::Both || declared == current;
}

} // namespace chatbot::cli

#endif // CHATBOT_CLI_COMMAND_MODE_H
