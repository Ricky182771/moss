#ifndef CHATBOT_TOOL_CALL_ACCUMULATOR_H
#define CHATBOT_TOOL_CALL_ACCUMULATOR_H

#include "chatbot/types.h"
#include "sse.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace chatbot {

/// Junta los pedazos de delta.tool_calls de una respuesta en llamadas
/// completas (guias/18-moss-code.md, 3.3). Pura: sin E/S ni estado global.
/// Con index, cada pedazo va a la llamada más reciente de ese index, salvo
/// que traiga un id distinto del que esa llamada ya tiene: entonces abre otra
/// (hay servidores que mandan todas con index 0). Sin index, un id que no se
/// ha visto abre otra llamada y uno visto va a la suya; sin index ni id se
/// suma a la última, salvo que traiga name y la última ya tenga el suyo:
/// entonces abre otra. De cada llamada se toman el primer id y el primer name
/// no vacíos, y los arguments se concatenan sin validarlos. Un pedazo sin
/// nada se ignora.
class ToolCallAccumulator {
public:
    /// Una llamada que ya se puede anunciar (StreamDelta::Kind::ToolCallStarted).
    struct Started {
        std::size_t position; ///< Su lugar en finish().
        std::string name;
    };

    void add(const sse::ToolCallChunk& chunk);

    /// La siguiente llamada por anunciar: la primera sin anunciar, si ya
    /// tiene name. Cada llamada sale una vez y en orden de aparición: una sin
    /// name detiene a las que siguen hasta que llegue.
    [[nodiscard]] std::optional<Started> next_started();

    /// Las llamadas en orden de aparición, con los ids de assign_free_ids.
    /// arguments vacío queda "{}". name puede quedar vacío: quien la ejecute
    /// responde el error.
    [[nodiscard]] std::vector<ToolCall> finish(const std::vector<Message>& history) const;

private:
    struct Entry {
        std::optional<std::size_t> index;
        ToolCall call;
    };
    std::vector<Entry> calls_;
    std::size_t announced_ = 0;
};

/// validate_tool_history no admite ids repetidos: un id de calls vacío, que
/// ya usa una llamada de history o que repite el de una llamada anterior de
/// calls pasa a ser el primer call_moss_<n> (n desde 1) que nadie usa. El id
/// solo empareja la llamada con su resultado, así que renombrarlo es seguro.
/// Lo demás no cambia. Pura.
void assign_free_ids(std::vector<ToolCall>& calls, const std::vector<Message>& history);

} // namespace chatbot

#endif // CHATBOT_TOOL_CALL_ACCUMULATOR_H
