#ifndef CHATBOT_CLI_COMMAND_PARSER_H
#define CHATBOT_CLI_COMMAND_PARSER_H

#include <optional>
#include <string>
#include <string_view>

namespace chatbot::cli {

/// Resultado del parseo de un comando.
struct ParsedCommand {
    enum class Type {
        Normal,      ///< Texto normal (no es un comando).
        Search,      ///< Comando /buscar con una consulta.
        SearchEmpty, ///< Comando /buscar sin consulta (error de uso).
        Copy,        ///< /copiar o /copiar <n>.
        CopyUsage,   ///< /copiar con argumentos que no son un número (error de uso).
        Save,        ///< /guardar, /guardar <n> o /guardar <n> <nombre>.
        SaveUsage,   ///< /guardar con argumentos de más o mal formados (error de uso).
        Export,      ///< /exportar.
        ExportUsage, ///< /exportar con argumentos (error de uso).
        Model,       ///< /model [proveedor] [modelo].
        ModelUsage,  ///< /model mal formado (error de uso; el aviso va en text).
    };

    Type type = Type::Normal;
    std::string text; ///< Para Normal: el texto completo. Para Search: la consulta.
    /// Copy y Save: número del bloque que escribió el usuario (puede estar
    /// fuera de rango; eso lo revisa quien lo usa), o nullopt para el último.
    std::optional<int> block;
    /// Save: nombre que escribió el usuario, o vacío para el nombre por
    /// defecto. Sin validar (ver validate_file_name en downloads.h).
    std::string file_name;
    /// Model: id del proveedor (de la tabla, en minúsculas), si se escribió.
    std::optional<std::string> provider;
    /// Model: id del modelo tal como se escribió, si se escribió.
    std::optional<std::string> model;
};

/// Avisos de uso de /buscar, /copiar, /guardar y /exportar. Los argumentos
/// coinciden con los del registro de comandos (commands.h).
inline constexpr std::string_view kSearchUsage = "Uso: /buscar <consulta>";
inline constexpr std::string_view kCopyUsage = "Uso: /copiar [número de bloque]";
inline constexpr std::string_view kSaveUsage = "Uso: /guardar [número de bloque [nombre]]";
inline constexpr std::string_view kExportUsage = "Uso: /exportar (sin argumentos)";
inline constexpr std::string_view kModelUsage = "Uso: /model [proveedor] [modelo]";

/// "Proveedor desconocido: <word>. Escribe /model para ver la lista."
[[nodiscard]] std::string unknown_provider_message(std::string_view word);

/// Parsea el texto de entrada para detectar un comando.
/// - `/buscar <consulta>`: devuelve Search con la consulta (sin espacios en los
///   extremos y con cada salto de línea cambiado por un espacio).
/// - `/buscar` sin consulta: devuelve SearchEmpty.
/// - `/copiar [n]`, `/guardar [n [nombre]]` y `/exportar`: Copy, Save y
///   Export; con otros argumentos, CopyUsage, SaveUsage y ExportUsage. n son
///   solo dígitos (a lo más 9); los argumentos se separan con espacios o
///   saltos de línea.
/// - `/model [proveedor] [modelo]`: Model. Una sola palabra es el proveedor
///   si coincide con un id de providers() (sin distinguir mayúsculas); si
///   no, el modelo. Con dos, la primera tiene que ser un proveedor; si no, o
///   con tres o más, ModelUsage con el aviso en text.
/// - Cualquier otro texto (incluido `/buscarx`, `/copiarx` u otros comandos): devuelve Normal.
ParsedCommand parse_command(std::string_view input);

}  // namespace chatbot::cli

#endif  // CHATBOT_CLI_COMMAND_PARSER_H
