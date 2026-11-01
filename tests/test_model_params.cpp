#include "chatbot/chat_client.h"
#include "chatbot/config.h"
#include "chatbot/model_params.h"
#include "fake_transport.hpp"
#include "model_params_form.h"
#include "temp_dir.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using chatbot::ModelParams;
using chatbot::Reasoning;
using ordered = nlohmann::ordered_json;

std::string write_config(const chatbot_test::ScopedTempDir& dir, const std::string& content) {
    const std::string path = (dir.path() / "config.json").string();
    std::ofstream{path} << content;
    return path;
}

std::string read_text(const std::string& path) {
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

/// El cuerpo que manda ChatClient con estos ajustes (stream: true).
ordered sent_body(const std::string& provider, const ModelParams& params) {
    chatbot::Config config = chatbot_test::test_config();
    config.provider = provider;
    config.model_params = params;
    auto harness = chatbot_test::make_client(config);
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200, "data: {\"choices\":[{\"delta\":{\"content\":\"x\"}}]}\n\ndata: [DONE]\n\n",
        std::nullopt, false, ""});
    (void)harness.client->complete_stream(chatbot_test::sample_messages(),
                                          [](std::string_view) { return true; });
    REQUIRE(harness.transport->requests.size() == 1);
    return ordered::parse(harness.transport->requests.front().body);
}

std::vector<std::string> keys_of(const ordered& object) {
    std::vector<std::string> keys;
    for (const auto& item : object.items()) {
        keys.push_back(item.key());
    }
    return keys;
}

} // namespace

TEST_CASE("Sin ajustes, el cuerpo es el de antes (mismo JSON, en el orden de la petición)",
          "[ajustes]") {
    auto harness = chatbot_test::make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});
    REQUIRE(harness.client->complete(chatbot_test::sample_messages()).is_ok());
    const std::string body = harness.transport->requests.front().body;
    CHECK(body == R"({"model":"modelo-de-prueba","stream":false,"messages":[{"role":"system",)"
                  R"("content":"Eres un asistente de prueba."},{"role":"user","content":"Hola"}]})");
    // El cuerpo de main (nlohmann::json, llaves en orden alfabético), parseado, es el mismo.
    const std::string main_body =
        R"({"messages":[{"content":"Eres un asistente de prueba.","role":"system"},)"
        R"({"content":"Hola","role":"user"}],"model":"modelo-de-prueba","stream":false})";
    CHECK(nlohmann::json::parse(body) == nlohmann::json::parse(main_body));
}

TEST_CASE("Los parámetros van después de messages, en el orden del formulario", "[ajustes]") {
    ModelParams params;
    params.reasoning = Reasoning::High;
    params.temperature = 0.6;
    params.top_p = 0.95;
    params.top_k = 40;
    params.max_tokens = 2000;
    params.frequency_penalty = -1.5;
    params.presence_penalty = 0.5;
    params.seed = 7;
    const ordered body = sent_body("groq", params);
    CHECK(keys_of(body) ==
          std::vector<std::string>{"model", "stream", "messages", "temperature", "top_p", "top_k",
                                   "max_tokens", "frequency_penalty", "presence_penalty", "seed",
                                   "reasoning_effort"});
    CHECK(body.at("temperature") == 0.6);
    CHECK(body.at("top_k") == 40);
    CHECK(body.at("seed") == 7);
    CHECK(body.at("reasoning_effort") == "high");
}

TEST_CASE("max_completion_tokens solo en openai", "[ajustes]") {
    ModelParams params;
    params.max_tokens = 100;
    const ordered openai = sent_body("openai", params);
    CHECK(openai.at("max_completion_tokens") == 100);
    CHECK_FALSE(openai.contains("max_tokens"));
    for (const char* provider : {"nvidia", "gemini", "anthropic", "custom:local", ""}) {
        const ordered body = sent_body(provider, params);
        CHECK(body.at("max_tokens") == 100);
        CHECK_FALSE(body.contains("max_completion_tokens"));
    }
}

TEST_CASE("Tabla del razonamiento, fila por fila", "[ajustes][razonamiento]") {
    using chatbot::reasoning_fields;
    for (const char* provider : {"openai", "gemini"}) {
        CHECK(reasoning_fields(provider, Reasoning::Low) == R"({"reasoning_effort":"low"})");
        CHECK(reasoning_fields(provider, Reasoning::Medium) == R"({"reasoning_effort":"medium"})");
        CHECK(reasoning_fields(provider, Reasoning::High) == R"({"reasoning_effort":"high"})");
        CHECK(reasoning_fields(provider, Reasoning::Off) == R"({"reasoning_effort":"none"})");
    }
    CHECK(reasoning_fields("openrouter", Reasoning::Low) == R"({"reasoning":{"effort":"low"}})");
    CHECK(reasoning_fields("openrouter", Reasoning::Medium) ==
          R"({"reasoning":{"effort":"medium"}})");
    CHECK(reasoning_fields("openrouter", Reasoning::High) == R"({"reasoning":{"effort":"high"}})");
    CHECK(reasoning_fields("openrouter", Reasoning::Off) == R"({"reasoning":{"effort":"none"}})");
    for (const Reasoning level : {Reasoning::Low, Reasoning::Medium, Reasoning::High}) {
        CHECK(reasoning_fields("nvidia", level) ==
              R"({"chat_template_kwargs":{"enable_thinking":true}})");
    }
    CHECK(reasoning_fields("nvidia", Reasoning::Off) ==
          R"({"chat_template_kwargs":{"enable_thinking":false}})");
    CHECK(reasoning_fields("anthropic", Reasoning::Low) ==
          R"({"thinking":{"type":"enabled","budget_tokens":1024}})");
    CHECK(reasoning_fields("anthropic", Reasoning::Medium) ==
          R"({"thinking":{"type":"enabled","budget_tokens":4096}})");
    CHECK(reasoning_fields("anthropic", Reasoning::High) ==
          R"({"thinking":{"type":"enabled","budget_tokens":16384}})");
    CHECK(reasoning_fields("anthropic", Reasoning::Off) == "{}");
    for (const char* provider : {"groq", "mistral", "deepseek", "ollama", "custom:omniroute"}) {
        CHECK(reasoning_fields(provider, Reasoning::Medium) == R"({"reasoning_effort":"medium"})");
        CHECK(reasoning_fields(provider, Reasoning::Off) == R"({"reasoning_effort":"none"})");
    }
    for (const char* provider : {"openai", "nvidia", "anthropic", "openrouter", "custom:x"}) {
        CHECK(reasoning_fields(provider, Reasoning::Default) == "{}");
    }
    CHECK(chatbot::reasoning_style("nvidia") == chatbot::ReasoningStyle::Toggle);
    CHECK(chatbot::reasoning_style("anthropic") == chatbot::ReasoningStyle::Budget);
    CHECK(chatbot::reasoning_style("custom:x") == chatbot::ReasoningStyle::Effort);
}

TEST_CASE("Anthropic: el máximo de salida con y sin valor", "[ajustes][razonamiento]") {
    ModelParams params;
    params.reasoning = Reasoning::Medium;
    SECTION("Predeterminado: presupuesto + 4096") {
        const ordered body = sent_body("anthropic", params);
        CHECK(body.at("max_tokens") == 4096 + 4096);
        CHECK(body.at("thinking").at("budget_tokens") == 4096);
        CHECK(chatbot::validate_model_params(params, "anthropic").empty());
    }
    SECTION("Un máximo mayor que el presupuesto se manda tal cual") {
        params.max_tokens = 5000;
        CHECK(chatbot::validate_model_params(params, "anthropic").empty());
        CHECK(sent_body("anthropic", params).at("max_tokens") == 5000);
    }
    SECTION("Un máximo que no pasa del presupuesto es error") {
        params.max_tokens = 4096;
        const auto problems = chatbot::validate_model_params(params, "anthropic");
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].field == "max_tokens");
        CHECK(problems[0].message ==
              "Con el razonamiento medio, el máximo de salida debe pasar de 4096 tokens.");
        // En otro proveedor no hay presupuesto.
        CHECK(chatbot::validate_model_params(params, "nvidia").empty());
    }
    SECTION("Apagado no manda thinking ni max_tokens") {
        params.reasoning = Reasoning::Off;
        const ordered body = sent_body("anthropic", params);
        CHECK_FALSE(body.contains("thinking"));
        CHECK_FALSE(body.contains("max_tokens"));
    }
}

TEST_CASE("Rangos de los campos", "[ajustes]") {
    const auto problem = [](ModelParams params, const char* provider = "nvidia") {
        const auto problems = chatbot::validate_model_params(params, provider);
        return problems.empty() ? std::string{} : problems.front().field + ": " +
                                                      problems.front().message;
    };
    ModelParams params;
    params.temperature = 2;
    CHECK(problem(params).empty());
    CHECK(problem(params, "anthropic") == "temperature: Debe ser un número entre 0 y 1.");
    params.temperature = -0.1;
    CHECK(problem(params) == "temperature: Debe ser un número entre 0 y 2.");
    params = {};
    params.top_p = 0;
    CHECK(problem(params) == "top_p: Debe ser mayor que 0 y como máximo 1.");
    params.top_p = 1;
    CHECK(problem(params).empty());
    params = {};
    params.top_k = 1001;
    CHECK(problem(params) == "top_k: Debe ser un número entero entre 1 y 1000.");
    params = {};
    params.max_tokens = 0;
    CHECK(problem(params) == "max_tokens: Debe ser un número entero entre 1 y 1000000.");
    params = {};
    params.frequency_penalty = 2.5;
    CHECK(problem(params) == "frequency_penalty: Debe ser un número entre -2 y 2.");
    params = {};
    params.presence_penalty = -2;
    CHECK(problem(params).empty());
    params = {};
    params.seed = 2147483648;
    CHECK(problem(params) == "seed: Debe ser un número entero entre 0 y 2147483647.");
    params = {};
    params.temperature = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(problem(params).empty());
}

TEST_CASE("JSON avanzado: merge patch al final", "[ajustes][extra]") {
    ModelParams params;
    params.reasoning = Reasoning::High;
    params.temperature = 0.3;
    SECTION("Gana extra y lo nuevo va al final") {
        params.extra = R"({"temperature": 0.9, "repetition_penalty": 1.1})";
        const ordered body = sent_body("custom:x", params);
        CHECK(body.at("temperature") == 0.9);
        CHECK(keys_of(body).back() == "repetition_penalty");
    }
    SECTION("Los objetos se mezclan por dentro") {
        params.extra = R"({"chat_template_kwargs": {"clear_thinking": false}})";
        const ordered body = sent_body("nvidia", params);
        CHECK(body.at("chat_template_kwargs") ==
              ordered::parse(R"({"enable_thinking": true, "clear_thinking": false})"));
    }
    SECTION("null borra lo que puso el formulario") {
        params.extra = R"({"reasoning_effort": null, "chat_template_kwargs": {"thinking": true}})";
        const ordered body = sent_body("custom:x", params);
        CHECK_FALSE(body.contains("reasoning_effort"));
        CHECK(body.at("chat_template_kwargs").at("thinking") == true);
    }
}

TEST_CASE("JSON avanzado: validación", "[ajustes][extra]") {
    const auto extra_error = [](std::string text) {
        ModelParams params;
        params.extra = std::move(text);
        const auto problems = chatbot::validate_model_params(params, "nvidia");
        return problems.empty() ? std::string{} : problems.front().message;
    };
    CHECK(extra_error("").empty());
    CHECK(extra_error("  \n").empty());
    CHECK(extra_error("{}").empty());
    for (const char* key : {"model", "messages", "stream", "stream_options", "tools",
                            "tool_choice"}) {
        CHECK(extra_error(std::string{"{\""} + key + "\": 1}") ==
              std::string{"«"} + key +
                  "» la pone Moss; no se puede cambiar desde el JSON avanzado.");
    }
    CHECK(extra_error("{\n  \"a\": 1,\n  \"b\" 2\n}") ==
          "El JSON avanzado no es válido (línea 3, columna 7).");
    CHECK(extra_error("{\"a\": ") == "El JSON avanzado no es válido (línea 1, columna 7).");
    CHECK(extra_error(R"({"a": [1, {"b": 1e999}]})") ==
          "El JSON avanzado tiene un número demasiado grande.");
    CHECK(extra_error("[1, 2]") == "El JSON avanzado tiene que ser un objeto: {…}.");
    CHECK(extra_error("{\"a\": \"\xC3\x28\"}") == "El JSON avanzado no es UTF-8 válido.");
    const std::string big = "{\"a\": \"" + std::string(chatbot::kMaxExtraBytes, 'x') + "\"}";
    CHECK(extra_error(big) == "El JSON avanzado pasa de 8 KiB.");
    // Un {} o solo espacios no cuentan como ajuste.
    ModelParams params;
    params.extra = " {} ";
    CHECK(params.empty());
}

TEST_CASE("Vista previa: el cuerpo exacto sin messages ni key", "[ajustes][vista]") {
    ModelParams params;
    params.temperature = 0.3;
    params.top_p = 0.9;
    params.reasoning = Reasoning::High;
    params.extra = R"({"chat_template_kwargs": {"thinking": true}})";
    const std::string preview = chatbot::request_preview("z-ai/glm5", "nvidia", params, 3);
    CHECK(preview == "{\n"
                     "  \"model\": \"z-ai/glm5\",\n"
                     "  \"stream\": true,\n"
                     "  \"messages\": \"[…3 mensajes…]\",\n"
                     "  \"temperature\": 0.3,\n"
                     "  \"top_p\": 0.9,\n"
                     "  \"chat_template_kwargs\": {\n"
                     "    \"enable_thinking\": true,\n"
                     "    \"thinking\": true\n"
                     "  }\n"
                     "}");
    CHECK_THAT(chatbot::request_preview("m", "nvidia", {}, 1),
               Catch::Matchers::ContainsSubstring("\"[…1 mensaje…]\""));

    // Igual al cuerpo que se manda, salvo messages, y nunca con la key.
    chatbot::Config config = chatbot_test::test_config();
    config.provider = "nvidia";
    config.model = "z-ai/glm5";
    config.model_params = params;
    auto harness = chatbot_test::make_client(config);
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200, "data: [DONE]\n\n", std::nullopt, false, ""});
    (void)harness.client->complete_stream(chatbot_test::sample_messages(),
                                          [](std::string_view) { return true; });
    ordered sent = ordered::parse(harness.transport->requests.front().body);
    sent["messages"] = "[…2 mensajes…]";
    CHECK(sent.dump(2) == chatbot::request_preview("z-ai/glm5", "nvidia", params, 2));
    CHECK(preview.find(config.api_key) == std::string::npos);
}

TEST_CASE("Cargar \"models\" del modelo efectivo", "[ajustes][config]") {
    const chatbot_test::ScopedTempDir dir;
    std::vector<std::string> warnings;
    const auto load = [&](const std::string& content, const char* provider = "nvidia",
                          const char* model = "z-ai/glm5") {
        warnings.clear();
        const std::string path = write_config(dir, content);
        const auto loaded = chatbot::load_model_params(path, provider, model, warnings);
        REQUIRE(loaded.is_ok());
        return loaded.value();
    };

    SECTION("Válido") {
        const ModelParams params = load(R"({"models": {"nvidia": {"z-ai/glm5": {
            "reasoning": "high", "temperature": 0.6, "top_p": 0.95, "top_k": 20, "seed": 3,
            "extra": {"chat_template_kwargs": {"clear_thinking": false}}}}}})");
        CHECK(warnings.empty());
        CHECK(params.reasoning == Reasoning::High);
        CHECK(params.temperature == 0.6);
        CHECK(params.top_p == 0.95);
        CHECK(params.top_k == 20);
        CHECK(params.seed == 3);
        CHECK(params.extra == R"({"chat_template_kwargs":{"clear_thinking":false}})");
        // Otro modelo u otro proveedor: sin ajustes.
        CHECK(load(R"({"models": {"nvidia": {"z-ai/glm5": {"temperature": 1}}}})", "nvidia",
                   "otro")
                  .empty());
        CHECK(load(R"({"models": {"nvidia": {"z-ai/glm5": {"temperature": 1}}}})", "openai")
                  .empty());
    }
    SECTION("Tipos malos y fuera de rango quedan en Predeterminado con aviso") {
        const ModelParams params = load(R"({"models": {"nvidia": {"z-ai/glm5": {
            "reasoning": "máximo", "temperature": "0.5", "top_p": 0.5, "top_k": 2.5,
            "max_tokens": 0, "seed": -1, "extra": [1], "otra": 1}}}})");
        CHECK(params.top_p == 0.5);
        ModelParams expected;
        expected.top_p = 0.5;
        CHECK(params == expected);
        CHECK(warnings == std::vector<std::string>{
                              "Ajustes de z-ai/glm5: reasoning no es válido; se usa el predeterminado.",
                              "Ajustes de z-ai/glm5: temperature no es válido; se usa el predeterminado.",
                              "Ajustes de z-ai/glm5: top_k no es válido; se usa el predeterminado.",
                              "Ajustes de z-ai/glm5: extra no es válido; se usa el predeterminado.",
                              "Ajustes de z-ai/glm5: se ignoró «otra», que no es un ajuste.",
                              "Ajustes de z-ai/glm5: max_tokens no es válido; se usa el predeterminado.",
                              "Ajustes de z-ai/glm5: seed no es válido; se usa el predeterminado.",
                          });
    }
    SECTION("extra con una llave reservada se ignora") {
        const ModelParams params =
            load(R"({"models": {"nvidia": {"z-ai/glm5": {"extra": {"model": "x"}, "seed": 1}}}})");
        CHECK(params.extra.empty());
        CHECK(params.seed == 1);
        CHECK(warnings.size() == 1);
    }
    SECTION("\"models\" que no es objeto se ignora completo") {
        CHECK(load(R"({"models": [1]})").empty());
        CHECK(warnings ==
              std::vector<std::string>{"Se ignoró \"models\" de config.json: no es un objeto."});
    }
    SECTION("Proveedor sin modelos o sin \"models\": sin ajustes ni avisos") {
        CHECK(load(R"({"models": {"nvidia": {}}})").empty());
        CHECK(load(R"({"provider": "nvidia"})").empty());
        CHECK(warnings.empty());
    }
    SECTION("Proveedor o modelo que no son objetos") {
        CHECK(load(R"({"models": {"nvidia": 3}})").empty());
        CHECK(warnings == std::vector<std::string>{
                              "Ajustes de z-ai/glm5: no son un objeto; se usan los predeterminados."});
    }
}

TEST_CASE("load_config trae los ajustes del modelo efectivo, también con CHAT_MODEL",
          "[ajustes][config]") {
    const chatbot_test::ScopedTempDir dir;
    const std::string path = write_config(dir, R"({"provider": "nvidia", "model": "a",
        "models": {"nvidia": {"a": {"temperature": 0.1}, "b": {"temperature": 0.2,
        "top_k": 5000}}}})");
    chatbot_test::FakeEnv env;
    env.values["CHAT_API_KEY"] = "clave-ficticia";
    chatbot::ConfigOptions options;
    options.env = std::ref(env);
    options.path = [path] { return std::optional<std::string>{path}; };
    options.credentials_path = [] { return std::optional<std::string>{}; };
    auto loaded = chatbot::load_config(options);
    REQUIRE(loaded.is_ok());
    CHECK(loaded.value().model_params.temperature == 0.1);
    CHECK(loaded.value().warnings.empty());

    env.values["CHAT_MODEL"] = "b";
    loaded = chatbot::load_config(options);
    REQUIRE(loaded.is_ok());
    CHECK(loaded.value().model_params.temperature == 0.2);
    CHECK_FALSE(loaded.value().model_params.top_k.has_value());
    CHECK(loaded.value().warnings ==
          std::vector<std::string>{
              "Ajustes de b: top_k no es válido; se usa el predeterminado."});
}

TEST_CASE("Guardar quita lo Predeterminado y conserva las demás llaves", "[ajustes][config]") {
    const chatbot_test::ScopedTempDir dir;
    const std::string path = write_config(dir, R"({"provider": "nvidia", "zzz": [1],
        "models": {"nvidia": {"otro": {"seed": 1}}}})");
    ModelParams params;
    params.temperature = 0.6;
    params.reasoning = Reasoning::Off;
    params.extra = "{\n  \"a\": {\"b\": null}\n}";
    REQUIRE_FALSE(chatbot::save_model_params(path, "nvidia", "z-ai/glm5", params).has_value());
    ordered document = ordered::parse(read_text(path));
    CHECK(keys_of(document) == std::vector<std::string>{"provider", "zzz", "models"});
    CHECK(document.at("models").at("nvidia").at("z-ai/glm5") ==
          ordered::parse(R"({"reasoning": "off", "temperature": 0.6, "extra": {"a": {"b": null}}})"));
    CHECK(document.at("models").at("nvidia").at("otro").at("seed") == 1);

    std::vector<std::string> warnings;
    const auto loaded = chatbot::load_model_params(path, "nvidia", "z-ai/glm5", warnings);
    REQUIRE(loaded.is_ok());
    CHECK(loaded.value().temperature == 0.6);
    CHECK(loaded.value().extra == R"({"a":{"b":null}})");

    // Restaurar todo quita el modelo; el último modelo quita el proveedor y "models".
    REQUIRE_FALSE(chatbot::save_model_params(path, "nvidia", "z-ai/glm5", {}).has_value());
    document = ordered::parse(read_text(path));
    CHECK_FALSE(document.at("models").at("nvidia").contains("z-ai/glm5"));
    REQUIRE_FALSE(chatbot::save_model_params(path, "nvidia", "otro", {}).has_value());
    document = ordered::parse(read_text(path));
    CHECK_FALSE(document.contains("models"));
    CHECK(keys_of(document) == std::vector<std::string>{"provider", "zzz"});

    // Los demás escritores conservan "models".
    REQUIRE_FALSE(chatbot::save_model_params(path, "nvidia", "m", params).has_value());
    REQUIRE_FALSE(chatbot::save_config_file(path, {"nvidia", "https://x.invalid/v1", "m", {}})
                      .has_value());
    REQUIRE_FALSE(chatbot::save_system_prompt(path, std::string{"hola"}).has_value());
    CHECK(ordered::parse(read_text(path)).at("models").at("nvidia").contains("m"));
}

TEST_CASE("Guardar no toca un \"models\" inválido", "[ajustes][config]") {
    const chatbot_test::ScopedTempDir dir;
    const std::string content = R"({"models": {"nvidia": "x"}})";
    const std::string path = write_config(dir, content);
    ModelParams params;
    params.seed = 1;
    const auto error = chatbot::save_model_params(path, "nvidia", "m", params);
    REQUIRE(error.has_value());
    CHECK(error->kind == chatbot::ErrorKind::Config);
    CHECK(read_text(path) == content);
}

TEST_CASE("Quitar los ajustes de un proveedor", "[ajustes][config]") {
    const chatbot_test::ScopedTempDir dir;
    const std::string path = write_config(
        dir, R"({"models": {"custom:a": {"m": {"seed": 1}}, "nvidia": {"m": {"seed": 2}}}})");
    REQUIRE_FALSE(chatbot::remove_provider_params(path, "custom:a").has_value());
    ordered document = ordered::parse(read_text(path));
    CHECK_FALSE(document.at("models").contains("custom:a"));
    CHECK(document.at("models").contains("nvidia"));
    REQUIRE_FALSE(chatbot::remove_provider_params(path, "nvidia").has_value());
    CHECK_FALSE(ordered::parse(read_text(path)).contains("models"));
    REQUIRE_FALSE(chatbot::remove_provider_params(path, "nvidia").has_value());
}

TEST_CASE("Leer y escribir el objeto de ajustes", "[ajustes]") {
    const auto parsed = chatbot::model_params_from_json(
        R"({"reasoning": null, "temperature": 1, "seed": 9223372036854775808, "x": 1})");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().params.temperature == 1.0);
    CHECK(parsed.value().invalid_fields == std::vector<std::string>{"seed"});
    CHECK(parsed.value().unknown_fields == std::vector<std::string>{"x"});
    CHECK(chatbot::model_params_from_json("[]").is_error());
    CHECK(chatbot::model_params_to_json({}) == "{}");
}

TEST_CASE("Formulario de Parámetros: textos, errores y Restaurar todo", "[ajustes][formulario]") {
    ModelParams saved;
    saved.temperature = 0.6;
    saved.seed = 42;
    saved.reasoning = Reasoning::Medium;
    saved.extra = R"({"a":1})";
    chatbot::cli::ModelParamsForm form{saved};
    CHECK(form.numbers[0] == "0.6");
    CHECK(form.numbers[6] == "42");
    CHECK(form.reasoning == 3);
    CHECK(form.extra == "{\n  \"a\": 1\n}");
    CHECK_FALSE(form.dirty());
    CHECK(form.draft() == ModelParams{Reasoning::Medium, 0.6, {}, {}, {}, {}, {}, 42,
                                      "{\n  \"a\": 1\n}"});

    form.numbers[2] = "2.5"; // Top K tiene que ser entero.
    form.numbers[1] = " 0,9 ";
    form.numbers[3] = "1e3x";
    std::vector<chatbot::FieldProblem> problems;
    const ModelParams draft = form.draft(&problems);
    CHECK(draft.top_p == 0.9);
    CHECK_FALSE(draft.top_k.has_value());
    CHECK(problems == std::vector<chatbot::FieldProblem>{
                          {"top_k", "Escribe un número entero."},
                          {"max_tokens", "Escribe un número entero."}});
    CHECK(form.dirty());

    form.restore_all();
    CHECK(form.draft().empty());
    CHECK(form.dirty());
    form.mark_saved();
    CHECK_FALSE(form.dirty());
}

TEST_CASE("Ayuda del razonamiento por proveedor", "[ajustes][formulario]") {
    CHECK_THAT(chatbot::cli::reasoning_help("nvidia"),
               Catch::Matchers::ContainsSubstring("solo lo encienden"));
    CHECK_THAT(chatbot::cli::reasoning_help("anthropic"),
               Catch::Matchers::ContainsSubstring("1024, 4096 y 16384"));
    CHECK_THAT(chatbot::cli::reasoning_help("openrouter"),
               Catch::Matchers::ContainsSubstring("reasoning.effort"));
    CHECK_THAT(chatbot::cli::reasoning_help("custom:x"),
               Catch::Matchers::ContainsSubstring("reasoning_effort"));
    CHECK(chatbot::cli::limit_hint(chatbot::model_param_limits("x")[1]) == "más de 0, hasta 1");
    CHECK(chatbot::cli::limit_hint(chatbot::model_param_limits("anthropic")[0]) == "0 a 1");
}
