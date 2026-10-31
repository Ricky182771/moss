// Lógica de la sección "Proveedor de IA" (sin FTXUI): tabla, cambio de
// proveedor, nombre y URL editables solo en un endpoint, enmascarado, campos bloqueados
// por CHAT_*, filtro de modelos y cambios sin guardar.

#include "provider_settings.h"
#include "chatbot/providers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {

using chatbot::ConfigFileValues;
using chatbot::Credentials;
using chatbot::cli::ModelsState;
using chatbot::cli::ProviderSettings;
using chatbot::cli::SettingsEnv;

std::size_t provider_index(std::string_view id) { return *chatbot::provider_index(id); }

constexpr const char* kSecret = "sk-clave-secreta-de-prueba-1234";

ConfigFileValues saved(std::string provider, std::string base_url, std::string model) {
    ConfigFileValues values;
    values.provider = std::move(provider);
    values.base_url = std::move(base_url);
    values.model = std::move(model);
    return values;
}

/// config.json con el endpoint "lab" (nombre "Lab") en uso, con esa URL.
ConfigFileValues lab(std::string url, std::string model = "m") {
    ConfigFileValues values = saved("custom:lab", url, std::move(model));
    values.endpoints = {{"lab", "Lab", std::move(url)}};
    return values;
}

Credentials credentials_with(std::string id, std::string key) {
    Credentials credentials;
    credentials.keys[std::move(id)] = std::move(key);
    return credentials;
}

} // namespace

TEST_CASE("proveedores: tabla con ids únicos y URL válidas", "[proveedor][ajustes]") {
    const auto table = chatbot::providers();
    REQUIRE(table.size() == 12);
    for (std::size_t i = 0; i < table.size(); ++i) {
        CAPTURE(table[i].id);
        CHECK(provider_index(table[i].id) == i);
        // Toda URL de la tabla es válida (http solo en Ollama, local).
        CHECK_FALSE(chatbot::validate_base_url(table[i].base_url).has_value());
        CHECK(table[i].needs_key == (table[i].id != "ollama"));
    }
    CHECK_FALSE(chatbot::provider_index("no-existe").has_value());
    CHECK_FALSE(chatbot::provider_index("custom").has_value());
    CHECK(table[provider_index("gemini")].base_url ==
          "https://generativelanguage.googleapis.com/v1beta/openai");
}

TEST_CASE("ajustes: el proveedor inicial sale de config.json", "[proveedor][ajustes]") {
    CHECK(ProviderSettings(saved("", "", ""), {}, {}).provider_id() == "nvidia");
    CHECK(ProviderSettings(saved("", "https://api.openai.com/v1", ""), {}, {}).provider_id() ==
          "openai");
    const ProviderSettings endpoint(lab("http://localhost:8080/v1"), {}, {});
    CHECK(endpoint.provider_id() == "custom:lab");
    CHECK(endpoint.is_endpoint());
    CHECK_FALSE(endpoint.is_new_endpoint());
    CHECK(endpoint.name() == "Lab");
    CHECK(endpoint.base_url() == "http://localhost:8080/v1");
    CHECK(endpoint.model() == "m");
    CHECK(endpoint.saved_provider() == endpoint.provider());
    CHECK_FALSE(endpoint.dirty());

    // Un proveedor que no existe (también el "custom" de antes): F2 empieza
    // en NVIDIA, sin nada elegido como guardado ni su modelo.
    for (const char* provider : {"custom", "lab", "custom:otro", "custom:", "no-existe"}) {
        CAPTURE(provider);
        ConfigFileValues values = lab("https://lab/v1", "qwen3");
        values.provider = provider;
        const ProviderSettings unknown(values, {}, {});
        CHECK(unknown.provider_id() == "nvidia");
        CHECK(unknown.saved_provider() == ProviderSettings::kNoProvider);
        CHECK(unknown.model().empty());
        CHECK_FALSE(unknown.dirty());
    }
}

TEST_CASE("ajustes: la lista de proveedores es la tabla, los endpoints y agregar",
          "[proveedor][ajustes][endpoints]") {
    ConfigFileValues values = saved("nvidia", "", "m");
    values.endpoints = {{"omniroute", "OmniRoute", "https://gw.example/v1"},
                        {"lab", "Servidor del lab", "http://localhost:8000/v1"}};
    const ProviderSettings settings(values, {}, {});
    const std::vector<chatbot::cli::ProviderEntry> list = settings.provider_list();
    const std::size_t table = chatbot::providers().size();
    REQUIRE(list.size() == table + 3);
    CHECK(list[0].id == "nvidia");
    CHECK_FALSE(list[0].endpoint);
    CHECK(list[table].id == "custom:omniroute");
    CHECK(list[table].name == "OmniRoute");
    CHECK(list[table].endpoint);
    CHECK(list[table].needs_key);
    CHECK(list[table + 1].id == "custom:lab");
    CHECK_FALSE(list[table + 1].needs_key); // Local.
    CHECK(list.back().add);
    CHECK(list.back().name == chatbot::cli::kAddEndpoint);

    CHECK(settings.find_provider("custom:lab") == table + 1);
    CHECK(settings.find_provider("lab") == table + 1); // El id solo.
    CHECK(settings.find_provider("custom:") == table + 2);
    CHECK(settings.find_provider("gemini") == provider_index("gemini"));
    CHECK_FALSE(settings.find_provider("custom").has_value());
    CHECK_FALSE(settings.find_provider("custom:otro").has_value());
    CHECK_FALSE(settings.find_provider("").has_value());
}

TEST_CASE("ajustes: cambiar de proveedor rellena la URL y la key guardada",
          "[proveedor][ajustes]") {
    ProviderSettings settings(saved("nvidia", "https://integrate.api.nvidia.com/v1", "m1"),
                              credentials_with("gemini", kSecret), {});
    CHECK(settings.key_status().empty()); // NVIDIA sin key guardada.
    settings.set_key("escrita");
    settings.select_provider(provider_index("gemini"));
    CHECK(settings.base_url() == "https://generativelanguage.googleapis.com/v1beta/openai");
    CHECK(settings.key().empty()); // La key escrita era de otro proveedor.
    CHECK(settings.key_status() == "guardada: …1234");
    CHECK(settings.effective_key() == kSecret);
    CHECK(settings.model().empty());
    // De regreso al guardado, vuelve su modelo.
    settings.select_provider(provider_index("nvidia"));
    CHECK(settings.model() == "m1");
    // Ollama: local, key opcional.
    settings.select_provider(provider_index("ollama"));
    CHECK(settings.base_url() == "http://localhost:11434/v1");
    CHECK(settings.key_status() == "opcional en local");
    CHECK(settings.can_request_models());
}

TEST_CASE("ajustes: nombre y URL solo se editan en un endpoint", "[proveedor][ajustes][endpoints]") {
    ConfigFileValues values = saved("openai", "https://api.openai.com/v1", "");
    values.endpoints = {{"lab", "Lab", "https://lab.example/v1"}};
    ProviderSettings settings(values, credentials_with("custom:lab", kSecret), {});
    CHECK_FALSE(settings.base_url_editable());
    CHECK_FALSE(settings.set_base_url("https://otra/v1"));
    CHECK_FALSE(settings.set_name("Otro"));
    CHECK(settings.base_url() == "https://api.openai.com/v1");

    settings.select_provider(*settings.find_provider("custom:lab"));
    CHECK(settings.base_url_editable());
    CHECK(settings.name() == "Lab");
    CHECK(settings.base_url() == "https://lab.example/v1");
    CHECK(settings.provider_label() == "Lab");
    CHECK(settings.key_status() == "guardada: …1234");
    // Cambiar el nombre y la URL no pierde la key: va por el id.
    CHECK(settings.set_name("Laboratorio"));
    CHECK(settings.set_base_url("https://nueva.example/v1"));
    CHECK(settings.dirty());
    CHECK(settings.effective_key() == kSecret);
    CHECK(settings.provider_id() == "custom:lab");
    settings.set_model("m");
    CHECK_FALSE(settings.validate().has_value());
    const ConfigFileValues file = settings.file_values();
    CHECK(file.provider == "custom:lab");
    REQUIRE(file.endpoints.size() == 1);
    CHECK(file.endpoints[0].id == "lab");
    CHECK(file.endpoints[0].name == "Laboratorio");
    CHECK(file.endpoints[0].base_url == "https://nueva.example/v1");
    settings.set_key("sk-escrita-nueva-0000000");
    REQUIRE(settings.credential_update().has_value());
    CHECK(settings.credential_update()->first == "custom:lab");
    // Al cambiar de proveedor y regresar, vuelve lo guardado.
    settings.select_provider(provider_index("openai"));
    settings.select_provider(*settings.find_provider("lab"));
    CHECK(settings.name() == "Lab");
    CHECK(settings.base_url() == "https://lab.example/v1");
}

TEST_CASE("ajustes: agregar un endpoint", "[proveedor][ajustes][endpoints]") {
    ConfigFileValues values = saved("nvidia", "https://integrate.api.nvidia.com/v1", "m");
    values.endpoints = {{"lab", "Lab", "https://lab.example/v1"}};
    ProviderSettings settings(values, credentials_with("nvidia", kSecret), {});
    settings.select_provider(*settings.find_provider("custom:"));
    CHECK(settings.is_new_endpoint());
    CHECK(settings.name().empty());
    CHECK(settings.base_url().empty());
    CHECK(settings.key_status().empty());
    CHECK(settings.validate() == "Escribe un nombre para el endpoint.");
    settings.set_name("LAB");
    CHECK(settings.validate() == "Ya hay un endpoint con ese nombre.");
    settings.set_name("Servidor Ñandú");
    CHECK(settings.validate() == "Escribe la URL base del servidor.");
    settings.set_base_url("http://192.168.1.10/v1");
    REQUIRE(settings.validate().has_value());
    CHECK_THAT(*settings.validate(), Catch::Matchers::ContainsSubstring("https://"));
    settings.set_base_url("https://nandu.example/v1");
    CHECK(settings.validate() == "Falta la API key de Servidor Ñandú.");
    settings.set_key("sk-del-servidor-nandu-1234");
    settings.set_model("qwen3");
    CHECK_FALSE(settings.validate().has_value());
    CHECK(settings.provider_id() == "custom:servidor-nandu");
    CHECK(settings.credential_update()->first == "custom:servidor-nandu");
    const ConfigFileValues file = settings.file_values();
    CHECK(file.provider == "custom:servidor-nandu");
    REQUIRE(file.endpoints.size() == 2);
    CHECK(file.endpoints[1].id == "servidor-nandu");
    CHECK(file.endpoints[1].name == "Servidor Ñandú");

    // Tras guardar queda en la lista, elegido, y ya no es nuevo.
    const std::size_t index = settings.provider();
    settings.mark_saved();
    CHECK_FALSE(settings.is_new_endpoint());
    CHECK(settings.provider() == index);
    CHECK(settings.saved_provider() == index);
    CHECK(settings.provider_list()[index].id == "custom:servidor-nandu");
    CHECK(settings.provider_list().back().add);
    CHECK(settings.key_status() == "guardada: …1234");
    CHECK_FALSE(settings.dirty());

    // Con CHAT_BASE_URL la URL no se edita: no se agrega uno sin la suya.
    SettingsEnv env;
    env.base_url = "https://desde-entorno/v1";
    ProviderSettings locked(values, credentials_with("nvidia", kSecret), env);
    locked.select_provider(*locked.find_provider("custom:"));
    locked.set_name("Nuevo");
    CHECK_FALSE(locked.set_base_url("https://nuevo/v1"));
    locked.set_model("m");
    CHECK(locked.validate() == "Con CHAT_BASE_URL definida no se puede agregar un endpoint.");
    // Uno guardado sí se edita: conserva su URL.
    locked.select_provider(*locked.find_provider("lab"));
    locked.set_model("m");
    locked.set_key("sk-del-servidor-lab-00000");
    CHECK_FALSE(locked.validate().has_value());
    CHECK(locked.file_values().endpoints[0].base_url == "https://lab.example/v1");

    // Tope: con 20 no se agrega otro.
    ConfigFileValues full = saved("nvidia", "", "m");
    for (int i = 0; i < 20; ++i) {
        full.endpoints.push_back({"e" + std::to_string(i), "E" + std::to_string(i), "https://x/v1"});
    }
    ProviderSettings capped(full, {}, {});
    capped.select_provider(*capped.find_provider("custom:"));
    capped.set_name("Otro");
    CHECK(capped.validate() == "Ya tienes 20 endpoints; borra uno antes de agregar otro.");
    // Editar uno de los 20 sí se puede.
    capped.select_provider(*capped.find_provider("e3"));
    capped.set_model("m");
    capped.set_key("sk-del-servidor-e3-000000");
    CHECK_FALSE(capped.validate().has_value());
}

TEST_CASE("ajustes: el enmascarado nunca expone más de 4 caracteres", "[proveedor][ajustes]") {
    using chatbot::cli::mask_key;
    CHECK(mask_key(kSecret) == "…1234");
    CHECK(mask_key("") == "…");
    CHECK(mask_key("abcd") == "…");    // Corta: no se muestra nada.
    CHECK(mask_key("abcdefg") == "…"); // 7 < 8.
    CHECK(mask_key("abcdefgh") == "…efgh");
    CHECK(mask_key("abcdef\x1b[2J") == "…"); // Controles: nada.
    for (const std::string& key : {std::string{kSecret}, std::string{"nvapi-0123456789abcdef"},
                                  std::string{"x"}, std::string(100, 'k')}) {
        const std::string masked = mask_key(key);
        CHECK(masked.size() <= std::string{"…"}.size() + 4);
    }
    // Ni el estado ni nada de lo que se dibuja trae la key entera.
    ProviderSettings settings(saved("nvidia", "", ""), credentials_with("nvidia", kSecret), {});
    CHECK(settings.key_status().find("clave-secreta") == std::string::npos);
    SettingsEnv env;
    env.api_key = kSecret;
    ProviderSettings locked(saved("nvidia", "", ""), {}, env);
    CHECK(locked.key_status() == "(definido por CHAT_API_KEY) …1234");
}

TEST_CASE("ajustes: CHAT_* bloquean sus campos", "[proveedor][ajustes]") {
    SettingsEnv env;
    env.base_url = "https://desde-entorno/v1";
    env.model = "modelo-entorno";
    env.api_key = "clave-del-entorno-9999";
    ConfigFileValues values = lab("https://guardada/v1", "guardado");
    ProviderSettings settings(values, credentials_with("custom:lab", kSecret), env);
    CHECK(settings.base_url_locked());
    CHECK_FALSE(settings.base_url_editable());
    CHECK_FALSE(settings.set_base_url("https://otra/v1"));
    CHECK(settings.effective_base_url() == "https://desde-entorno/v1");
    CHECK(settings.base_url() == "https://guardada/v1");
    CHECK(settings.key_locked());
    CHECK_FALSE(settings.set_key("nueva"));
    CHECK(settings.effective_key() == "clave-del-entorno-9999");
    CHECK_FALSE(settings.credential_update().has_value());
    CHECK(settings.model_locked());
    CHECK_FALSE(settings.set_model("otro"));
    settings.models_loaded({"a", "b"});
    CHECK_FALSE(settings.pick_highlighted());
    CHECK(settings.effective_model() == "modelo-entorno");
    // Al guardar, el modelo de config.json no se pisa con el del entorno.
    CHECK(settings.file_values().model == "guardado");
    const chatbot::Config config = settings.request_config({});
    CHECK(config.base_url == "https://desde-entorno/v1");
    CHECK(config.api_key == "clave-del-entorno-9999");
    CHECK(config.model == "modelo-entorno");
    CHECK_FALSE(settings.validate().has_value());
}

TEST_CASE("ajustes: filtro de modelos, resaltado y elección", "[proveedor][ajustes]") {
    ProviderSettings settings(saved("gemini", "", "gemini-3.5-flash-lite"), {}, {});
    CHECK(settings.models_state() == ModelsState::Idle);
    settings.models_loading();
    CHECK(settings.models_state() == ModelsState::Loading);
    settings.models_loaded({"gemini-3.5-flash", "gemini-3.5-flash-lite", "Gemini-Pro", "embed"});
    CHECK(settings.models_state() == ModelsState::Loaded);
    CHECK(settings.highlighted() == 1); // Empieza en el elegido.

    settings.set_filter("GEMINI");
    CHECK(settings.filtered_models() ==
          std::vector<std::string>{"gemini-3.5-flash", "gemini-3.5-flash-lite", "Gemini-Pro"});
    settings.set_filter("flash");
    CHECK(settings.filtered_models().size() == 2);
    settings.move_highlight(5);
    CHECK(settings.highlighted() == 1);
    settings.move_highlight(-9);
    CHECK(settings.highlighted() == 0);
    REQUIRE(settings.pick_highlighted());
    CHECK(settings.model() == "gemini-3.5-flash");
    settings.set_filter("nada-coincide");
    CHECK(settings.filtered_models().empty());
    CHECK_FALSE(settings.pick_highlighted());

    // Si /models falla, el modelo se escribe a mano.
    settings.models_failed("[error de autenticación] Unauthorized");
    CHECK(settings.models_state() == ModelsState::Failed);
    CHECK(settings.models_error() == "[error de autenticación] Unauthorized");
    CHECK(settings.set_model("claude-a-mano"));
    CHECK(settings.effective_model() == "claude-a-mano");
}

TEST_CASE("ajustes: cambios sin guardar y validación", "[proveedor][ajustes]") {
    ProviderSettings settings(saved("nvidia", "https://integrate.api.nvidia.com/v1", "m"),
                              credentials_with("nvidia", kSecret), {});
    CHECK_FALSE(settings.dirty());
    CHECK_FALSE(settings.validate().has_value());
    settings.set_filter("x"); // El filtro no es un cambio.
    CHECK_FALSE(settings.dirty());
    settings.set_model("otro");
    CHECK(settings.dirty());
    settings.set_model("m");
    CHECK_FALSE(settings.dirty());
    settings.set_key("nueva-clave-escrita");
    CHECK(settings.dirty());
    settings.set_key("");
    settings.select_provider(provider_index("openai"));
    CHECK(settings.dirty());
    // Sin key ni modelo para OpenAI: no se puede guardar.
    REQUIRE(settings.validate().has_value());
    CHECK_THAT(*settings.validate(), Catch::Matchers::ContainsSubstring("API key"));
    CHECK_FALSE(settings.can_request_models());
    settings.set_key("sk-openai-escrita-0000");
    REQUIRE(settings.validate().has_value());
    CHECK_THAT(*settings.validate(), Catch::Matchers::ContainsSubstring("modelo"));
    settings.set_model("gpt");
    CHECK_FALSE(settings.validate().has_value());

    // Guardar: lo actual pasa a ser lo guardado; la key queda en credentials.
    const ConfigFileValues values = settings.file_values();
    CHECK(values.provider == "openai");
    CHECK(values.base_url == "https://api.openai.com/v1");
    CHECK(values.model == "gpt");
    settings.mark_saved();
    CHECK_FALSE(settings.dirty());
    CHECK(settings.key().empty());
    CHECK(settings.key_status() == "guardada: …0000");

}

TEST_CASE("ajustes: una key que parece variable o incompleta no se guarda",
          "[proveedor][ajustes]") {
    const std::string kMessage =
        "Eso parece el nombre de una variable o una key incompleta; pega la key completa.";
    ProviderSettings settings(saved("nvidia", "https://integrate.api.nvidia.com/v1", "m"),
                              credentials_with("nvidia", kSecret), {});
    const std::string rejected[] = {
        "$NIMKEY",
        "  ",
        "abc",
        "$" + std::string(40, 'a'),                     // Larga, pero empieza con "$".
        std::string(30, 'a') + " " + std::string(5, 'b'), // Con un espacio.
        std::string(30, 'a') + "\n",                    // Con un salto de línea.
        std::string(19, 'k'),                           // Un carácter menos del mínimo.
    };
    for (const std::string& key : rejected) {
        INFO("largo " << key.size());
        settings.set_key(key);
        const std::optional<std::string> error = settings.validate();
        REQUIRE(error.has_value());
        CHECK(*error == kMessage); // Nunca incluye el valor.
        CHECK_FALSE(settings.credential_update().has_value());
    }

    const std::string accepted[] = {std::string(70, 'x'), "nvapi-" + std::string(64, 'Z'),
                                    std::string(20, 'k')};
    for (const std::string& key : accepted) {
        INFO("largo " << key.size());
        settings.set_key(key);
        CHECK_FALSE(settings.validate().has_value());
        CHECK(settings.credential_update().has_value());
    }

    // Sin key escrita se conserva la guardada: no se valida.
    settings.set_key("");
    CHECK_FALSE(settings.validate().has_value());
    // Con CHAT_API_KEY definida, la del formulario no cuenta.
    SettingsEnv env;
    env.api_key = "$EN_EL_ENTORNO";
    ProviderSettings locked(saved("nvidia", "https://integrate.api.nvidia.com/v1", "m"),
                            credentials_with("nvidia", kSecret), env);
    CHECK_FALSE(locked.set_key("abc"));
    CHECK_FALSE(locked.validate().has_value());
}

TEST_CASE("ajustes: key vacía válida en Ollama y en un host local", "[proveedor][ajustes]") {
    for (const ConfigFileValues& values : {
             saved("ollama", "http://localhost:11434/v1", "m"),
             lab("http://localhost:8080/v1")}) {
        ProviderSettings settings(values, {}, {});
        CHECK_FALSE(settings.validate().has_value());
        CHECK_FALSE(settings.credential_update().has_value());
    }
}

TEST_CASE("ajustes: aviso de URL base sin ruta, sin bloquear", "[proveedor][ajustes]") {
    for (const char* url : {"https://h", "https://h/", "https://h:8443", "http://localhost:8080"}) {
        INFO(url);
        ProviderSettings settings(lab(url), credentials_with("custom:lab", kSecret), {});
        const std::optional<std::string> hint = settings.base_url_hint();
        REQUIRE(hint.has_value());
        CHECK_THAT(*hint, Catch::Matchers::ContainsSubstring("/v1"));
        // Solo es un aviso: se puede guardar y pedir modelos.
        CHECK(settings.validate() == std::nullopt);
        CHECK(settings.can_request_models());
    }
    for (const char* url :
         {"https://h/v1", "https://h:8443/openai/v1", "http://h", "no es url", ""}) {
        INFO(url);
        ProviderSettings settings(lab("https://h/v1"), {}, {});
        (void)settings.set_base_url(url); // Las inválidas no se cargan de config.json.
        CHECK_FALSE(settings.base_url_hint().has_value());
    }

    // Se actualiza al escribir.
    ProviderSettings typing(lab("https://h/v1"), {}, {});
    CHECK_FALSE(typing.base_url_hint().has_value());
    REQUIRE(typing.set_base_url("https://h"));
    CHECK(typing.base_url_hint().has_value());

    // Un proveedor conocido nunca, aunque CHAT_BASE_URL no tenga ruta.
    for (std::size_t i = 0; i < chatbot::providers().size(); ++i) {
        ProviderSettings settings(lab("https://h"), {}, {});
        settings.select_provider(i);
        CHECK_FALSE(settings.base_url_hint().has_value());
        SettingsEnv env;
        env.base_url = "https://h";
        ProviderSettings locked(saved(std::string{chatbot::providers()[i].id}, "", "m"), {},
                                env);
        CHECK_FALSE(locked.base_url_hint().has_value());
    }

    // En un endpoint cuenta la URL efectiva: CHAT_BASE_URL.
    SettingsEnv env;
    env.base_url = "https://h";
    ProviderSettings locked(lab("https://h/v1"), {}, env);
    CHECK(locked.base_url_hint().has_value());
}
