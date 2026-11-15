#ifndef CHATBOT_SRC_SSE_H
#define CHATBOT_SRC_SSE_H

#include "chatbot/error.h"
#include "chatbot/result.h"
#include "chatbot/types.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace chatbot {
namespace sse {

/// Estado del parser de eventos SSE (subconjunto usado por OpenAI/NIM,
/// decisión del usuario para el hito 2). Se alimenta con los trozos crudos
/// que llegan de la red y va entregando eventos completos.
class Parser {
public:
    /// Añade bytes crudos del cuerpo de la respuesta. Los eventos completos
    /// quedan en una cola interna, listos para extraerse con next_event().
    void feed(std::string_view raw);

    /// Extrae el siguiente evento completo en orden de llegada, o nullopt si
    /// aún no hay ninguno. Los eventos se separan por una línea en blanco;
    /// los datos multilínea se unen con '\n'.
    [[nodiscard]] std::optional<std::string> next_event();

    /// Indica si hay datos acumulados que aún no forman un evento completo.
    [[nodiscard]] bool has_partial_data() const;

private:
    std::string buffer_;
    std::deque<std::string> pending_;
};

/// Tipo de evento decodificado.
enum class EventType {
    Delta,  ///< Data payload con contenido para el cliente.
    Done,   ///< Sentinela data: [DONE]: fin del flujo.
    Comment ///< Línea(s) que solo contienen comentarios; se ignora.
};

/// Evento SSE ya decodificado.
struct Event {
    EventType type = EventType::Comment;
    std::string data; ///< Payload unido con '\n' (vacío en Comment y Done).
};

/// Decodifica el payload crudo de un evento: extrae las líneas data:, ignora
/// comentarios y campos no usados (event/id/retry). Devuelve Comment si el
/// evento no tiene datos. BadResponse si no se puede interpretar.
[[nodiscard]] Result<Event> decode_event(const std::string& raw);

/// Un pedazo de choices[0].delta.tool_calls (guias/18-moss-code.md, 3.3).
/// Un campo ausente o con tipo equivocado queda vacío (index: nullopt si no
/// es un entero no negativo).
struct ToolCallChunk {
    std::optional<std::size_t> index;
    std::string id;
    std::string name;      ///< function.name
    std::string arguments; ///< function.arguments
};

/// Interpreta el data payload de un chunk de OpenAI y extrae el incremento
/// de texto (choices[0].delta.content). Nullopt si este chunk no aporta
/// texto (por ejemplo, el último, que cierra con delta vacío y finish_reason).
/// BadResponse si el JSON está roto o la estructura es inesperada.
/// Si finish_reason no es nulo y el chunk trae choices[0].finish_reason como
/// cadena, se escribe ahí; si es null o falta, no se toca.
/// Si reasoning no es nulo, recibe el pensamiento del chunk:
/// delta.reasoning_content o, si no es cadena, delta.reasoning (OpenRouter).
/// Un campo de pensamiento que no es cadena se ignora, sin error; sin
/// pensamiento queda vacío. Si reasoning_key no es nulo, recibe la llave de
/// ese pensamiento; sin pensamiento no se toca.
/// Si tool_calls no es nulo, recibe un pedazo por cada objeto de
/// delta.tool_calls (vacío si no hay). Como con el pensamiento, lo que tiene
/// tipo equivocado se ignora sin error: tool_calls que no es arreglo, un
/// elemento que no es objeto o un campo del pedazo.
[[nodiscard]] Result<std::optional<std::string>> decode_openai_chunk(
    const std::string& data, std::string* finish_reason = nullptr,
    std::string* reasoning = nullptr, ReasoningKey* reasoning_key = nullptr,
    std::vector<ToolCallChunk>* tool_calls = nullptr);

} // namespace sse
} // namespace chatbot

#endif // CHATBOT_SRC_SSE_H
