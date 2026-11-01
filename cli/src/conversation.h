#ifndef CHATBOT_CLI_CONVERSATION_H
#define CHATBOT_CLI_CONVERSATION_H

#include "conversation_store.h"

#include "chatbot/error.h"
#include "chatbot/search_context.h"
#include "chatbot/types.h"
#include "chatbot/web_search.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// Error de una respuesta que terminó bien pero sin texto (finish_success).
inline constexpr std::string_view kEmptyAnswer = "El modelo no devolvió texto.";
/// Lo mismo cuando llegó pensamiento y finish_reason es "length": se le
/// acabó la salida pensando.
inline constexpr std::string_view kOutOfTokensThinking =
    "El modelo se quedó sin tokens pensando; sube el máximo de salida en Configuración del "
    "modelo.";

/// Tipo de una entrada de la pantalla.
enum class EntryKind {
    User,
    Assistant,
    Error,
    Notice, ///< Texto informativo, no es un error (p. ej. "Respuesta cancelada.").
    Sources, ///< Fuentes de una búsqueda web, después de la respuesta (en sources).
};

/// Cómo quiere ver el usuario el pensamiento de una respuesta (solo en la
/// sesión: no se guarda, y al reabrir una conversación vuelve a Auto).
enum class ReasoningFold : std::uint8_t {
    Auto,   ///< Abierto (las últimas líneas) mientras solo llega pensamiento; si no, plegado.
    Open,   ///< Abierto, completo.
    Closed, ///< Plegado.
};

/// Cómo se dibuja el pensamiento de una entrada (reasoning_view).
enum class ReasoningView : std::uint8_t {
    None,   ///< No tiene pensamiento (o no es del asistente).
    Folded, ///< Una línea "▸ Pensamiento · N palabras".
    Live,   ///< Llegando sin texto todavía: la línea y las últimas líneas.
    Open,   ///< La línea y el pensamiento completo.
};

/// Una entrada de la pantalla. Es independiente del historial para la API:
/// los errores y las respuestas incompletas se muestran, pero no se envían.
struct Entry {
    EntryKind kind = EntryKind::User;
    std::string text;
    bool in_progress = false; ///< Respuesta del asistente que sigue llegando.
    bool incomplete = false;  ///< Respuesta cortada por un error.
    bool cancelled = false;   ///< Respuesta cortada porque el usuario canceló.
    std::string note;         ///< Nota de cómo terminó (finish_reason), o vacía.
    /// Solo Sources: los resultados que recibió el modelo, numerados desde 1
    /// en el mismo orden que en el bloque. Salen de la búsqueda, no del texto
    /// del modelo (que podría inventar URLs).
    std::vector<SearchResult> sources;
    /// Solo Assistant: el pensamiento del modelo, para mostrarse plegado.
    /// Nunca va en el historial que se envía.
    std::string reasoning;
    ReasoningFold reasoning_fold = ReasoningFold::Auto;
};

/// Cómo se dibuja el pensamiento de entry, según su ReasoningFold.
[[nodiscard]] ReasoningView reasoning_view(const Entry& entry);

/// "Pensamiento · 1 240 palabras": palabras separadas por espacios, con
/// espacio como separador de miles ("1 palabra" en singular).
[[nodiscard]] std::string reasoning_summary(std::string_view reasoning);

/// Estado de la conversación. Se usa solo desde el hilo de la interfaz y no
/// sabe nada de FTXUI ni de hilos.
class Conversation {
public:
    /// El historial empieza con un mensaje de sistema con system_prompt (las
    /// instrucciones vigentes); si está vacío, sin mensaje de sistema.
    explicit Conversation(std::string_view system_prompt);

    /// Cambia las instrucciones de sistema: reemplaza, agrega o quita (si
    /// está vacío) el mensaje System inicial del historial. Las entradas, los
    /// pares guardados y lo demás del historial no cambian. Con una petición
    /// en curso no hace nada y devuelve false.
    bool set_system_prompt(std::string_view system_prompt);

    /// Recorta espacios en los extremos. Si el texto queda vacío o hay una
    /// petición en curso, no hace nada y devuelve nullopt. Si no, agrega la
    /// entrada de usuario y el mensaje al historial, marca "ocupado" y
    /// devuelve una copia del historial para enviarla.
    [[nodiscard]] std::optional<std::vector<Message>> submit(std::string_view text);

    /// La búsqueda de /buscar terminó con resultados (hilo de la interfaz):
    /// el content del último mensaje User del historial pasa a ser block (el
    /// bloque que se envió), y la búsqueda se guarda con el par si la
    /// respuesta termina bien; las fuentes se muestran después de ella. date:
    /// "AAAA-MM-DD" con la que se armó el bloque. Sin petición en curso no
    /// hace nada y devuelve false.
    bool attach_search(SearchResponse response, std::string date, std::string block);

    /// Agrega texto a la respuesta en curso; la crea con el primer delta (de
    /// texto o de pensamiento). Sin petición en curso o con texto vacío, no
    /// hace nada.
    void append_delta(std::string_view text);
    /// Igual que append_delta, pero al pensamiento (Entry::reasoning), que
    /// se guarda con el par y nunca entra al historial que se envía.
    void append_reasoning(std::string_view text);

    /// Abre o cierra el pensamiento de la entrada index (clic en su línea).
    /// Sin pensamiento, no hace nada.
    void toggle_reasoning(std::size_t index);
    /// Ctrl+T: si alguno no está abierto completo, abre todos; si no, los
    /// cierra todos.
    void toggle_all_reasoning();

    /// Cierra la respuesta y la agrega al historial. Si no llegó texto, se
    /// trata como error (empty_answer_error) y devuelve el texto
    /// del usuario, igual que finish_error(); si salió bien, nullopt.
    /// finish_reason ("stop", "length", ...) solo agrega una nota a la
    /// entrada: el texto entra al historial porque es texto real. model es el
    /// modelo configurado, que se guarda con la respuesta.
    [[nodiscard]] std::optional<std::string> finish_success(std::string_view finish_reason = {},
                                                            std::string_view model = {});
    /// El error de terminar ahora sin texto (BadResponse): kOutOfTokensThinking
    /// si la respuesta en curso trae pensamiento y finish_reason es "length";
    /// si no, kEmptyAnswer. Session lo pide antes de finish_success.
    [[nodiscard]] ChatError empty_answer_error(std::string_view finish_reason) const;

    /// Quita del historial el último mensaje de usuario (no de la pantalla),
    /// marca como incompleta la respuesta parcial si la hubo y agrega una
    /// entrada de error. Devuelve el texto del usuario para que la interfaz
    /// lo regrese a la caja de entrada.
    /// Con ErrorKind::Cancelled no hay entrada de error: la respuesta parcial
    /// se marca cancelada o, si no hubo texto, se agrega un aviso (Notice).
    /// Una búsqueda sin resultados (ErrorKind::NoSearchResults, sin importar
    /// el texto) también es un aviso, no un error. La búsqueda adjunta, si había, se descarta.
    [[nodiscard]] std::string finish_error(const ChatError& error);

    /// Agrega una entrada de error en rojo (p. ej. al no poder guardar).
    void add_error(std::string text);
    /// Agrega una entrada informativa (Notice).
    void add_notice(std::string text);

    // Persistencia. id, fecha de creación y título están vacíos mientras la
    // conversación no tiene ningún par terminado (no se ha guardado).

    /// Asigna el id y la fecha de creación al guardarla por primera vez.
    void set_identity(std::string id, std::string created_at);
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const std::string& created_at() const { return created_at_; }
    [[nodiscard]] const std::string& title() const { return title_; }
    /// true si hay al menos un par usuario/asistente terminado.
    [[nodiscard]] bool has_turns() const { return !turns_.empty(); }
    /// Modelo de la última respuesta guardada, o vacío.
    [[nodiscard]] std::string last_model() const;

    /// Forma guardable: solo los pares terminados, sin el mensaje de sistema,
    /// con model y finish_reason de cada respuesta.
    [[nodiscard]] StoredConversation to_stored(std::string updated_at) const;
    /// Reconstruye historial (con system_prompt, las instrucciones vigentes,
    /// al inicio; ninguna si está vacío) y entradas de pantalla, incluidas
    /// las notas por finish_reason. Un mensaje con búsqueda se muestra como
    /// se escribió y se envía con el bloque de format_search_context, armado
    /// con su fecha (o la de created_at, si no la tiene) y un nonce nuevo de
    /// make_nonce; sus fuentes van después de la respuesta.
    [[nodiscard]] static Conversation from_stored(
        const StoredConversation& stored, std::string_view system_prompt,
        const std::function<std::string()>& make_nonce = generate_search_nonce);

    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] const std::vector<Message>& history() const { return history_; }

private:
    /// Respuesta del asistente en curso, o nullptr si aún no llega texto.
    Entry* current_answer();
    [[nodiscard]] const Entry* current_answer() const;
    /// La respuesta en curso para un delta, creándola si hace falta; nullptr
    /// si el delta se ignora.
    Entry* answer_for_delta(std::string_view text);

    std::vector<Message> history_; ///< Lo que se envía a la API.
    std::vector<Entry> entries_;   ///< Lo que se muestra en pantalla.
    std::vector<StoredMessage> turns_; ///< Pares terminados, lo que se guarda.
    std::string pending_user_text_;
    std::optional<StoredSearch> pending_search_; ///< Búsqueda del par en curso.
    bool busy_ = false;
    std::string id_;
    std::string created_at_;
    std::string title_;
};

/// Guarda la conversación después de un par terminado. Sin pares no hace
/// nada (una conversación nueva no crea archivo). La primera vez asigna id y
/// fecha de creación. Si falla, agrega una entrada de error ("No se pudo
/// guardar la conversación: <motivo>") y el chat sigue. No lanza excepciones.
/// Devuelve true si escribió el archivo.
[[nodiscard]] bool save_conversation(Conversation& conversation, const ConversationStore& store);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_CONVERSATION_H
