#ifndef CHATBOT_CODE_SRC_OUTPUT_CAP_H
#define CHATBOT_CODE_SRC_OUTPUT_CAP_H

#include <cstddef>
#include <string>
#include <string_view>

namespace chatbot::code {

/// Tope por defecto del resultado de una herramienta, en caracteres (guias/18-moss-code.md,
/// "Tope de resultado").
inline constexpr std::size_t kDefaultToolOutputCap = 30000;

/// text tal cual si tiene hasta max_chars caracteres (puntos de código UTF-8, no
/// bytes; un byte inválido cuenta como uno). Si no, la cabeza (max_chars / 2
/// caracteres) y la cola (el resto del tope) con una línea "[… N lines omitted …]"
/// en medio, en inglés porque la lee el modelo; N cuenta las líneas que el recorte
/// toca. Corta en límites de línea cuando puede y nunca a media secuencia UTF-8; un
/// texto de una sola línea se recorta por caracteres. Para cualquier entrada de
/// bytes, la cola empieza donde termina la cabeza o después, y el resultado pasa de
/// max_chars solo por la marca y sus saltos de línea; si text es UTF-8 válido, el
/// resultado también.
[[nodiscard]] std::string cap_tool_output(std::string_view text,
                                          std::size_t max_chars = kDefaultToolOutputCap);

/// Una línea de un archivo para el modelo (read, grep): sin '\r' final, UTF-8 válido (U+FFFD por
/// cada byte que no lo es) y, si pasa de max_chars caracteres, recortada con " [… line
/// truncated]".
[[nodiscard]] std::string shown_line(std::string_view line, std::size_t max_chars);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_OUTPUT_CAP_H
