#ifndef CHATBOT_HISTORY_H
#define CHATBOT_HISTORY_H

#include "chatbot/types.h"

#include <cstddef>
#include <vector>

namespace chatbot {

/// Resultado de trim_history: el historial recortado y cuántos mensajes se
/// quitaron.
struct TrimResult {
    std::vector<Message> messages;
    std::size_t dropped = 0;
};

/// Recorta el historial para que el total de bytes UTF-8 (content, reasoning,
/// tool_call_id y el id, name y arguments de cada tool_calls; aproximadamente
/// caracteres) no pase de limit_bytes. Función pura: el núcleo no guarda el
/// historial (sección 6).
/// - La unidad es el grupo: un User con todo lo que le sigue hasta el
///   siguiente User (asistentes, con o sin tool_calls, y sus Tool).
/// - Los mensajes System iniciales y el último grupo (el que contiene el
///   último mensaje, aunque sea un Tool) nunca se quitan ni se parten.
/// - Se quitan grupos completos, los más viejos primero, para que tras el
///   sistema lo primero siga siendo User y ningún Tool quede sin su llamada
///   ni una llamada sin sus Tool: si la entrada pasa validate_tool_history,
///   la salida también.
/// - Se para al caber, o cuando solo quedan el sistema y el último grupo
///   (aunque sigan excediendo). dropped cuenta mensajes, no grupos.
/// - limit_bytes == 0 devuelve todo sin cambios.
/// No lanza excepciones (salvo falta de memoria al copiar).
[[nodiscard]] TrimResult trim_history(const std::vector<Message>& messages,
                                      std::size_t limit_bytes);

} // namespace chatbot

#endif // CHATBOT_HISTORY_H
