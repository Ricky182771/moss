#ifndef CHATBOT_CHAT_CLIENT_H
#define CHATBOT_CHAT_CLIENT_H

#include "chatbot/cancel_token.h"
#include "chatbot/completion_info.h"
#include "chatbot/config.h"
#include "chatbot/result.h"
#include "chatbot/sleeper.h"
#include "chatbot/transport.h"
#include "chatbot/types.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chatbot {

/// Un incremento del flujo: texto de la respuesta (delta.content) o
/// pensamiento del modelo (delta.reasoning_content o delta.reasoning). El
/// pensamiento es solo para mostrarse: nunca vuelve al modelo.
struct StreamDelta {
    enum class Kind { Content, Reasoning };
    Kind kind = Kind::Content;
    std::string_view text; ///< Nunca vacío; vale solo durante la llamada.
};

/// Callback de streaming: recibe cada incremento, de texto o de pensamiento,
/// en el orden en que llegan. Devolver false cancela la petición (se
/// devuelve ErrorKind::Cancelled).
using StreamDeltaCallback = std::function<bool(const StreamDelta& delta)>;

/// Una herramienta que se ofrece al modelo (formato OpenAI "function").
struct ToolSpec {
    std::string name;
    std::string description;
    std::string parameters; ///< JSON Schema como texto; tiene que ser un objeto.
};

/// Opciones de la petición del modo código (guias/18-moss-code.md, 3.2 y 3.4).
/// Por defecto, el cuerpo es el del chat, byte por byte.
struct RequestOptions {
    /// Con herramientas se mandan "tools" y "tool_choice": "auto"; sin
    /// ellas, ninguna de las dos llaves. Nombre vacío o repetido, o
    /// parameters que no es un objeto JSON: error Config sin mandar nada.
    std::vector<ToolSpec> tools{};
    /// true: cada asistente con reasoning lo lleva en su llave
    /// (reasoning_key). false: el pensamiento nunca se manda.
    bool replay_reasoning{false};
};

/// Cliente del hito 1: petición simple sin streaming, con reintentos.
/// Las excepciones internas nunca cruzan la API pública (sección 6).
class ChatClient {
public:
    /// Toma posesión del transporte. Usa RealSleeper salvo que se inyecte
    /// otro (para que las pruebas no esperen de verdad).
    ChatClient(Config config, std::unique_ptr<Transport> transport);
    ChatClient(Config config, std::unique_ptr<Transport> transport,
               std::unique_ptr<Sleeper> sleeper);
    ~ChatClient();

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;
    ChatClient(ChatClient&&) = delete;
    ChatClient& operator=(ChatClient&&) = delete;

    /// Envía la conversación completa y devuelve el contenido de la respuesta
    /// del asistente (choices[0].message.content). Nunca lanza excepciones.
    /// Con cancel: se revisa antes de cada intento, viaja al transporte e
    /// interrumpe la espera de reintento; si se cancela, devuelve
    /// ErrorKind::Cancelled sin reintentar. El token debe vivir toda la llamada.
    [[nodiscard]] Result<std::string> complete(const std::vector<Message>& messages,
                                               const CancelToken* cancel = nullptr);

    /// Igual que complete() pero pidiendo stream:true al servidor: entrega
    /// los incrementos de texto y de pensamiento por on_delta conforme llegan
    /// (en un mismo chunk, primero el pensamiento) y devuelve el
    /// resultado: en éxito, el finish_reason (CompletionInfo); el texto
    /// completo ya pasó por el callback. Reintenta igual que complete() mientras no se haya entregado
    /// ningún delta, de ningún tipo. Cancela si on_delta devuelve false o con cancel, igual
    /// que complete(). options: herramientas y pensamiento de regreso
    /// (RequestOptions). Nunca lanza excepciones.
    [[nodiscard]] Result<CompletionInfo> complete_stream(const std::vector<Message>& messages,
                                                         const StreamDeltaCallback& on_delta,
                                                         const CancelToken* cancel = nullptr,
                                                         const RequestOptions& options = {});

    /// GET {base_url}/models: los id de modelo que da el servidor, sin el
    /// prefijo "models/" (Gemini), sin duplicados y en orden alfabético. No
    /// necesita modelo en la configuración (solo URL y key, o un servidor
    /// local). Mismo mapeo de errores, reintentos y cancelación que
    /// complete(); "data" ausente o que no es arreglo da BadResponse. Nunca
    /// lanza excepciones.
    [[nodiscard]] Result<std::vector<std::string>> list_models(const CancelToken* cancel = nullptr);

    /// Las llaves de primer nivel que los ajustes del modelo agregan al
    /// cuerpo (request_param_keys de su Config); vacío sin ajustes.
    [[nodiscard]] std::vector<std::string> param_keys() const;

private:
    /// Envía request con la política de reintentos (sección 6) y devuelve el
    /// cuerpo de la respuesta 2xx, o el error mapeado.
    [[nodiscard]] Result<std::string> send_with_retries(const HttpRequest& request,
                                                        const CancelToken* cancel);
    Sleeper& sleeper();

    Config config_;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<Sleeper> sleeper_;
};

} // namespace chatbot

#endif // CHATBOT_CHAT_CLIENT_H
