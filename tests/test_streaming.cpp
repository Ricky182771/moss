#include "chatbot/cancel_token.h"
#include "chatbot/chat_client.h"
#include "fake_transport.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using chatbot_test::make_client;
using chatbot_test::sample_messages;
using chatbot_test::success_body;
} // namespace

/// Flujo SSE con delta "Hola" y cierre sin content (done implícito).
std::string sse_flow(const std::string& text = "Hola") {
    std::string flow;
    flow += "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"" + text +
            "\"}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n";
    flow += "data: [DONE]\n\n";
    return flow;
}

TEST_CASE("complete_stream: pide stream:true al servidor", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    const chatbot::Result<chatbot::CompletionInfo> response =
        harness.client->complete_stream(sample_messages(), [](const chatbot::StreamDelta&) {
            return true;
        });

    REQUIRE(response.is_ok());
    const nlohmann::json body = nlohmann::json::parse(harness.transport->requests.front().body);
    CHECK(body.at("stream") == true);
}

TEST_CASE("complete_stream: entrega los deltas en orden con trozos partidos", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow("uno-dos-tres"), std::nullopt, false, ""});
    harness.transport->chunk_size = 4;

    std::vector<std::string> deltas;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            deltas.emplace_back(delta.text);
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK_FALSE(deltas.empty());
    std::string joined;
    for (const std::string& delta : deltas) {
        joined += delta;
    }
    CHECK(joined == "uno-dos-tres");
}

TEST_CASE("complete_stream: flujo sin [DONE] al final se tolera", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});
    // Corta el [DONE] final.
    chatbot::HttpResponse& response = harness.transport->responses.back();
    response.body.erase(response.body.size() - std::string{"data: [DONE]\n\n"}.size());

    std::string joined;
    const chatbot::Result<chatbot::CompletionInfo> result = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            joined += delta.text;
            return true;
        });

    REQUIRE(result.is_ok());
    CHECK(joined == "Hola");
}

TEST_CASE("complete_stream: reintenta si falla antes del primer delta", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{429, "{}", std::nullopt, false, ""});
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::vector<std::string> deltas;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            deltas.emplace_back(delta.text);
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK(harness.transport->requests.size() == 2);
    REQUIRE(harness.sleeper->sleeps.size() == 1);
    CHECK(harness.sleeper->sleeps[0] == std::chrono::milliseconds{1000});
    CHECK_FALSE(deltas.empty());
}

TEST_CASE("complete_stream: no reintenta si ya se entregó contenido", "[streaming]") {
    auto harness = make_client();
    // Primer intento: delta válido y después un evento con JSON roto a mitad
    // del flujo (antes del cierre).
    chatbot::HttpResponse broken;
    broken.status = 200;
    broken.body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"parcial\"}}]}\n\n"
        "data: {\"no es sse\"\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    harness.transport->responses.push_back(broken);

    std::vector<std::string> deltas;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            deltas.emplace_back(delta.text);
            return true;
        });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
    CHECK_FALSE(deltas.empty()); // El texto parcial ya se entregó.
}

TEST_CASE("complete_stream: cancelación desde el callback devuelve Cancelled", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow("parcial"), std::nullopt, false, ""});

    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) {
            return false; // Cancela al primer delta.
        });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Cancelled);
    CHECK(harness.transport->requests.size() == 1);
}

TEST_CASE("complete_stream: reintenta Server (500) antes del primer delta", "[streaming]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{500, "{}", std::nullopt, false, ""});
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::vector<std::string> deltas;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            deltas.emplace_back(delta.text);
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK(harness.transport->requests.size() == 2);
    CHECK_FALSE(deltas.empty());
}

TEST_CASE("complete_stream: error Config no llama al transporte", "[streaming]") {
    chatbot::Config config = chatbot_test::test_config();
    config.model.clear();
    auto harness = make_client(config);

    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) {
            return true;
        });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Config);
    CHECK(harness.transport->requests.empty());
}

TEST_CASE("complete_stream: BadResponse de un chunk no se reintenta", "[streaming]") {
    auto harness = make_client();
    chatbot::HttpResponse bad_chunk;
    bad_chunk.status = 200;
    bad_chunk.body = "data: {oops\n\n";
    harness.transport->responses.push_back(bad_chunk);

    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) {
            return true;
        });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
}

TEST_CASE("complete_stream: UTF-8 inválido no lanza y se sustituye por U+FFFD",
          "[streaming][utf8]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});
    const std::vector<chatbot::Message> messages{
        chatbot::Message{chatbot::Role::User, std::string("caf\xE9")}};

    std::optional<chatbot::Result<chatbot::CompletionInfo>> response;
    REQUIRE_NOTHROW(response.emplace(harness.client->complete_stream(
        messages, [](const chatbot::StreamDelta&) { return true; })));
    REQUIRE(response->is_ok());
    REQUIRE(harness.transport->requests.size() == 1);

    const nlohmann::json body = nlohmann::json::parse(harness.transport->requests.front().body);
    CHECK(body.at("messages")[0].at("content") == "caf\xEF\xBF\xBD");
}

TEST_CASE("complete_stream: callback que lanza devuelve Cancelled sin reintentar",
          "[streaming][excepciones]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow("parcial"), std::nullopt, false, ""});

    std::optional<chatbot::Result<chatbot::CompletionInfo>> response;
    REQUIRE_NOTHROW(response.emplace(harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) -> bool {
            throw std::runtime_error("fallo en la interfaz");
        })));

    REQUIRE(response->is_error());
    CHECK(response->error().kind == chatbot::ErrorKind::Cancelled);
    CHECK_THAT(response->error().message,
               Catch::Matchers::ContainsSubstring("fallo en la interfaz"));
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
}

TEST_CASE("complete_stream: primer chunk con content null se ignora", "[streaming][null]") {
    auto harness = make_client();
    std::string flow;
    flow += "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{\"content\":\"Hola \"}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{\"content\":\"mundo\"}}]}\n\n";
    flow += "data: [DONE]\n\n";
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, flow, std::nullopt, false, ""});

    std::string joined;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            joined += delta.text;
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK(joined == "Hola mundo");
}

TEST_CASE("complete_stream: chunk final con choices vacío y usage se ignora",
          "[streaming][null]") {
    auto harness = make_client();
    std::string flow;
    flow += "data: {\"choices\":[{\"delta\":{\"content\":\"Hola \"}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{\"content\":\"mundo\"}}]}\n\n";
    flow += "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":2}}\n\n";
    flow += "data: [DONE]\n\n";
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, flow, std::nullopt, false, ""});

    std::string joined;
    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            joined += delta.text;
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK(joined == "Hola mundo");
}

TEST_CASE("complete_stream: error dentro del flujo con estado 200 conserva el mensaje",
          "[streaming][null]") {
    auto harness = make_client();
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200, "data: {\"error\":{\"message\":\"Cuota agotada\"}}\n\n", std::nullopt, false,
        ""});

    const chatbot::Result<chatbot::CompletionInfo> response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(response.error().message == "Cuota agotada");
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
}

namespace {
/// Ejecuta complete_stream con un cuerpo fijo y junta los deltas.
std::pair<chatbot::Result<chatbot::CompletionInfo>, std::string> stream_body(const std::string& body,
                                                          std::size_t chunk_size = 0) {
    auto harness = make_client();
    harness.transport->chunk_size = chunk_size;
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, body, std::nullopt, false, ""});
    std::string joined;
    chatbot::Result<chatbot::CompletionInfo> result = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            joined += delta.text;
            return true;
        });
    return {std::move(result), joined};
}

const std::string kHolaChunk = "data: {\"choices\":[{\"delta\":{\"content\":\"Hola\"}}]}";
} // namespace

TEST_CASE("Fin de flujo: [DONE] sin línea en blanco final es éxito", "[streaming][fin]") {
    for (const std::size_t size : {std::size_t{0}, std::size_t{3}}) {
        const auto [result, joined] = stream_body(kHolaChunk + "\n\ndata: [DONE]\n", size);
        CHECK(result.is_ok());
        CHECK(joined == "Hola");
    }
}

TEST_CASE("Fin de flujo: chunk válido final sin línea en blanco entrega su texto",
          "[streaming][fin]") {
    const std::string body = kHolaChunk + "\n\n" +
                             "data: {\"choices\":[{\"delta\":{\"content\":\" mundo\"}}]}";
    const auto [result, joined] = stream_body(body);
    CHECK(result.is_ok());
    CHECK(joined == "Hola mundo");
}

TEST_CASE("Fin de flujo: JSON cortado es BadResponse", "[streaming][fin]") {
    const auto [result, joined] =
        stream_body(kHolaChunk + "\n\ndata: {\"choices\":[{\"del");
    REQUIRE(result.is_error());
    CHECK(result.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(joined == "Hola");
}

TEST_CASE("Fin de flujo: basura sin campo data es BadResponse", "[streaming][fin]") {
    const auto [result, joined] = stream_body(kHolaChunk + "\n\nbasura");
    REQUIRE(result.is_error());
    CHECK(result.error().kind == chatbot::ErrorKind::BadResponse);
}

TEST_CASE("Fin de flujo: comentario final sin línea en blanco se ignora", "[streaming][fin]") {
    const auto [result, joined] = stream_body(kHolaChunk + "\n\n: ping");
    CHECK(result.is_ok());
    CHECK(joined == "Hola");
}

namespace {
chatbot::HttpResponse stream_error_response(const std::string& error_json) {
    return chatbot::HttpResponse{200, "data: " + error_json + "\n\n", std::nullopt, false, ""};
}
} // namespace

TEST_CASE("Error de flujo con código 500 se reintenta como Server", "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(stream_error_response(
        R"j({"error":{"message":"ResourceExhausted: Worker local total request limit reached (917/16)","type":"internal_server_error","code":500}})j"));
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::string joined;
    const auto response = harness.client->complete_stream(sample_messages(), [&](const chatbot::StreamDelta& d) {
        joined += d.text;
        return true;
    });
    REQUIRE(response.is_ok());
    CHECK(joined == "Hola");
    CHECK(harness.transport->requests.size() == 2);
    REQUIRE(harness.sleeper->sleeps.size() == 1);
    CHECK(harness.sleeper->sleeps[0] == std::chrono::milliseconds{1000});
}

TEST_CASE("Error de flujo con código 429 es RateLimited", "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        stream_error_response(R"j({"error":{"message":"demasiadas","code":429}})j"));

    const auto response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::RateLimited);
    CHECK(harness.transport->requests.size() == 4); // 1 + 3 reintentos.
}

TEST_CASE("Error de flujo overloaded_error sin código se reintenta como Server",
          "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(stream_error_response(
        R"j({"error":{"message":"Service temporarily overloaded","type":"overloaded_error"}})j"));
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    const auto response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });
    REQUIRE(response.is_ok());
    CHECK(harness.transport->requests.size() == 2);
}

TEST_CASE("Error de flujo sin código ni tipo reconocible no se reintenta",
          "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        stream_error_response(R"j({"error":{"message":"algo raro","type":"invalid_thing"}})j"));

    const auto response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(response.error().message == "algo raro");
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
}

TEST_CASE("Error de servidor después de un delta no se reintenta", "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200,
        "data: {\"choices\":[{\"delta\":{\"content\":\"parcial\"}}]}\n\n"
        "data: {\"error\":{\"message\":\"overloaded\",\"code\":500}}\n\n",
        std::nullopt, false, ""});
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::string joined;
    const auto response = harness.client->complete_stream(sample_messages(), [&](const chatbot::StreamDelta& d) {
        joined += d.text;
        return true;
    });
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Server);
    CHECK(joined == "parcial");
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
}

namespace {
/// complete_stream con un cuerpo fijo; devuelve el finish_reason (debe ser éxito).
std::string finish_reason_of(const std::string& body) {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, body, std::nullopt, false, ""});
    const chatbot::Result<chatbot::CompletionInfo> result = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });
    REQUIRE(result.is_ok());
    return result.value().finish_reason;
}

std::string chunk(const std::string& content, const std::string& finish_reason_json) {
    return "data: {\"choices\":[{\"delta\":{\"content\":\"" + content +
           "\"},\"finish_reason\":" + finish_reason_json + "}]}\n\n";
}
} // namespace

TEST_CASE("finish_reason: stop", "[streaming][finish]") {
    CHECK(finish_reason_of(chunk("Hola", "null") + chunk("", "\"stop\"") + "data: [DONE]\n\n") ==
          "stop");
}

TEST_CASE("finish_reason: length", "[streaming][finish]") {
    CHECK(finish_reason_of(chunk("Hola", "\"length\"") + "data: [DONE]\n\n") == "length");
}

TEST_CASE("finish_reason: ausente queda vacío", "[streaming][finish]") {
    CHECK(finish_reason_of(
              "data: {\"choices\":[{\"delta\":{\"content\":\"Hola\"}}]}\n\ndata: [DONE]\n\n")
              .empty());
}

TEST_CASE("finish_reason: varios null y uno final con valor", "[streaming][finish]") {
    CHECK(finish_reason_of(chunk("Ho", "null") + chunk("la", "null") + chunk("", "\"length\"") +
                           "data: {\"choices\":[],\"usage\":{}}\n\ndata: [DONE]\n\n") ==
          "length");
}

TEST_CASE("Error de flujo solo con 'Service temporarily overloaded' se reintenta",
          "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        stream_error_response(R"j({"error":{"message":"Service temporarily overloaded"}})j"));
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::string joined;
    const auto response = harness.client->complete_stream(sample_messages(), [&](const chatbot::StreamDelta& d) {
        joined += d.text;
        return true;
    });
    REQUIRE(response.is_ok());
    CHECK(joined == "Hola");
    CHECK(harness.transport->requests.size() == 2);
}

TEST_CASE("Error de flujo con un mensaje cualquiera sigue siendo BadResponse",
          "[streaming][error-flujo]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        stream_error_response(R"j({"error":{"message":"algo distinto"}})j"));

    const auto response = harness.client->complete_stream(
        sample_messages(), [](const chatbot::StreamDelta&) { return true; });
    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::BadResponse);
    CHECK(harness.transport->requests.size() == 1);
}

// Pensamiento (M3): delta.reasoning_content / delta.reasoning llegan aparte
// del texto, con su tipo, en el orden del flujo.

namespace {

using Kind = chatbot::StreamDelta::Kind;

/// Un delta recibido, con su tipo.
struct Seen {
    Kind kind;
    std::string text;
    bool operator==(const Seen&) const = default;
};

std::string reasoning_chunk(const std::string& field, const std::string& text) {
    return "data: {\"choices\":[{\"delta\":{\"" + field + "\":\"" + text + "\"}}]}\n\n";
}

} // namespace

TEST_CASE("complete_stream: entrega pensamiento y texto con su tipo y en orden",
          "[streaming][pensamiento]") {
    const std::string field = GENERATE(std::string{"reasoning_content"}, std::string{"reasoning"});
    auto harness = make_client();
    std::string flow = "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\n\n";
    flow += reasoning_chunk(field, "uno ");
    flow += reasoning_chunk(field, "dos");
    // Los dos en un chunk: primero el pensamiento.
    flow += "data: {\"choices\":[{\"delta\":{\"" + field + "\":\" tres\",\"content\":\"Hola\"}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{\"content\":\" mundo\"}}]}\n\n";
    flow += "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
    harness.transport->responses.push_back(chatbot::HttpResponse{200, flow, std::nullopt, false, ""});
    harness.transport->chunk_size = 7;

    std::vector<Seen> seen;
    const auto response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            seen.push_back(Seen{delta.kind, std::string{delta.text}});
            return true;
        });

    REQUIRE(response.is_ok());
    CHECK(response.value().finish_reason == "stop");
    CHECK(seen == std::vector<Seen>{{Kind::Reasoning, "uno "},
                                    {Kind::Reasoning, "dos"},
                                    {Kind::Reasoning, " tres"},
                                    {Kind::Content, "Hola"},
                                    {Kind::Content, " mundo"}});
}

TEST_CASE("complete_stream: sin pensamiento todos los deltas son de texto",
          "[streaming][pensamiento]") {
    auto harness = make_client();
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow("Hola"), std::nullopt, false, ""});
    std::vector<Seen> seen;
    const auto response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            seen.push_back(Seen{delta.kind, std::string{delta.text}});
            return true;
        });
    REQUIRE(response.is_ok());
    CHECK(seen == std::vector<Seen>{{Kind::Content, "Hola"}});
}

TEST_CASE("complete_stream: no reintenta después de un delta de pensamiento",
          "[streaming][pensamiento]") {
    auto harness = make_client();
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200,
        reasoning_chunk("reasoning_content", "pienso") +
            "data: {\"error\":{\"code\":503,\"message\":\"saturado\"}}\n\n",
        std::nullopt, false, ""});
    harness.transport->responses.push_back(
        chatbot::HttpResponse{200, sse_flow(), std::nullopt, false, ""});

    std::vector<Seen> seen;
    const auto response = harness.client->complete_stream(
        sample_messages(), [&](const chatbot::StreamDelta& delta) {
            seen.push_back(Seen{delta.kind, std::string{delta.text}});
            return true;
        });

    REQUIRE(response.is_error());
    CHECK(response.error().kind == chatbot::ErrorKind::Server);
    CHECK(harness.transport->requests.size() == 1);
    CHECK(harness.sleeper->sleeps.empty());
    CHECK(seen == std::vector<Seen>{{Kind::Reasoning, "pienso"}});
}

TEST_CASE("complete_stream: cancelar durante el pensamiento", "[streaming][pensamiento]") {
    auto harness = make_client();
    harness.transport->responses.push_back(chatbot::HttpResponse{
        200, reasoning_chunk("reasoning_content", "pienso") + sse_flow("texto"), std::nullopt,
        false, ""});
    std::vector<Seen> seen;

    SECTION("el callback devuelve false") {
        const auto response = harness.client->complete_stream(
            sample_messages(), [&](const chatbot::StreamDelta& delta) {
                seen.push_back(Seen{delta.kind, std::string{delta.text}});
                return false;
            });
        REQUIRE(response.is_error());
        CHECK(response.error().kind == chatbot::ErrorKind::Cancelled);
    }
    SECTION("con el token") {
        chatbot::CancelToken token;
        const auto response = harness.client->complete_stream(
            sample_messages(),
            [&](const chatbot::StreamDelta& delta) {
                seen.push_back(Seen{delta.kind, std::string{delta.text}});
                token.cancel();
                return true;
            },
            &token);
        REQUIRE(response.is_error());
        CHECK(response.error().kind == chatbot::ErrorKind::Cancelled);
    }
    CHECK(harness.transport->requests.size() == 1);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.front() == Seen{Kind::Reasoning, "pienso"});
}
