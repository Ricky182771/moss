#ifndef CHATBOT_CLI_PROVIDER_SETTINGS_H
#define CHATBOT_CLI_PROVIDER_SETTINGS_H

#include "chatbot/providers.h"

#include "chatbot/config.h"
#include "chatbot/credentials.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot::cli {

/// Variables CHAT_* que pisan el formulario (solo las definidas y no vacías).
struct SettingsEnv {
    std::optional<std::string> base_url; ///< CHAT_BASE_URL
    std::optional<std::string> model;    ///< CHAT_MODEL
    std::optional<std::string> api_key;  ///< CHAT_API_KEY
};

/// Estado de la lista de modelos.
enum class ModelsState { Idle, Loading, Loaded, Failed };

/// false si la key escrita parece el nombre de una variable ("$NIMKEY"),
/// trae espacios o saltos de línea, o es demasiado corta para ser completa
/// (menos de 20 caracteres). Vale para la key del modelo y la de búsqueda.
[[nodiscard]] bool plausible_key(std::string_view key);

/// Key enmascarada: "…" y sus últimos 4 caracteres si mide al menos 8 y esos
/// 4 son ASCII imprimibles; si no, solo "…". Nunca expone más de 4.
[[nodiscard]] std::string mask_key(std::string_view key);

/// Una fila de la lista de proveedores (ProviderSettings::provider_list).
struct ProviderEntry {
    std::string id; ///< "nvidia", "custom:<id>" o vacío en la de agregar.
    std::string name;
    std::string base_url;
    bool needs_key = true; ///< En un endpoint: si su URL no es local.
    bool endpoint = false; ///< Un endpoint guardado.
    bool add = false;      ///< "Agregar endpoint…" (siempre la última).
};
inline constexpr std::string_view kAddEndpoint = "Agregar endpoint…";

/// Estado del formulario "Proveedor de IA", sin FTXUI ni hilos: proveedor
/// (de la tabla, un endpoint guardado o uno nuevo), nombre y URL base
/// (editables solo en un endpoint), key escrita, filtro y lista de modelos,
/// modelo elegido y si hay cambios sin guardar. Los campos definidos por
/// CHAT_* se muestran con su valor efectivo y no se editan. Se usa solo
/// desde el hilo de la interfaz.
class ProviderSettings {
public:
    /// saved: lo que hay en config.json (sin el entorno); credentials: las
    /// keys guardadas; env: las variables CHAT_* definidas.
    ProviderSettings(ConfigFileValues saved, Credentials credentials, SettingsEnv env);

    /// Lo que hay en config.json, tal como se leyó.
    [[nodiscard]] const ConfigFileValues& saved() const { return saved_; }

    // --- Proveedor ---
    /// La lista de F2, del selector de /model y del RPC: los de providers(),
    /// los endpoints en su orden y "Agregar endpoint…" al final. Los índices
    /// de provider() y select_provider son de esta lista.
    [[nodiscard]] std::vector<ProviderEntry> provider_list() const;
    [[nodiscard]] std::size_t provider() const { return provider_; }
    /// El proveedor de lo guardado (el de config.json), o kNoProvider si no
    /// está en la lista (proveedor desconocido o endpoint que no existe).
    [[nodiscard]] std::size_t saved_provider() const { return saved_provider_; }
    static constexpr std::size_t kNoProvider = static_cast<std::size_t>(-1);
    /// Índice en provider_list() de un id de la tabla, "custom:<id>", el id
    /// de un endpoint solo o "custom:" (la fila de agregar); nullopt si no
    /// existe.
    [[nodiscard]] std::optional<std::size_t> find_provider(std::string_view id) const;
    /// Cambia de proveedor: la URL base y el nombre son los de la tabla o del
    /// endpoint (vacíos en uno nuevo), la key escrita y el filtro se vacían,
    /// la lista de modelos vuelve a Idle y el modelo es el guardado si se
    /// regresa al proveedor guardado; si no, ninguno.
    void select_provider(std::size_t index);
    /// Un endpoint (guardado o nuevo): nombre y URL editables.
    [[nodiscard]] bool is_endpoint() const;
    [[nodiscard]] bool is_new_endpoint() const;
    /// El id con que se guarda: el de la tabla o "custom:<id>" (en uno
    /// nuevo, el que make_endpoint_id genera del nombre).
    [[nodiscard]] std::string provider_id() const;
    /// Nombre para avisos: el de la tabla o el del endpoint.
    [[nodiscard]] std::string provider_label() const;

    // --- Nombre (solo endpoints) ---
    [[nodiscard]] const std::string& name() const { return name_; }
    /// false (y no cambia nada) si no es un endpoint.
    bool set_name(std::string name);

    // --- URL base ---
    /// Valor del formulario (lo que se guarda en config.json).
    [[nodiscard]] const std::string& base_url() const { return base_url_; }
    /// La que se usa: CHAT_BASE_URL si está definida; si no, la del formulario.
    [[nodiscard]] const std::string& effective_base_url() const;
    [[nodiscard]] bool base_url_locked() const { return env_.base_url.has_value(); }
    [[nodiscard]] bool base_url_editable() const;
    /// false (y no cambia nada) si no es editable.
    bool set_base_url(std::string url);
    /// Aviso que no bloquea (validate() y can_request_models() no cambian):
    /// en un endpoint, si la URL efectiva es válida pero no tiene ruta
    /// ("https://h", "https://h/"), recuerda que casi todos usan /v1. Nullopt
    /// con un proveedor conocido, con ruta o con una URL inválida.
    [[nodiscard]] std::optional<std::string> base_url_hint() const;

    // --- Key ---
    /// La key escrita en el formulario (vacía: se conserva la guardada).
    [[nodiscard]] const std::string& key() const { return key_; }
    [[nodiscard]] bool key_locked() const { return env_.api_key.has_value(); }
    /// false (y no cambia nada) si CHAT_API_KEY está definida.
    bool set_key(std::string key);
    /// La que se usa: CHAT_API_KEY, la escrita o la guardada para el
    /// proveedor.
    [[nodiscard]] std::string effective_key() const;
    /// Texto junto al campo, siempre enmascarado: "(definido por
    /// CHAT_API_KEY) …abcd", "guardada: …abcd", "opcional en local" o "".
    [[nodiscard]] std::string key_status() const;
    /// Hay key en credentials.json para el proveedor.
    [[nodiscard]] bool has_saved_key() const { return !saved_key().empty(); }

    // --- Modelo ---
    [[nodiscard]] const std::string& model() const { return model_; }
    [[nodiscard]] bool model_locked() const { return env_.model.has_value(); }
    /// El que se usa: CHAT_MODEL si está definida; si no, el del formulario.
    [[nodiscard]] const std::string& effective_model() const;
    /// Escrito a mano. false (y no cambia nada) si CHAT_MODEL está definida.
    bool set_model(std::string model);

    // --- Lista de modelos ---
    [[nodiscard]] ModelsState models_state() const { return models_state_; }
    /// Error ya formateado para mostrarse (sin la key).
    [[nodiscard]] const std::string& models_error() const { return models_error_; }
    void models_loading();
    void models_loaded(std::vector<std::string> models);
    void models_failed(std::string message);
    [[nodiscard]] const std::string& filter() const { return filter_; }
    void set_filter(std::string filter);
    /// Los modelos que contienen el filtro, sin distinguir mayúsculas.
    [[nodiscard]] std::vector<std::string> filtered_models() const;
    /// Índice resaltado en filtered_models().
    [[nodiscard]] std::size_t highlighted() const { return highlighted_; }
    /// ↑/↓: mueve el resaltado dentro de filtered_models().
    void move_highlight(int delta);
    /// Enter: el modelo resaltado pasa a ser el elegido. false si no hay o
    /// el modelo está definido por CHAT_MODEL.
    bool pick_highlighted();

    // --- Petición y guardado ---
    /// true si hay URL válida y key (o el host es local): se puede pedir
    /// la lista de modelos.
    [[nodiscard]] bool can_request_models() const;
    /// base con la URL, la key y el modelo efectivos del formulario.
    [[nodiscard]] Config request_config(Config base) const;
    /// Error para mostrar si no se puede guardar, o nullopt: en un endpoint,
    /// el nombre, el tope de kMaxEndpoints y la URL que se guarda (la del
    /// formulario, aunque CHAT_BASE_URL gane); luego URL, key y modelo. Una
    /// key escrita que empieza con "$", trae espacios o saltos de línea, o
    /// mide menos de 20 caracteres se rechaza (el mensaje nunca la incluye).
    [[nodiscard]] std::optional<std::string> validate() const;
    /// Lo que se escribe en config.json: en un endpoint, su entrada de
    /// "endpoints" actualizada (o una nueva al final). Con CHAT_MODEL
    /// definida se conserva el modelo guardado.
    [[nodiscard]] ConfigFileValues file_values() const;
    /// Llave y key que se escriben en credentials.json, si se escribió una
    /// (y CHAT_API_KEY no está definida).
    [[nodiscard]] std::optional<std::pair<std::string, std::string>> credential_update() const;
    /// true si el formulario difiere de lo guardado.
    [[nodiscard]] bool dirty() const;
    /// Tras guardar: lo actual pasa a ser lo guardado (un endpoint nuevo
    /// queda en la lista con su id) y la key escrita se vacía (queda en
    /// credentials).
    void mark_saved();

private:
    [[nodiscard]] std::string saved_key() const;
    /// force: también si index ya es el elegido (para el constructor).
    void select_provider(std::size_t index, bool force);
    /// El endpoint guardado elegido, o nullptr (tabla o uno nuevo).
    [[nodiscard]] const Endpoint* endpoint() const;

    ConfigFileValues saved_;
    std::size_t saved_provider_ = 0;
    Credentials credentials_;
    SettingsEnv env_;

    std::size_t provider_ = 0;
    std::string name_;
    std::string base_url_;
    std::string key_;
    std::string model_;

    ModelsState models_state_ = ModelsState::Idle;
    std::string models_error_;
    std::vector<std::string> models_;
    std::string filter_;
    std::size_t highlighted_ = 0;

    // Formulario al abrir o tras el último guardado, para dirty().
    std::size_t initial_provider_ = 0;
    std::string initial_name_;
    std::string initial_url_;
    std::string initial_model_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_PROVIDER_SETTINGS_H
