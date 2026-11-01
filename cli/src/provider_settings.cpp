#include "provider_settings.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <utility>

namespace chatbot::cli {

namespace {

/// Largo mínimo de una key escrita en el formulario.
constexpr std::size_t kMinKeyLength = 20;

std::string lowercase(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

/// Filas de la tabla en provider_list(): los endpoints van después.
std::size_t table_size() { return providers().size(); }

} // namespace

bool plausible_key(std::string_view key) {
    if (key.size() < kMinKeyLength || key.front() == '$') {
        return false;
    }
    return key.find_first_of(" \t\n\r\v\f") == std::string_view::npos;
}

std::string mask_key(std::string_view key) {
    static const std::string kEllipsis = "…";
    if (key.size() < 8) {
        return kEllipsis;
    }
    const std::string_view tail = key.substr(key.size() - 4);
    for (const char c : tail) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x21 || byte > 0x7e) {
            return kEllipsis;
        }
    }
    return kEllipsis + std::string{tail};
}

ProviderSettings::ProviderSettings(ConfigFileValues saved, Credentials credentials,
                                   SettingsEnv env)
    : saved_(std::move(saved)), credentials_(std::move(credentials)), env_(std::move(env)) {
    // Sin "provider": el de la tabla con esa URL base (sin URL o con otra,
    // NVIDIA, el valor por defecto del núcleo). Uno que no existe no queda
    // elegido como guardado: F2 empieza en NVIDIA.
    if (saved_.provider.empty()) {
        const auto table = providers();
        const auto same = std::find_if(table.begin(), table.end(), [this](const ProviderInfo& info) {
            return info.base_url == saved_.base_url;
        });
        saved_provider_ = same != table.end() ? static_cast<std::size_t>(same - table.begin()) : 0;
    } else {
        // En config.json solo valen un id de la tabla o "custom:<id>" de un
        // endpoint guardado (ni el id solo ni "custom:").
        const std::optional<std::size_t> index = find_provider(saved_.provider);
        const bool known =
            index.has_value() &&
            (*index < table_size() || (endpoint_id(saved_.provider).has_value() &&
                                       *index < table_size() + saved_.endpoints.size()));
        saved_provider_ = known ? *index : kNoProvider;
    }
    provider_ = saved_provider_ == kNoProvider ? 0 : saved_provider_;
    select_provider(provider_, true); // El modelo guardado, solo si se conoce el proveedor.
    initial_provider_ = provider_;
    initial_name_ = name_;
    initial_url_ = base_url_;
    initial_model_ = model_;
}

std::vector<ProviderEntry> ProviderSettings::provider_list() const {
    std::vector<ProviderEntry> list;
    for (const ProviderInfo& info : providers()) {
        list.push_back({std::string{info.id}, std::string{info.name}, std::string{info.base_url},
                        info.needs_key, false, false});
    }
    for (const Endpoint& endpoint : saved_.endpoints) {
        list.push_back({endpoint_provider(endpoint.id), endpoint.name, endpoint.base_url,
                        !is_local_base_url(endpoint.base_url), true, false});
    }
    list.push_back({{}, std::string{kAddEndpoint}, {}, true, false, true});
    return list;
}

std::optional<std::size_t> ProviderSettings::find_provider(std::string_view id) const {
    if (const std::optional<std::size_t> index = provider_index(id)) {
        return index;
    }
    const std::string_view bare = endpoint_id(id).value_or(id);
    if (id == kEndpointPrefix) {
        return table_size() + saved_.endpoints.size();
    }
    for (std::size_t i = 0; i < saved_.endpoints.size(); ++i) {
        if (saved_.endpoints[i].id == bare) {
            return table_size() + i;
        }
    }
    return std::nullopt;
}

void ProviderSettings::select_provider(std::size_t index) { select_provider(index, false); }

void ProviderSettings::select_provider(std::size_t index, bool force) {
    if (index > table_size() + saved_.endpoints.size() || (index == provider_ && !force)) {
        return;
    }
    provider_ = index;
    const Endpoint* chosen = endpoint();
    if (index < table_size()) {
        name_.clear();
        base_url_ = std::string{providers()[index].base_url};
    } else {
        name_ = chosen != nullptr ? chosen->name : std::string{};
        base_url_ = chosen != nullptr ? chosen->base_url : std::string{};
    }
    key_.clear();
    filter_.clear();
    models_.clear();
    models_error_.clear();
    models_state_ = ModelsState::Idle;
    highlighted_ = 0;
    model_ = index == saved_provider_ ? saved_.model : std::string{};
}

const Endpoint* ProviderSettings::endpoint() const {
    if (provider_ < table_size() || provider_ - table_size() >= saved_.endpoints.size()) {
        return nullptr;
    }
    return &saved_.endpoints[provider_ - table_size()];
}

bool ProviderSettings::is_endpoint() const { return provider_ >= table_size(); }

bool ProviderSettings::is_new_endpoint() const { return is_endpoint() && endpoint() == nullptr; }

std::string ProviderSettings::provider_id() const {
    if (!is_endpoint()) {
        return std::string{providers()[provider_].id};
    }
    if (const Endpoint* chosen = endpoint()) {
        return endpoint_provider(chosen->id);
    }
    return endpoint_provider(make_endpoint_id(name_, saved_.endpoints));
}

std::string ProviderSettings::provider_label() const {
    return is_endpoint() ? name_ : std::string{providers()[provider_].name};
}

bool ProviderSettings::set_name(std::string name) {
    if (!is_endpoint()) {
        return false;
    }
    name_ = std::move(name);
    return true;
}

const std::string& ProviderSettings::effective_base_url() const {
    return env_.base_url.has_value() ? *env_.base_url : base_url_;
}

bool ProviderSettings::base_url_editable() const { return is_endpoint() && !base_url_locked(); }

std::optional<std::string> ProviderSettings::base_url_hint() const {
    const std::string& url = effective_base_url();
    // Hay servidores que sirven en la raíz: es solo un aviso.
    if (!is_endpoint() || validate_base_url(url).has_value() ||
        base_url_has_path(url)) {
        return std::nullopt;
    }
    return "Casi todos los servidores compatibles con OpenAI usan una ruta como /v1 "
           "(p. ej. https://servidor/v1).";
}

bool ProviderSettings::set_base_url(std::string url) {
    if (!base_url_editable()) {
        return false;
    }
    if (url != base_url_) {
        models_state_ = ModelsState::Idle; // La lista era de otra URL.
        models_.clear();
        models_error_.clear();
        highlighted_ = 0;
    }
    base_url_ = std::move(url);
    return true;
}

bool ProviderSettings::set_key(std::string key) {
    if (key_locked()) {
        return false;
    }
    key_ = std::move(key);
    return true;
}

std::string ProviderSettings::saved_key() const {
    if (is_new_endpoint()) {
        return {};
    }
    const auto it = credentials_.keys.find(credentials_key(provider_id()));
    return it != credentials_.keys.end() ? it->second : std::string{};
}

std::string ProviderSettings::effective_key() const {
    if (env_.api_key.has_value()) {
        return *env_.api_key;
    }
    return key_.empty() ? saved_key() : key_;
}

std::string ProviderSettings::key_status() const {
    if (env_.api_key.has_value()) {
        return "(definido por CHAT_API_KEY) " + mask_key(*env_.api_key);
    }
    if (!key_.empty()) {
        return {};
    }
    if (const std::string saved = saved_key(); !saved.empty()) {
        return "guardada: " + mask_key(saved);
    }
    if (is_local_base_url(effective_base_url())) {
        return "opcional en local";
    }
    return {};
}

const std::string& ProviderSettings::effective_model() const {
    return env_.model.has_value() ? *env_.model : model_;
}

bool ProviderSettings::set_model(std::string model) {
    if (model_locked()) {
        return false;
    }
    model_ = std::move(model);
    return true;
}

void ProviderSettings::models_loading() {
    models_state_ = ModelsState::Loading;
    models_error_.clear();
}

void ProviderSettings::models_loaded(std::vector<std::string> models) {
    models_state_ = ModelsState::Loaded;
    models_error_.clear();
    models_ = std::move(models);
    highlighted_ = 0;
    // El resaltado empieza en el modelo elegido, si está en la lista.
    const std::vector<std::string> visible = filtered_models();
    const auto it = std::find(visible.begin(), visible.end(), effective_model());
    if (it != visible.end()) {
        highlighted_ = static_cast<std::size_t>(it - visible.begin());
    }
}

void ProviderSettings::models_failed(std::string message) {
    models_state_ = ModelsState::Failed;
    models_error_ = std::move(message);
    models_.clear();
    highlighted_ = 0;
}

void ProviderSettings::set_filter(std::string filter) {
    filter_ = std::move(filter);
    highlighted_ = 0;
}

std::vector<std::string> ProviderSettings::filtered_models() const {
    const std::string needle = lowercase(filter_);
    std::vector<std::string> out;
    for (const std::string& model : models_) {
        if (lowercase(model).find(needle) != std::string::npos) {
            out.push_back(model);
        }
    }
    return out;
}

void ProviderSettings::move_highlight(int delta) {
    const std::size_t count = filtered_models().size();
    if (count == 0) {
        highlighted_ = 0;
        return;
    }
    const long long next = static_cast<long long>(highlighted_) + delta;
    highlighted_ = static_cast<std::size_t>(
        std::clamp<long long>(next, 0, static_cast<long long>(count) - 1));
}

bool ProviderSettings::pick_highlighted() {
    const std::vector<std::string> visible = filtered_models();
    if (visible.empty() || model_locked()) {
        return false;
    }
    model_ = visible[std::min(highlighted_, visible.size() - 1)];
    return true;
}

bool ProviderSettings::can_request_models() const {
    const std::string& url = effective_base_url();
    return !validate_base_url(url).has_value() &&
           (!effective_key().empty() || is_local_base_url(url));
}

Config ProviderSettings::request_config(Config base) const {
    base.provider = provider_id();
    base.base_url = effective_base_url();
    base.api_key = effective_key();
    base.model = effective_model();
    // El error es del proveedor de config.json; el del formulario ya es válido.
    base.provider_error.clear();
    return base;
}

std::optional<std::string> ProviderSettings::validate() const {
    if (is_new_endpoint() && saved_.endpoints.size() >= kMaxEndpoints) {
        return "Ya tienes " + std::to_string(kMaxEndpoints) +
               " endpoints; borra uno antes de agregar otro.";
    }
    if (is_endpoint()) {
        if (std::optional<std::string> problem = endpoint_name_problem(name_)) {
            return problem;
        }
        const Endpoint* self = endpoint();
        for (const Endpoint& other : saved_.endpoints) {
            if (&other != self && same_endpoint_name(other.name, name_)) {
                return "Ya hay un endpoint con ese nombre.";
            }
        }
        // Con CHAT_BASE_URL la URL no se edita: uno nuevo quedaría sin la suya.
        if (is_new_endpoint() && base_url_locked()) {
            return "Con CHAT_BASE_URL definida no se puede agregar un endpoint.";
        }
    }
    const std::string& url = effective_base_url();
    if (url.empty()) {
        return "Escribe la URL base del servidor.";
    }
    if (const std::optional<ChatError> error = validate_base_url(url)) {
        return error->message;
    }
    // El mensaje nunca incluye la key.
    if (!key_locked() && !key_.empty() && !plausible_key(key_)) {
        return "Eso parece el nombre de una variable o una key incompleta; pega la key completa.";
    }
    if (effective_key().empty() && !is_local_base_url(url)) {
        return "Falta la API key de " + provider_label() + ".";
    }
    if (effective_model().empty()) {
        return "Elige un modelo de la lista o escríbelo.";
    }
    return std::nullopt;
}

ConfigFileValues ProviderSettings::file_values() const {
    ConfigFileValues values;
    values.provider = provider_id();
    values.base_url = base_url_;
    values.model = model_locked() ? saved_.model : model_;
    values.endpoints = saved_.endpoints;
    if (is_endpoint()) {
        const std::string id{*endpoint_id(values.provider)};
        const auto it = std::find_if(values.endpoints.begin(), values.endpoints.end(),
                                     [&id](const Endpoint& other) { return other.id == id; });
        if (it == values.endpoints.end()) {
            values.endpoints.push_back({id, name_, base_url_});
        } else {
            it->name = name_;
            it->base_url = base_url_;
        }
    }
    return values;
}

std::optional<std::pair<std::string, std::string>> ProviderSettings::credential_update() const {
    if (key_locked() || !plausible_key(key_)) {
        return std::nullopt;
    }
    return std::pair{credentials_key(provider_id()), key_};
}

bool ProviderSettings::dirty() const {
    return provider_ != initial_provider_ || name_ != initial_name_ ||
           base_url_ != initial_url_ || model_ != initial_model_ || !key_.empty();
}

void ProviderSettings::mark_saved() {
    if (const auto update = credential_update()) {
        credentials_.keys[update->first] = update->second;
    }
    saved_ = file_values(); // Un endpoint nuevo queda en provider_, al final de los guardados.
    saved_provider_ = provider_;
    key_.clear();
    initial_provider_ = provider_;
    initial_name_ = name_;
    initial_url_ = base_url_;
    initial_model_ = model_;
}

} // namespace chatbot::cli
