#ifndef CHATBOT_MODEL_PARAMS_H
#define CHATBOT_MODEL_PARAMS_H

#include "chatbot/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot {

/// Razonamiento: un solo control que el núcleo traduce según el proveedor
/// (reasoning_fields). Default no manda nada.
enum class Reasoning { Default, Off, Low, Medium, High };

/// Ajustes del modelo de "models" en config.json, por proveedor y modelo.
/// Lo que no tiene valor (nullopt, Reasoning::Default, extra vacío) es
/// "Predeterminado del modelo": su llave no se manda ni se guarda.
struct ModelParams {
    Reasoning reasoning = Reasoning::Default;
    std::optional<double> temperature;
    std::optional<double> top_p;
    std::optional<std::int64_t> top_k;
    std::optional<std::int64_t> max_tokens; ///< En "openai" se manda como max_completion_tokens.
    std::optional<double> frequency_penalty;
    std::optional<double> presence_penalty;
    std::optional<std::int64_t> seed;
    /// JSON avanzado: el texto de un objeto, tal como se escribió (vacío:
    /// ninguno). Se mezcla al final con JSON Merge Patch (RFC 7386).
    std::string extra;

    /// Todo en Predeterminado (un extra "{}" o solo espacios cuenta como vacío).
    [[nodiscard]] bool empty() const;
    bool operator==(const ModelParams&) const = default;
};

/// Cómo se traduce el razonamiento en un proveedor: "effort"
/// (reasoning_effort o reasoning.effort), "toggle" (NIM: solo enciende o
/// apaga) o "budget" (Anthropic: presupuesto de tokens).
enum class ReasoningStyle { Effort, Toggle, Budget };
[[nodiscard]] ReasoningStyle reasoning_style(std::string_view provider);
[[nodiscard]] std::string_view reasoning_style_name(ReasoningStyle style);

/// "off" | "low" | "medium" | "high" ("" para Default), y al revés
/// (nullopt si no es ninguno de los cuatro).
[[nodiscard]] std::string_view reasoning_name(Reasoning reasoning);
[[nodiscard]] std::optional<Reasoning> parse_reasoning(std::string_view name);

/// Tabla del razonamiento (guias/07-configuracion.md): las llaves que se
/// agregan al cuerpo por proveedor y nivel, como texto de un objeto JSON
/// compacto ("{}" si no se agrega nada). Único lugar del código con la tabla.
[[nodiscard]] std::string reasoning_fields(std::string_view provider, Reasoning reasoning);

/// Presupuesto de "thinking" de Anthropic para el nivel (1024 / 4096 /
/// 16384), o nullopt con Default y Off.
[[nodiscard]] std::optional<std::int64_t> anthropic_budget(Reasoning reasoning);

/// Rango de un campo numérico, para validar y para que la GUI no lo copie.
struct ParamLimit {
    std::string_view field; ///< Nombre en config.json ("temperature").
    double min = 0;
    double max = 0;
    bool min_exclusive = false; ///< top_p: mayor que 0.
    bool integer = false;
};
/// Los siete campos numéricos en el orden del formulario, con los rangos
/// del proveedor (temperature hasta 1 en "anthropic").
[[nodiscard]] std::vector<ParamLimit> model_param_limits(std::string_view provider);

/// Tope del JSON avanzado, en bytes del texto.
inline constexpr std::size_t kMaxExtraBytes = 8 * 1024;

/// Un error de validación junto a su campo ("temperature", ..., "extra").
struct FieldProblem {
    std::string field;
    std::string message;
    bool operator==(const FieldProblem&) const = default;
};

/// Revisa los ajustes para provider: rangos, el JSON avanzado (objeto
/// válido, como máximo kMaxExtraBytes, UTF-8 válido, sin llaves que pone
/// Moss) y, en Anthropic con razonamiento, que el máximo de salida pase del
/// presupuesto. Vacío si todo está bien.
[[nodiscard]] std::vector<FieldProblem> validate_model_params(const ModelParams& params,
                                                              std::string_view provider);

/// Los ajustes como objeto JSON de "models" (solo lo que no es
/// Predeterminado; "{}" si nada), compacto. extra va como objeto; requiere
/// unos ajustes que pasaron validate_model_params.
[[nodiscard]] std::string model_params_to_json(const ModelParams& params);

/// El JSON avanzado guardado (compacto) con sangría de 2, para editarlo; ""
/// si está vacío, y tal cual si no es JSON.
[[nodiscard]] std::string format_extra(std::string_view extra);

/// Lo que dio leer un objeto de ajustes (de config.json o del RPC).
struct ParsedModelParams {
    ModelParams params;
    /// Campos con un tipo que no corresponde (quedan en Predeterminado).
    std::vector<std::string> invalid_fields;
    /// Llaves que no son de la tabla (se ignoran).
    std::vector<std::string> unknown_fields;
};
/// Lee un objeto con los nombres de config.json: null o ausente es
/// Predeterminado; extra es un objeto (o null). Solo revisa tipos, no
/// rangos (eso es validate_model_params). Error Config si json no es un
/// objeto JSON.
[[nodiscard]] Result<ParsedModelParams> model_params_from_json(std::string_view json);

/// Vista previa del cuerpo que se mandaría (sangría de 2, en el orden de la
/// petición), con "messages" cambiado por "[…N mensajes…]". La key va en la
/// cabecera: nunca aparece aquí. Requiere ajustes válidos.
[[nodiscard]] std::string request_preview(std::string_view model, std::string_view provider,
                                          const ModelParams& params, std::size_t message_count);

/// Lee los ajustes de provider y model de "models" en config.json, como
/// load_config: lo inválido queda en Predeterminado con su aviso en
/// warnings. Sin archivo, sin proveedor o sin modelo: sin ajustes. Error
/// Config si el archivo no es un objeto JSON válido.
[[nodiscard]] Result<ModelParams> load_model_params(const std::string& path,
                                                    std::string_view provider,
                                                    std::string_view model,
                                                    std::vector<std::string>& warnings);

/// Escribe los ajustes de provider y model en "models" de forma atómica,
/// conservando las demás llaves (el escritor de save_config_file). Sin
/// ajustes, quita el modelo, y el proveedor si se queda sin modelos (y
/// "models" si queda vacío). Si "models" o el proveedor no son objetos, no
/// toca nada y devuelve el error Config. Requiere ajustes válidos.
[[nodiscard]] std::optional<ChatError> save_model_params(const std::string& path,
                                                         std::string_view provider,
                                                         std::string_view model,
                                                         const ModelParams& params);

/// Quita de "models" todos los ajustes de provider (al borrar un endpoint).
/// Si "models" no es un objeto, no lo toca.
[[nodiscard]] std::optional<ChatError> remove_provider_params(const std::string& path,
                                                              std::string_view provider);

} // namespace chatbot

#endif // CHATBOT_MODEL_PARAMS_H
