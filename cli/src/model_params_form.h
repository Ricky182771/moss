#ifndef CHATBOT_CLI_MODEL_PARAMS_FORM_H
#define CHATBOT_CLI_MODEL_PARAMS_FORM_H

#include "chatbot/model_params.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// Lo que dio Session::check_model_params.
struct ModelParamsCheck {
    std::vector<FieldProblem> problems; ///< Por campo ("extra" incluido); vacío si se puede guardar.
    /// El cuerpo que se mandaría con la conversación abierta (request_preview),
    /// o nullopt si hay problemas.
    std::optional<std::string> preview;
};

/// Sin proveedor o sin modelo: Parámetros y Avanzado no se pueden ajustar.
inline constexpr std::string_view kNoModelForParams =
    "Elige un modelo en Proveedor de IA (o con /model) para ajustarlo.";

/// Las opciones del razonamiento en la lista, en el orden de Reasoning.
inline constexpr std::array<std::string_view, 5> kReasoningChoices{
    "Predeterminado", "Apagado", "Bajo", "Medio", "Alto"};

/// Qué hace cada nivel del razonamiento en ese proveedor (la ayuda de F2).
[[nodiscard]] std::string reasoning_help(std::string_view provider);

/// La etiqueta corta de un campo numérico ("temperature" → "Temperatura").
[[nodiscard]] std::string_view param_label(std::string_view field);

/// El rango de un campo, para mostrarlo junto a él ("0 a 2").
[[nodiscard]] std::string limit_hint(const ParamLimit& limit);

/// El formulario de Parámetros y Avanzado (F2 → Configuración del modelo),
/// sin FTXUI: los campos como texto (vacío = Predeterminado), como los escribe
/// el usuario. Los campos numéricos van en el orden de model_param_limits.
class ModelParamsForm {
public:
    static constexpr std::size_t kNumberFields = 7;

    ModelParamsForm() = default;
    /// Con lo guardado (los números como texto, extra con sangría).
    explicit ModelParamsForm(const ModelParams& saved);

    std::array<std::string, kNumberFields> numbers{};
    int reasoning = 0; ///< Índice en kReasoningChoices.
    std::string extra;

    /// Los ajustes del formulario. Un texto que no es número (o no es entero
    /// donde debe) queda en Predeterminado y su error va a problems.
    [[nodiscard]] ModelParams draft(std::vector<FieldProblem>* problems = nullptr) const;
    /// Cambió algo desde lo guardado (o desde mark_saved).
    [[nodiscard]] bool dirty() const;
    /// Todo a Predeterminado (Restaurar todo); hay que guardar.
    void restore_all();
    /// Lo de ahora es lo guardado.
    void mark_saved();

private:
    std::array<std::string, kNumberFields> saved_numbers_{};
    int saved_reasoning_ = 0;
    std::string saved_extra_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_MODEL_PARAMS_FORM_H
