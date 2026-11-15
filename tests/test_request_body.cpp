#include "chatbot/chat_client.h"
#include "chatbot/trimmed_completion.h"
#include "fake_transport.hpp"
#include "tool_history.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {
using chatbot_test::make_client;
using chatbot_test::sample_messages;
using chatbot_test::test_config;
} // namespace

TEST_CASE("El cuerpo tiene model, stream false y los mensajes en orden", "[peticion]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    const chatbot::Result<std::string> response = harness.client->complete(sample_messages());
    REQUIRE(response.is_ok());
    REQUIRE(harness.transport->requests.size() == 1);

    const chatbot::HttpRequest& request = harness.transport->requests.front();
    const nlohmann::json body = nlohmann::json::parse(request.body);

    CHECK(body.at("model") == "modelo-de-prueba");
    CHECK(body.at("stream") == false);
    REQUIRE(body.at("messages").is_array());
    REQUIRE(body.at("messages").size() == 2);
    CHECK(body.at("messages")[0].at("role") == "system");
    CHECK(body.at("messages")[0].at("content") == "Eres un asistente de prueba.");
    CHECK(body.at("messages")[1].at("role") == "user");
    CHECK(body.at("messages")[1].at("content") == "Hola");
}

TEST_CASE("La URL es base_url + /chat/completions", "[peticion]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    (void)harness.client->complete(sample_messages());

    CHECK(harness.transport->requests.front().url ==
          "https://pruebas.invalid/v1/chat/completions");
}

TEST_CASE("La barra final de base_url se normaliza", "[peticion]") {
    chatbot::Config config = test_config();
    config.base_url = "https://pruebas.invalid/v1/";
    auto harness = make_client(config);
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    (void)harness.client->complete(sample_messages());

    CHECK(harness.transport->requests.front().url ==
          "https://pruebas.invalid/v1/chat/completions");
}

TEST_CASE("Los tres roles se serializan con su nombre correcto", "[peticion]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    const std::vector<chatbot::Message> messages{
        chatbot::Message{chatbot::Role::System, "s"},
        chatbot::Message{chatbot::Role::User, "u"},
        chatbot::Message{chatbot::Role::Assistant, "a"},
    };
    const chatbot::Result<std::string> response = harness.client->complete(messages);
    REQUIRE(response.is_ok());

    const nlohmann::json body = nlohmann::json::parse(harness.transport->requests.front().body);
    REQUIRE(body.at("messages").size() == 3);
    CHECK(body.at("messages")[0].at("role") == "system");
    CHECK(body.at("messages")[1].at("role") == "user");
    CHECK(body.at("messages")[2].at("role") == "assistant");
}

TEST_CASE("Historial vacío: error Config sin llamar al transporte", "[peticion]") {
    auto harness = make_client();
    const chatbot::Result<std::string> response = harness.client->complete({});

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Config);
    CHECK(harness.transport->requests.empty());
}

TEST_CASE("Config sin key: error Config sin llamar al transporte", "[peticion]") {
    chatbot::Config config = test_config();
    config.api_key.clear();
    auto harness = make_client(config);

    const chatbot::Result<std::string> response = harness.client->complete(sample_messages());
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Config);
    CHECK(harness.transport->requests.empty());
}

TEST_CASE("Config sin modelo: error Config sin llamar al transporte", "[peticion]") {
    chatbot::Config config = test_config();
    config.model.clear();
    auto harness = make_client(config);

    const chatbot::Result<std::string> response = harness.client->complete(sample_messages());
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Config);
    CHECK(harness.transport->requests.empty());
}

TEST_CASE("El timeout configurado llega al transporte en milisegundos", "[peticion]") {
    chatbot::Config config = test_config();
    config.timeout_seconds = std::chrono::seconds{45};
    auto harness = make_client(config);
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    (void)harness.client->complete(sample_messages());

    CHECK(harness.transport->requests.front().timeout == std::chrono::milliseconds{45000});
}

namespace {

/// Config fija para las pruebas doradas: ajustes del modelo, razonamiento
/// y JSON avanzado, para cubrir todo el camino de request_json.
chatbot::Config golden_config() {
    chatbot::Config config = test_config();
    config.provider = "nvidia";
    config.model_params.reasoning = chatbot::Reasoning::High;
    config.model_params.temperature = 0.5;
    config.model_params.max_tokens = 512;
    config.model_params.seed = 7;
    config.model_params.extra = R"({"custom":{"a":1}})";
    return config;
}

std::vector<chatbot::Message> golden_messages() {
    return {
        chatbot::Message{chatbot::Role::System, "Sé breve."},
        chatbot::Message{chatbot::Role::User, "Di \"hola\"\n"},
        chatbot::Message{chatbot::Role::Assistant, "¡Hola!"},
        chatbot::Message{chatbot::Role::User, "¿Qué tal?"},
    };
}

constexpr const char* kGoldenMessages =
    R"([{"role":"system","content":"Sé breve."},{"role":"user","content":"Di \"hola\"\n"},)"
    R"({"role":"assistant","content":"¡Hola!"},{"role":"user","content":"¿Qué tal?"}])";

constexpr const char* kGoldenParams =
    R"("temperature":0.5,"max_tokens":512,"seed":7,)"
    R"("chat_template_kwargs":{"enable_thinking":true},"custom":{"a":1}})";

std::string golden_body(bool stream) {
    return std::string{R"({"model":"modelo-de-prueba","stream":)"} + (stream ? "true" : "false") +
           R"(,"messages":)" + kGoldenMessages + "," + kGoldenParams;
}

chatbot::HttpResponse stream_done() {
    return chatbot::HttpResponse{200, "data: [DONE]\n\n", std::nullopt, false, ""};
}

} // namespace

TEST_CASE("Cuerpo dorado sin herramientas: complete, cadena exacta", "[peticion][dorada]") {
    auto harness = make_client(golden_config());
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});

    REQUIRE(harness.client->complete(golden_messages()).is_ok());
    REQUIRE(harness.transport->requests.size() == 1);
    CHECK(harness.transport->requests.front().body == golden_body(false));
}

TEST_CASE("Cuerpo dorado sin herramientas: complete_stream, cadena exacta", "[peticion][dorada]") {
    auto harness = make_client(golden_config());
    harness.transport->responses.push_back(stream_done());

    REQUIRE(harness.client
                ->complete_stream(golden_messages(), [](const chatbot::StreamDelta&) { return true; })
                .is_ok());
    REQUIRE(harness.transport->requests.size() == 1);
    CHECK(harness.transport->requests.front().body == golden_body(true));
}

namespace {

using Catch::Matchers::ContainsSubstring;

chatbot::Message assistant_calls(std::string content, std::vector<chatbot::ToolCall> calls) {
    chatbot::Message message{chatbot::Role::Assistant, std::move(content)};
    message.tool_calls = std::move(calls);
    return message;
}

chatbot::Message tool_result(std::string id, std::string content) {
    chatbot::Message message{chatbot::Role::Tool, std::move(content)};
    message.tool_call_id = std::move(id);
    return message;
}

chatbot::RequestOptions read_tool() {
    return chatbot::RequestOptions{
        {chatbot::ToolSpec{"read", "Lee un archivo", R"({"type":"object","properties":{}})"}},
        false};
}

constexpr const char* kReadToolJson =
    R"("tools":[{"type":"function","function":{"name":"read","description":"Lee un archivo",)"
    R"("parameters":{"type":"object","properties":{}}}}],"tool_choice":"auto")";

/// Lo que pasó al mandar con complete_stream: el resultado y el cuerpo de la
/// petición (nullopt si no llegó al transporte).
struct Sent {
    chatbot::Result<chatbot::CompletionInfo> result;
    std::optional<std::string> body;
};

Sent send_stream(const std::vector<chatbot::Message>& messages,
                 const chatbot::RequestOptions& options = {},
                 chatbot::Config config = test_config()) {
    auto harness = make_client(std::move(config));
    harness.transport->responses.push_back(stream_done());
    chatbot::Result<chatbot::CompletionInfo> result = harness.client->complete_stream(
        messages, [](const chatbot::StreamDelta&) { return true; }, nullptr, options);
    std::optional<std::string> body;
    if (!harness.transport->requests.empty()) {
        body = harness.transport->requests.front().body;
    }
    return Sent{std::move(result), std::move(body)};
}

/// Error Config sin mandar nada, con text en el mensaje.
void check_config_error(const Sent& sent, const std::string& text) {
    REQUIRE(sent.result.is_error());
    CHECK(sent.result.error().kind == chatbot::ErrorKind::Config);
    CHECK_THAT(sent.result.error().message, ContainsSubstring(text));
    CHECK_FALSE(sent.body.has_value());
}

/// El error de validate_tool_history, que tiene que existir y ser Config.
std::string history_error(const std::vector<chatbot::Message>& messages) {
    const std::optional<chatbot::ChatError> error = chatbot::validate_tool_history(messages);
    REQUIRE(error.has_value());
    CHECK(error->kind == chatbot::ErrorKind::Config);
    return error->message;
}

} // namespace

TEST_CASE("Con herramientas: tools y tool_choice auto, sin parallel_tool_calls", "[peticion][herramientas]") {
    const Sent sent = send_stream({chatbot::Message{chatbot::Role::User, "Hola"}}, read_tool());
    REQUIRE(sent.result.is_ok());
    REQUIRE(sent.body.has_value());
    CHECK(*sent.body == std::string{R"({"model":"modelo-de-prueba","stream":true,)"
                                    R"("messages":[{"role":"user","content":"Hola"}],)"} +
                            kReadToolJson + "}");
}

TEST_CASE("Llamadas y resultados: content null, su texto y mensaje tool exacto", "[peticion][herramientas]") {
    const std::vector<chatbot::Message> messages{
        chatbot::Message{chatbot::Role::User, "Lee a"},
        assistant_calls("Leo.", {chatbot::ToolCall{"call_1", "read", R"({"path":"a"})"}}),
        tool_result("call_1", "uno"),
        assistant_calls("", {chatbot::ToolCall{"call_2", "read", "{}"},
                             chatbot::ToolCall{"call_3", "glob", "{}"}}),
        tool_result("call_3", "tres"),
        tool_result("call_2", "dos"),
        chatbot::Message{chatbot::Role::Assistant, "Listo."},
    };
    const Sent sent = send_stream(messages);
    REQUIRE(sent.result.is_ok());
    REQUIRE(sent.body.has_value());
    CHECK(*sent.body ==
          R"({"model":"modelo-de-prueba","stream":true,"messages":[)"
          R"({"role":"user","content":"Lee a"},)"
          R"({"role":"assistant","content":"Leo.","tool_calls":[{"id":"call_1","type":"function",)"
          R"("function":{"name":"read","arguments":"{\"path\":\"a\"}"}}]},)"
          R"({"role":"tool","tool_call_id":"call_1","content":"uno"},)"
          R"({"role":"assistant","content":null,"tool_calls":[)"
          R"({"id":"call_2","type":"function","function":{"name":"read","arguments":"{}"}},)"
          R"({"id":"call_3","type":"function","function":{"name":"glob","arguments":"{}"}}]},)"
          R"({"role":"tool","tool_call_id":"call_3","content":"tres"},)"
          R"({"role":"tool","tool_call_id":"call_2","content":"dos"},)"
          R"({"role":"assistant","content":"Listo."}]})");
}

TEST_CASE("Mensaje tool sin contenido: error Config sin mandar nada", "[peticion][herramientas]") {
    check_config_error(send_stream({chatbot::Message{chatbot::Role::User, "Lee a"},
                                    assistant_calls("", {chatbot::ToolCall{"call_1", "read", "{}"}}),
                                    tool_result("call_1", "")}),
                       "sin contenido");
}

TEST_CASE("UTF-8 inválido en tool, arguments y pensamiento: U+FFFD", "[peticion][herramientas][utf8]") {
    chatbot::Message call =
        assistant_calls("", {chatbot::ToolCall{"call_1", "read", std::string("{\"p\":\"\xE9\"}")}});
    call.reasoning = std::string("pens\xE9");
    const Sent sent = send_stream({chatbot::Message{chatbot::Role::User, "Lee"}, call,
                                   tool_result("call_1", std::string("caf\xE9"))},
                                  chatbot::RequestOptions{{}, true});
    REQUIRE(sent.result.is_ok());
    REQUIRE(sent.body.has_value());
    const nlohmann::json body = nlohmann::json::parse(*sent.body);
    const nlohmann::json& assistant = body.at("messages")[1];
    CHECK(assistant.at("tool_calls")[0].at("function").at("arguments") ==
          "{\"p\":\"\xEF\xBF\xBD\"}");
    CHECK(assistant.at("reasoning_content") == "pens\xEF\xBF\xBD");
    CHECK(body.at("messages")[2].at("content") == "caf\xEF\xBF\xBD");
}

TEST_CASE("Pensamiento de regreso: cada asistente con su llave", "[peticion][pensamiento]") {
    chatbot::Message first{chatbot::Role::Assistant, "Uno."};
    first.reasoning = "pienso uno";
    first.reasoning_key = chatbot::ReasoningKey::ReasoningContent;
    chatbot::Message second{chatbot::Role::Assistant, "Dos."};
    second.reasoning = "pienso dos";
    second.reasoning_key = chatbot::ReasoningKey::Reasoning;
    const std::vector<chatbot::Message> with_reasoning{
        chatbot::Message{chatbot::Role::User, "a"}, first,
        chatbot::Message{chatbot::Role::User, "b"}, second,
        chatbot::Message{chatbot::Role::User, "c"}};

    SECTION("replay_reasoning true") {
        const Sent sent = send_stream(with_reasoning, chatbot::RequestOptions{{}, true});
        REQUIRE(sent.body.has_value());
        CHECK_THAT(*sent.body, ContainsSubstring(
                                   R"({"role":"assistant","content":"Uno.","reasoning_content":"pienso uno"})"));
        CHECK_THAT(*sent.body, ContainsSubstring(
                                   R"({"role":"assistant","content":"Dos.","reasoning":"pienso dos"})"));
    }
    SECTION("replay_reasoning false: el mismo cuerpo que sin pensamiento") {
        std::vector<chatbot::Message> without = with_reasoning;
        without[1].reasoning.clear();
        without[3].reasoning.clear();
        const Sent sent = send_stream(with_reasoning);
        const Sent plain = send_stream(without);
        REQUIRE(sent.body.has_value());
        REQUIRE(plain.body.has_value());
        CHECK(*sent.body == *plain.body);
        CHECK_THAT(*sent.body, !ContainsSubstring("pienso"));
    }
}

TEST_CASE("El JSON avanzado no pisa las herramientas del núcleo", "[peticion][herramientas]") {
    chatbot::Config config = test_config();
    config.model_params.extra = R"({"tools":[{"type":"function"}],"tool_choice":"none"})";
    const Sent sent = send_stream({chatbot::Message{chatbot::Role::User, "Hola"}}, read_tool(), config);
    REQUIRE(sent.body.has_value());
    CHECK(*sent.body == std::string{R"({"model":"modelo-de-prueba","stream":true,)"
                                    R"("messages":[{"role":"user","content":"Hola"}],)"} +
                            kReadToolJson + "}");
}

TEST_CASE("ToolSpec inválida: error Config sin mandar nada", "[peticion][herramientas]") {
    const std::vector<chatbot::Message> messages{chatbot::Message{chatbot::Role::User, "Hola"}};
    const auto with = [](std::vector<chatbot::ToolSpec> tools) {
        return chatbot::RequestOptions{std::move(tools), false};
    };
    SECTION("nombre vacío") {
        check_config_error(send_stream(messages, with({{"", "d", "{}"}})), "sin nombre");
    }
    SECTION("nombre repetido") {
        check_config_error(send_stream(messages, with({{"read", "d", "{}"}, {"read", "e", "{}"}})),
                           "«read» está repetida");
    }
    SECTION("parameters que no es JSON") {
        check_config_error(send_stream(messages, with({{"read", "d", "{nope"}})),
                           "«read» no son JSON válido");
    }
    SECTION("parameters que no es objeto") {
        check_config_error(send_stream(messages, with({{"read", "d", "[1]"}})),
                           "«read» no son un objeto JSON");
    }
}

TEST_CASE("Historial con herramientas válido y chat normal pasan la validación", "[peticion][herramientas]") {
    CHECK_FALSE(chatbot::validate_tool_history(golden_messages()).has_value());
    CHECK_FALSE(chatbot::validate_tool_history(
                    {chatbot::Message{chatbot::Role::User, "Lee"},
                     assistant_calls("", {chatbot::ToolCall{"a", "read", "{}"},
                                          chatbot::ToolCall{"b", "read", "{}"}}),
                     tool_result("b", "2"), chatbot::Message{chatbot::Role::System, "s"},
                     tool_result("a", "1"), chatbot::Message{chatbot::Role::Assistant, "Ya."}})
                    .has_value());
}

TEST_CASE("Validación del historial: un error por regla, con su id", "[peticion][herramientas]") {
    const chatbot::Message user{chatbot::Role::User, "Lee"};
    const chatbot::ToolCall call_a{"call_a", "read", "{}"};

    SECTION("tool_calls fuera de un asistente") {
        chatbot::Message bad = user;
        bad.tool_calls = {call_a};
        CHECK_THAT(history_error({bad}), ContainsSubstring("Solo un mensaje del asistente"));
    }
    SECTION("tool sin tool_call_id") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a}), tool_result("", "x")}),
                   ContainsSubstring("sin el id de su llamada"));
    }
    SECTION("tool sin llamada") {
        CHECK_THAT(history_error({user, tool_result("call_z", "x")}),
                   ContainsSubstring("«call_z» no corresponde a ninguna llamada"));
    }
    SECTION("tool repetido") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a}), tool_result("call_a", "1"),
                                  tool_result("call_a", "2")}),
                   ContainsSubstring("«call_a» responde una llamada que ya tenía resultado"));
    }
    SECTION("tool con el id de una llamada de un grupo anterior") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a}), tool_result("call_a", "1"),
                                  chatbot::Message{chatbot::Role::Assistant, "Ya."},
                                  chatbot::Message{chatbot::Role::User, "Otra"},
                                  tool_result("call_a", "2")}),
                   ContainsSubstring("«call_a» responde una llamada que ya tenía resultado"));
    }
    SECTION("id de llamada repetido en el historial") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a}), tool_result("call_a", "1"),
                                  assistant_calls("", {call_a}), tool_result("call_a", "2")}),
                   ContainsSubstring("«call_a» está repetido"));
    }
    SECTION("llamada sin id") {
        CHECK_THAT(history_error({user, assistant_calls("", {chatbot::ToolCall{"", "read", "{}"}})}),
                   ContainsSubstring("sin id"));
    }
    SECTION("llamada sin resultado antes del siguiente mensaje") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a}),
                                  chatbot::Message{chatbot::Role::User, "¿Y?"}}),
                   ContainsSubstring("«call_a» no tiene su resultado antes del siguiente mensaje"));
    }
    SECTION("llamada sin resultado al final") {
        CHECK_THAT(history_error({user, assistant_calls("", {call_a})}),
                   ContainsSubstring("«call_a» no tiene su resultado al final"));
    }
    SECTION("la validación aplica sin herramientas y no manda nada") {
        check_config_error(send_stream({user, assistant_calls("", {call_a})}), "«call_a»");
    }
}

TEST_CASE("complete_trimmed pasa las opciones a la petición", "[peticion][herramientas]") {
    auto harness = make_client();
    harness.transport->responses.push_back(stream_done());
    const auto ignore = [](const chatbot::StreamDelta&) { return true; };
    const std::vector<chatbot::Message> messages{chatbot::Message{chatbot::Role::User, "Hola"}};

    REQUIRE(chatbot::complete_trimmed(*harness.client, messages, 0, ignore).result.is_ok());
    REQUIRE(chatbot::complete_trimmed(*harness.client, messages, 0, ignore, nullptr, read_tool())
                .result.is_ok());
    REQUIRE(harness.transport->requests.size() == 2);
    CHECK_THAT(harness.transport->requests[0].body, !ContainsSubstring("\"tools\""));
    CHECK_THAT(harness.transport->requests[0].body, !ContainsSubstring("tool_choice"));
    CHECK_THAT(harness.transport->requests[1].body, ContainsSubstring(kReadToolJson));
}

TEST_CASE("UTF-8 inválido en un mensaje: no lanza y se sustituye por U+FFFD", "[peticion][utf8]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, chatbot_test::success_body(), std::nullopt, false, ""});
    const std::vector<chatbot::Message> messages{
        chatbot::Message{chatbot::Role::User, std::string("caf\xE9")}};

    std::optional<chatbot::Result<std::string>> response;
    REQUIRE_NOTHROW(response.emplace(harness.client->complete(messages)));
    REQUIRE(response->is_ok());
    REQUIRE(harness.transport->requests.size() == 1);

    // El cuerpo enviado es JSON válido y el byte inválido quedó como U+FFFD.
    const nlohmann::json body = nlohmann::json::parse(harness.transport->requests.front().body);
    CHECK(body.at("messages")[0].at("content") == "caf\xEF\xBF\xBD");
}
