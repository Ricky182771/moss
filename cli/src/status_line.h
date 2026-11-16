#ifndef CHATBOT_CLI_STATUS_LINE_H
#define CHATBOT_CLI_STATUS_LINE_H

#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace chatbot::cli {

/// Una parte de la línea de estado.
struct StatusItem {
    std::string text;
    ftxui::Decorator style = ftxui::nothing; ///< Vacío ({}): sin estilo.
    /// Indicador ("Pensando…", "↓ Hay más abajo"): tiene su ancho reservado
    /// y nunca se recorta.
    bool reserved = false;
};

/// La línea de estado en una fila de width columnas, con las partes en su
/// orden y separadas por tres espacios. Primero se reserva el ancho de los
/// indicadores (si ni ellos caben, se quitan desde el último, nunca el
/// primero); las demás partes (avisos) usan lo que sobra, en orden, y la que
/// no cabe termina en "…" (las siguientes no se muestran; con menos de 2
/// columnas libres, tampoco ella). Sin partes, una fila vacía: la línea mide
/// siempre una fila.
[[nodiscard]] ftxui::Element status_line(const std::vector<StatusItem>& items, int width);

/// La línea de estado de tres zonas del modo código.
struct StatusZones {
    StatusItem left;     ///< El modo, a la izquierda (reserved no cuenta).
    std::string activity; ///< "Pensando…", primero en el centro; vacío sin turno. Nunca se recorta.
    std::string hint;    ///< Va pegado a activity (" (Esc para cancelar)") solo si cabe todo.
    std::vector<StatusItem> center; ///< Después de activity: avisos e indicadores (status_line).
    StatusItem right;    ///< El modelo, a la derecha (reserved no cuenta).
};

/// left, el centro y right separados por tres espacios, right pegado al borde derecho. Si no
/// cabe todo a su ancho natural, se quita primero hint, luego right y luego left; con lo que
/// queda, el centro es status_line (recorta los avisos y, al final, quita indicadores). Mientras
/// left se ve, activity (con hint si cabe) va centrada en el ancho, o lo más cerca del centro
/// que deje libres a left, los avisos de center (pegados a left) y right.
[[nodiscard]] ftxui::Element zoned_status_line(const StatusZones& zones, int width);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_STATUS_LINE_H
