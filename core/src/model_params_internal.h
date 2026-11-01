#ifndef CHATBOT_MODEL_PARAMS_INTERNAL_H
#define CHATBOT_MODEL_PARAMS_INTERNAL_H

#include "chatbot/model_params.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace chatbot {

/// model_params_from_json sobre un objeto ya leído.
[[nodiscard]] ParsedModelParams parse_params_object(const nlohmann::ordered_json& object);

/// model_params_to_json como objeto.
[[nodiscard]] nlohmann::ordered_json params_object(const ModelParams& params);

/// El cuerpo de /chat/completions en su orden: model, stream, messages, los
/// parámetros del formulario, la traducción del razonamiento y, al final,
/// extra mezclado con JSON Merge Patch. Lo comparten la petición y
/// request_preview, para que la vista previa sea el cuerpo exacto.
[[nodiscard]] nlohmann::ordered_json request_json(std::string_view model, bool stream,
                                                  nlohmann::ordered_json messages,
                                                  std::string_view provider,
                                                  const ModelParams& params);

} // namespace chatbot

#endif // CHATBOT_MODEL_PARAMS_INTERNAL_H
