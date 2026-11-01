// Pruebas de Session: la lógica de la app sin FTXUI, sin terminal ni red.
// El post mete las tareas en una cola que la prueba vacía, como en
// test_request_runner.cpp.

#include "session.h"

#include "command_parser.h"
#include "downloads.h"
#include "chatbot/providers.h"
#include "search_settings.h"

#include "blocking_transport.hpp"
#include "fake_search_provider.hpp"
#include "fake_transport.hpp"
#include "performance.hpp"
#include "session_fakes.hpp"
#include "system_prompt_invariants.hpp"
#include "temp_dir.hpp"

#include "chatbot/config.h"
#include "chatbot/credentials.h"
#include "chatbot/platform.h"
#include "chatbot/search_context.h"
#include "chatbot/system_prompt.h"
#include "chatbot/tavily_search.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using chatbot::cli::Session;
using chatbot::cli::SubmitOutcome;
using chatbot_test::BlockingTransport;
using chatbot_test::kThreadTimeout;
using Type = SubmitOutcome::Type;

using chatbot_test::kSearchKey;
using chatbot_test::kSessionNow;
using chatbot_test::stream;
using chatbot_test::stream_with_finish;
constexpr std::time_t kNow = kSessionNow;

/// La app con un directorio temporal (session_fakes.hpp): config.json,
/// credentials.json, las conversaciones y las descargas.
struct Harness : chatbot_test::SessionFakes {
    chatbot_test::TaskQueue queue;
    std::vector<Session::Finished> finished; ///< Cada on_finished.
    std::unique_ptr<Session> session; ///< Al último: se destruye primero (join).

    void start(chatbot::Config config = chatbot_test::test_config()) {
        session = std::make_unique<Session>(
            environment(), std::move(config),
            [this](chatbot::cli::RequestRunner::Task task) { queue.post(std::move(task)); },
            [this](Session::Finished done) { finished.push_back(std::move(done)); });
    }

    /// Vacía la cola hasta el siguiente on_finished.
    bool finish() {
        const std::size_t before = finished.size();
        return queue.run_until([&] { return finished.size() > before; });
    }

    /// Envía text y espera la respuesta; true si se guardó el par.
    bool exchange(const std::string& text) {
        REQUIRE(session->submit(text).type == Type::Started);
        REQUIRE(finish());
        return finished.back().saved;
    }

    [[nodiscard]] const chatbot::cli::Entry& last_entry() const {
        REQUIRE_FALSE(session->conversation().entries().empty());
        return session->conversation().entries().back();
    }
};

void check_status(const SubmitOutcome& got, const std::string& text) {
    CHECK(got.type == Type::Status);
    CHECK(got.status == text);
    CHECK_FALSE(got.clear);
}

} // namespace

TEST_CASE("Session sin configuración no envía, pero los comandos locales responden",
          "[session]") {
    Harness h;
    chatbot::Config config = chatbot_test::test_config();
    config.model.clear();
    h.start(config);
    CHECK_FALSE(h.session->configured());
    CHECK(h.session->model().empty());
    CHECK(h.clients == 0);
    check_status(h.session->submit("hola"), std::string{chatbot::cli::kSetupNotice});
    check_status(h.session->submit("/buscar clima"), std::string{chatbot::cli::kSetupNotice});
    check_status(h.session->submit("/copiar"),
                 chatbot::cli::choose_code_block({}, std::nullopt).problem);
    check_status(h.session->submit("/exportar"), "Todavía no hay respuestas que exportar.");
    CHECK(h.session->conversation().entries().empty());
}

TEST_CASE("Session envía, guarda el par y avisa al terminar", "[session]") {
    Harness h;
    h.start();
    REQUIRE(h.session->configured());
    CHECK(h.session->model() == "modelo-de-prueba");
    const SubmitOutcome empty = h.session->submit("   ");
    CHECK(empty.type == Type::Ignored);
    CHECK(empty.sent == chatbot::cli::SendResult::Empty);
    CHECK(empty.command == chatbot::cli::ParsedCommand::Type::Normal);
    const SubmitOutcome sent = h.session->submit("  hola  ");
    CHECK(sent.type == Type::Started);
    CHECK(sent.clear);
    CHECK(sent.sent == chatbot::cli::SendResult::Sent);
    CHECK(h.session->busy());
    REQUIRE(h.finish());
    REQUIRE(h.finished.size() == 1);
    CHECK(h.finished[0].restored == std::nullopt);
    CHECK(h.finished[0].error == std::nullopt);
    CHECK(h.finished[0].saved);
    CHECK(h.finished[0].dropped == 0);
    CHECK_FALSE(h.session->busy());
    const std::string& id = h.session->conversation().id();
    REQUIRE_FALSE(id.empty());
    CHECK(std::filesystem::exists(h.dir.path() / "conversations" / (id + ".json")));
    CHECK(h.last_entry().text == "Hola, soy el asistente.");
    CHECK(chatbot_test::check_invariants(chatbot_test::sent_messages_of(h.chat->requests.back()),
                                         h.session->system_prompt(), chatbot_test::Ending::Sent,
                                         "hola") == "");
}

TEST_CASE("Session regresa el texto tras un error o una cancelación, sin guardar", "[session]") {
    Harness h;
    SECTION("429") {
        chatbot::HttpResponse limited;
        limited.status = 429;
        limited.body = R"({"error":{"message":"demasiadas"}})";
        h.responses = {limited};
        h.start();
        CHECK_FALSE(h.exchange("hola"));
        CHECK(h.finished.back().restored == "hola");
        REQUIRE(h.finished.back().error.has_value());
        CHECK(h.finished.back().error->kind == chatbot::ErrorKind::RateLimited);
        CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Error);
    }
    SECTION("cancelación a la mitad") {
        BlockingTransport* blocking = nullptr;
        h.block(blocking);
        h.start();
        REQUIRE(h.session->submit("hola").type == Type::Started);
        REQUIRE(blocking->started.wait_for(kThreadTimeout));
        h.session->cancel();
        REQUIRE(h.finish());
        CHECK(h.finished.back().restored == "hola");
        REQUIRE(h.finished.back().error.has_value());
        CHECK(h.finished.back().error->kind == chatbot::ErrorKind::Cancelled);
        CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Assistant);
        CHECK(h.last_entry().cancelled);
    }
    CHECK(h.session->conversation().id().empty());
    CHECK(h.session->store().list().empty());
    CHECK_FALSE(h.session->busy());
}

TEST_CASE("Session: /buscar", "[session]") {
    Harness h;
    SECTION("sin consulta") {
        h.start();
        check_status(h.session->submit("/buscar"), std::string{chatbot::cli::kSearchUsage});
    }
    SECTION("sin key") {
        h.start();
        CHECK_FALSE(h.session->command_context().has_search_key);
        check_status(h.session->submit("/buscar clima"),
                     std::string{chatbot::cli::kMissingSearchKey});
    }
    SECTION("con resultados: buscando hasta que llega la búsqueda, y se guarda con el par") {
        h.env.values["CHAT_SEARCH_API_KEY"] = kSearchKey;
        h.start();
        REQUIRE(h.session->submit("/buscar clima").type == Type::Started);
        CHECK(h.session->searching());
        REQUIRE(h.queue.run_until([&] { return !h.session->searching(); }));
        CHECK(h.session->busy()); // Se apagó al llegar la búsqueda, no al terminar.
        REQUIRE(h.finish());
        REQUIRE(h.finished.back().restored == std::nullopt);
        CHECK_FALSE(h.session->searching());
        const chatbot::cli::LoadResult loaded =
            h.session->store().load(h.session->conversation().id());
        REQUIRE(loaded.conversation.has_value());
        const auto& search = loaded.conversation->messages.front().search;
        REQUIRE(search.has_value());
        CHECK(search->date == chatbot::local_iso_date(kNow));
        CHECK(search->response.results.size() == 1);
    }
    SECTION("sin resultados: regresa el texto sin llamar al modelo") {
        h.env.values["CHAT_SEARCH_API_KEY"] = kSearchKey;
        h.search_responses = {chatbot_test::tavily_response(nlohmann::json::array())};
        h.start();
        CHECK_FALSE(h.exchange("/buscar nada"));
        CHECK(h.finished.back().restored == "/buscar nada");
        REQUIRE(h.finished.back().error.has_value());
        CHECK(h.finished.back().error->kind == chatbot::ErrorKind::NoSearchResults);
        CHECK(h.chat->requests.empty());
        CHECK_FALSE(h.session->searching());
    }
}

TEST_CASE("Session con una respuesta en curso", "[session]") {
    Harness h;
    BlockingTransport* blocking = nullptr;
    h.block(blocking);
    h.start();
    REQUIRE(h.session->submit("hola").type == Type::Started);
    REQUIRE(blocking->started.wait_for(kThreadTimeout));
    const SubmitOutcome busy = h.session->submit("otra");
    CHECK(busy.type == Type::Ignored);
    CHECK(busy.sent == chatbot::cli::SendResult::Busy);
    check_status(h.session->submit("/copiar"), "Espera a que termine la respuesta");
    check_status(h.session->submit("/exportar"), "Espera a que termine la respuesta");
    const std::optional<std::string> wait{std::string{chatbot::cli::kWaitOrCancel}};
    CHECK(h.session->new_conversation() == wait);
    CHECK(h.session->open("20260101-000000-abcdef") == wait);
    CHECK(h.session->remove("20260101-000000-abcdef") == wait);
    CHECK(h.session->command_context().busy);
    h.session->cancel();
    REQUIRE(h.finish());
}

TEST_CASE("Session abre conversaciones guardadas", "[session]") {
    Harness h;
    chatbot::Config first = chatbot_test::test_config();
    first.model = "modelo-a";
    h.start(first);
    REQUIRE(h.exchange("hola"));
    const std::string id = h.session->conversation().id();
    chatbot::Config second = chatbot_test::test_config();
    second.model = "modelo-b";
    h.start(second);
    REQUIRE(h.session->open(id) == std::nullopt);
    CHECK(h.session->conversation().id() == id);
    CHECK(h.last_entry().text == "Esta conversación usó modelo-a; se continúa con modelo-b.");

    // Ilegible: el error de store.load, y la abierta no cambia.
    std::ofstream{h.dir.path() / "conversations" / "20260101-000000-abcdef.json"} << "{no es json";
    const std::optional<std::string> error = h.session->open("20260101-000000-abcdef");
    REQUIRE(error.has_value());
    CHECK(*error == h.session->store().load("20260101-000000-abcdef").error);
    CHECK(h.session->conversation().id() == id);
}

TEST_CASE("Session borra conversaciones", "[session]") {
    Harness h;
    h.start();
    REQUIRE(h.exchange("uno"));
    const std::string other = h.session->conversation().id();
    REQUIRE(h.session->new_conversation() == std::nullopt);
    CHECK(h.session->conversation().entries().empty());
    REQUIRE(h.exchange("dos"));
    const std::string open = h.session->conversation().id();
    REQUIRE(other != open);

    REQUIRE(h.session->remove(other) == std::nullopt);
    CHECK(h.session->conversation().id() == open);
    CHECK(h.session->conversation().has_turns());
    CHECK(h.session->store().list().size() == 1);

    REQUIRE(h.session->remove(open) == std::nullopt);
    CHECK(h.session->conversation().id().empty());
    CHECK(h.session->conversation().entries().empty());
    CHECK(h.session->store().list().empty());
}

TEST_CASE("Session: /copiar, /guardar y /exportar", "[session]") {
    Harness h;
    h.responses = {stream("Mira:\\n\\n```cpp\\nint x;\\n```")};
    h.start();
    check_status(h.session->submit("/exportar"), "Todavía no hay respuestas que exportar.");
    REQUIRE(h.exchange("un bloque"));
    const std::vector<chatbot::cli::CodeBlock>& blocks = h.session->code_blocks().blocks();
    REQUIRE(blocks.size() == 1);
    CHECK(h.session->command_context().has_code_blocks);

    check_status(h.session->submit("/copiar tres"), std::string{chatbot::cli::kCopyUsage});
    check_status(h.session->submit("/guardar suma.cpp"), std::string{chatbot::cli::kSaveUsage});
    check_status(h.session->submit("/exportar todo"), std::string{chatbot::cli::kExportUsage});
    check_status(h.session->submit("/copiar 2"), chatbot::cli::choose_code_block(blocks, 2).problem);
    check_status(h.session->submit("/guardar 1 ../x"),
                 *chatbot::cli::validate_file_name("../x"));
    const std::string no_dir{chatbot::cli::no_download_dir_message(chatbot::current_os())};
    check_status(h.session->submit("/guardar 1"), no_dir);
    check_status(h.session->submit("/exportar"), no_dir);

    const SubmitOutcome copy = h.session->submit("/copiar");
    CHECK(copy.type == Type::Copy);
    CHECK(copy.clear);
    CHECK(copy.block.number == 1);
    CHECK(copy.block.code == blocks.front().code);
    CHECK(copy.command == chatbot::cli::ParsedCommand::Type::Copy);
    CHECK_FALSE(copy.sent.has_value());
    // Lo que hace moss-rpc: el aviso de la GUI al historial.
    h.session->report({chatbot::cli::copied_message(copy.block), false});
    CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Notice);
    CHECK(h.last_entry().text == "Copiado el bloque #1 (cpp, 1 línea).");
    h.session->report({"no se pudo", true});
    CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Error);

    h.download_base = h.path("descargas");
    std::filesystem::create_directory(*h.download_base);
    const SubmitOutcome saved = h.session->submit("/guardar 1 suma.cpp");
    CHECK(saved.type == Type::Done);
    CHECK(saved.clear);
    CHECK(saved.command == chatbot::cli::ParsedCommand::Type::Save);
    CHECK(saved.action.path ==
          (h.dir.path() / "descargas" / "moss" / "suma.cpp").string());
    CHECK(h.last_entry().text == saved.action.message);
    CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Notice);
    CHECK(std::filesystem::exists(h.dir.path() / "descargas" / "moss" / "suma.cpp"));

    // Error de escritura: la carpeta queda debajo de un archivo normal (igual
    // en POSIX y en Windows). El error va al historial y la caja no se vacía.
    std::ofstream{h.dir.path() / "archivo"} << "x";
    h.download_base = (h.dir.path() / "archivo" / "sub").string();
    const SubmitOutcome failed = h.session->submit("/guardar 1 otro.cpp");
    CHECK(failed.type == Type::Done);
    CHECK_FALSE(failed.clear);
    CHECK(failed.action.error);
    CHECK_FALSE(failed.action.path.has_value());
    CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Error);
    h.download_base = h.path("descargas");

    const SubmitOutcome exported = h.session->submit("/exportar");
    CHECK(exported.type == Type::Done);
    CHECK(exported.clear);
    CHECK(std::filesystem::exists(h.dir.path() / "descargas" / "moss" /
                                  ("conversacion-" + h.session->conversation().id() + ".md")));
}

TEST_CASE("Session: los botones guardan y exportan sin tocar el historial", "[session]") {
    using Status = chatbot::cli::FileOutcome::Status;
    Harness h;
    h.responses = {stream("Mira:\\n\\n```cpp\\nint x;\\n```")};
    h.start();
    chatbot::cli::FileOutcome got = h.session->export_to_downloads();
    CHECK(got.status == Status::Invalid);
    CHECK(got.result.message == "Todavía no hay respuestas que exportar.");
    REQUIRE(h.exchange("un bloque"));
    const std::size_t entries = h.session->conversation().entries().size();

    got = h.session->save_code_block(2, "");
    CHECK(got.status == Status::Invalid);
    CHECK(got.result.message ==
          chatbot::cli::choose_code_block(h.session->code_blocks().blocks(), 2).problem);
    got = h.session->save_code_block(1, "../x");
    CHECK(got.status == Status::Invalid);
    CHECK(got.result.message == *chatbot::cli::validate_file_name("../x"));
    got = h.session->save_code_block(1, "");
    CHECK(got.status == Status::NoFolder);
    CHECK(got.result.message == chatbot::cli::no_download_dir_message(chatbot::current_os()));
    CHECK(h.session->export_to_downloads().status == Status::NoFolder);

    h.download_base = h.path("descargas");
    std::filesystem::create_directory(*h.download_base);
    got = h.session->save_code_block(1, "");
    CHECK(got.status == Status::Attempted);
    CHECK_FALSE(got.result.error);
    CHECK(got.result.path == (h.dir.path() / "descargas" / "moss" / "bloque-1.cpp").string());
    got = h.session->save_code_block(std::nullopt, "x.cpp"); // Sin número: el último.
    CHECK(got.result.path == (h.dir.path() / "descargas" / "moss" / "x.cpp").string());
    got = h.session->export_to_downloads();
    CHECK(got.status == Status::Attempted);
    CHECK(got.result.message.rfind("Conversación exportada a ", 0) == 0);
    CHECK(got.result.path.has_value());
    CHECK(h.session->conversation().entries().size() == entries);

    // Con una respuesta en curso también; el bloque a medias lleva su aviso.
    BlockingTransport* blocking = nullptr;
    h.chat_transport = [&blocking] {
        auto transport = std::make_unique<BlockingTransport>(BlockingTransport::Mode::Block);
        transport->first_chunk =
            "data: {\"choices\":[{\"delta\":{\"content\":\"```py\\nprint(1)\\n\"}}]}\n\n";
        blocking = transport.get();
        return transport;
    };
    h.start();
    REQUIRE(h.session->open(h.session->store().list().front().id) == std::nullopt);
    REQUIRE(h.session->submit("otro").type == Type::Started);
    REQUIRE(blocking->started.wait_for(kThreadTimeout));
    REQUIRE(h.queue.run_until([&] { return h.session->code_blocks().blocks().size() == 2; }));
    const std::size_t busy_entries = h.session->conversation().entries().size();
    got = h.session->save_code_block(2, "");
    CHECK(got.status == Status::Attempted);
    CHECK(got.result.message.find("Ojo: el bloque está incompleto (la respuesta aún no "
                                  "termina).") != std::string::npos);
    CHECK(h.session->export_to_downloads().status == Status::Attempted);
    CHECK(h.session->conversation().entries().size() == busy_entries);
    h.session->cancel();
    REQUIRE(h.finish());
}

TEST_CASE("Session guarda el proveedor y reconstruye el cliente", "[session]") {
    Harness h;
    chatbot::Credentials credentials;
    credentials.keys["nvidia"] = "sk-clave-secreta-de-prueba-1234";
    REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
    REQUIRE_FALSE(chatbot::save_config_file(h.config_path(),
                                            {"nvidia", "https://integrate.api.nvidia.com/v1", "m1"})
                      .has_value());
    SECTION("otro modelo") {
        h.start(h.load());
        REQUIRE(h.exchange("hola"));
        const std::string id = h.session->conversation().id();
        chatbot::Result<chatbot::cli::ProviderSettings> form = h.session->provider_form();
        REQUIRE(form.is_ok());
        chatbot::cli::ProviderSettings changed = form.value();
        REQUIRE(changed.set_model("m2"));
        REQUIRE(h.session->save_provider(changed) == std::nullopt);
        CHECK(h.clients == 2);
        CHECK(h.session->model() == "m2");
        CHECK(h.session->config().model == "m2");
        CHECK(h.last_entry().text == "Ahora se usa m2 de " + changed.provider_label() + ".");
        CHECK(h.session->conversation().id() == id);
        CHECK(h.session->conversation().has_turns());
    }
    SECTION("con CHAT_MODEL se conserva el modelo guardado") {
        h.env.values["CHAT_MODEL"] = "modelo-del-entorno";
        h.start(h.load());
        chatbot::cli::ProviderSettings form = h.session->provider_form().value();
        CHECK_FALSE(form.set_model("m2"));
        REQUIRE(h.session->save_provider(form) == std::nullopt);
        CHECK(h.session->model() == "modelo-del-entorno");
        CHECK(chatbot::load_config_file_values(h.config_path()).value().model == "m1");
    }
}

TEST_CASE("Session guarda las instrucciones del sistema", "[session]") {
    Harness h;
    chatbot::Config config = chatbot_test::test_config();
    config.history_limit_bytes = 50;
    h.start(config);
    CHECK(h.session->system_prompt() == chatbot::kDefaultSystemPrompt);

    REQUIRE(h.session->save_system_prompt(std::string{}) == std::nullopt);
    CHECK(h.last_entry().text == "Sin instrucciones del sistema desde el siguiente mensaje.");
    REQUIRE(h.exchange("uno"));
    CHECK(chatbot_test::check_invariants(chatbot_test::sent_messages_of(h.chat->requests.back()),
                                         "", chatbot_test::Ending::Sent, "uno") == "");

    const std::string long_prompt(80, 'x');
    REQUIRE(h.session->save_system_prompt(long_prompt) == std::nullopt);
    const std::vector<chatbot::cli::Entry>& entries = h.session->conversation().entries();
    REQUIRE(entries.size() >= 2);
    CHECK(entries[entries.size() - 2].text ==
          "Instrucciones del sistema actualizadas: valen desde el siguiente mensaje.");
    CHECK(entries.back().text == chatbot::system_prompt_limit_warning(long_prompt, 50).value());
    REQUIRE(h.exchange("dos"));
    CHECK(chatbot_test::check_invariants(chatbot_test::sent_messages_of(h.chat->requests.back()),
                                         long_prompt, chatbot_test::Ending::Sent, "dos") == "");

    REQUIRE(h.session->save_system_prompt(std::nullopt) == std::nullopt); // Restaurar.
    CHECK(h.session->system_prompt() == chatbot::kDefaultSystemPrompt);
    // Las predeterminadas también miden más que el límite de 50.
    CHECK(entries[entries.size() - 2].text ==
          "Instrucciones del sistema actualizadas: valen desde el siguiente mensaje.");
    CHECK(entries.back().text ==
          chatbot::system_prompt_limit_warning(chatbot::kDefaultSystemPrompt, 50).value());
    REQUIRE(h.exchange("tres"));
    CHECK(chatbot_test::check_invariants(chatbot_test::sent_messages_of(h.chat->requests.back()),
                                         chatbot::kDefaultSystemPrompt, chatbot_test::Ending::Sent,
                                         "tres") == "");
}

TEST_CASE("Session guarda la key de búsqueda", "[session]") {
    Harness h;
    h.start();
    REQUIRE(h.session->save_search_key(kSearchKey) == std::nullopt);
    CHECK(h.session->command_context().has_search_key);
    CHECK(h.last_entry().text == "Key de búsqueda guardada: usa /buscar <consulta>.");
    CHECK(h.session->submit("/buscar clima").type == Type::Started);
    REQUIRE(h.finish());
}

TEST_CASE("Session no guarda la configuración con una respuesta en curso", "[session]") {
    Harness h;
    BlockingTransport* blocking = nullptr;
    h.block(blocking);
    h.start();
    chatbot::cli::ProviderSettings form = h.session->provider_form().value();
    REQUIRE(form.set_key("sk-clave-secreta-de-prueba-1234"));
    REQUIRE(form.set_model("m2"));
    REQUIRE(h.session->submit("hola").type == Type::Started);
    REQUIRE(blocking->started.wait_for(kThreadTimeout));
    const std::optional<std::string> wait{std::string{chatbot::cli::kWaitOrCancel}};
    CHECK(h.session->save_provider(form) == wait);
    CHECK(h.session->save_system_prompt(std::string{"otras"}) == wait);
    CHECK(h.session->save_system_prompt_text("otras") == wait);
    CHECK(h.session->save_search_key(kSearchKey) == wait);
    CHECK_FALSE(std::filesystem::exists(h.config_path()));
    CHECK_FALSE(std::filesystem::exists(h.credentials_path()));
    // Las preferencias de la GUI no tocan el chat: sí se guardan.
    CHECK(h.session->save_gui_settings(R"({"tema":"oscuro"})") == std::nullopt);
    h.session->cancel();
    REQUIRE(h.finish());
}

TEST_CASE("Session::save_provider valida el formulario antes de escribir", "[session]") {
    Harness h;
    h.start();
    chatbot::cli::ProviderSettings form = h.session->provider_form().value();
    form.select_provider(0); // Un proveedor con key obligatoria, sin key ni modelo.
    const std::optional<std::string> error = form.validate();
    REQUIRE(error.has_value());
    CHECK(h.session->save_provider(form) == error);
    CHECK_FALSE(std::filesystem::exists(h.config_path()));
}

TEST_CASE("Session: si las instrucciones del sistema están en config.json", "[session]") {
    Harness h;
    SECTION("ausentes, vacías o propias") {
        h.start();
        CHECK_FALSE(h.session->system_prompt_stored());
        REQUIRE(h.session->save_system_prompt_text("") == std::nullopt);
        CHECK(h.session->system_prompt_stored());
        CHECK(h.session->system_prompt().empty());
        REQUIRE(h.session->save_system_prompt_text("Eres un tutor.") == std::nullopt);
        CHECK(h.session->system_prompt_stored());
        CHECK(h.session->system_prompt() == "Eres un tutor.");
        // Restaurar: mandar las predeterminadas borra la llave.
        REQUIRE(h.session->save_system_prompt_text(std::string{chatbot::kDefaultSystemPrompt}) ==
                std::nullopt);
        CHECK_FALSE(h.session->system_prompt_stored());
        CHECK(h.session->system_prompt() == chatbot::kDefaultSystemPrompt);
        CHECK(chatbot::load_system_prompt_value(h.config_path()).value() == std::nullopt);
    }
    SECTION("vacías al arrancar") {
        std::ofstream{h.config_path()} << R"({"system_prompt": ""})";
        h.start();
        CHECK(h.session->system_prompt_stored());
        CHECK(h.session->system_prompt().empty());
    }
    SECTION("inválidas: no se guardan") {
        h.start();
        const std::string bad = "a\x01" "b";
        const std::optional<std::string> error = chatbot::validate_system_prompt(bad);
        REQUIRE(error.has_value());
        CHECK(h.session->save_system_prompt_text(bad) == error);
        CHECK_FALSE(std::filesystem::exists(h.config_path()));
    }
}

TEST_CASE("Session: preferencias de la GUI", "[session]") {
    Harness h;
    h.start();
    CHECK(h.session->gui_settings().value() == std::nullopt);
    REQUIRE(h.session->save_gui_settings(R"({ "z": 1, "a": [true] })") == std::nullopt);
    CHECK(h.session->gui_settings().value() == R"({"z":1,"a":[true]})");
    REQUIRE(h.session->save_gui_settings(std::nullopt) == std::nullopt); // Borra la llave.
    CHECK(h.session->gui_settings().value() == std::nullopt);

    const std::string not_object = "Las preferencias de la GUI tienen que ser un objeto JSON.";
    CHECK(h.session->save_gui_settings("5") == not_object);
    CHECK(h.session->save_gui_settings("[1]") == not_object);
    CHECK(h.session->save_gui_settings("{") == not_object);
    // Se mide compacto: los espacios no cuentan.
    const std::string fits = R"({"x":")" + std::string(chatbot::cli::kMaxGuiSettingsBytes - 8, 'a') +
                             R"("}   )";
    CHECK(h.session->save_gui_settings(fits) == std::nullopt);
    const std::string big = R"({"x":")" + std::string(chatbot::cli::kMaxGuiSettingsBytes, 'a') +
                            R"("})";
    CHECK(h.session->save_gui_settings(big) == "Las preferencias de la GUI pasan de 64 KiB.");
    CHECK(h.session->gui_settings().value()->size() == chatbot::cli::kMaxGuiSettingsBytes);
}

TEST_CASE("Session: avisos de arranque", "[session]") {
    Harness h;
    std::ofstream{h.config_path()} << R"({"system_prompt": 5})";
    const std::string prompt_warning =
        chatbot::resolve_system_prompt(chatbot::load_system_prompt_value(h.config_path())).warning;
    REQUIRE_FALSE(prompt_warning.empty());
#ifndef _WIN32
    SECTION("con credentials.json de permisos abiertos") {
        chatbot::Credentials credentials;
        credentials.keys[std::string{chatbot::kSearchCredentialsKey}] = kSearchKey;
        REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
        std::filesystem::permissions(h.credentials_path(), std::filesystem::perms::others_read,
                                     std::filesystem::perm_options::add);
        const chatbot::Result<std::string> key =
            chatbot::load_search_api_key(h.env, h.credentials_path());
        REQUIRE(key.is_error());
        h.start();
        CHECK(h.session->startup_warning() == prompt_warning + " " + key.error().message);
        CHECK(h.session->startup_warnings() ==
              std::vector<std::string>{prompt_warning, key.error().message});
        CHECK_FALSE(h.session->command_context().has_search_key);
    }
#endif
    SECTION("solo las instrucciones") {
        h.start();
        CHECK(h.session->startup_warning() == prompt_warning);
        CHECK(h.session->startup_warnings() == std::vector<std::string>{prompt_warning});
        CHECK(h.session->system_prompt() == chatbot::kDefaultSystemPrompt);
    }
}

TEST_CASE("Session::send no interpreta comandos y dice por qué no envió", "[session]") {
    using chatbot::cli::SendResult;
    Harness h;
    SECTION("sin configuración") {
        chatbot::Config config = chatbot_test::test_config();
        config.model.clear();
        h.start(config);
        CHECK(h.session->send("hola") == SendResult::NotConfigured);
        CHECK(h.session->send("", std::string{"clima"}) == SendResult::NotConfigured);
    }
    SECTION("texto vacío, consulta vacía y sin key") {
        h.start();
        CHECK(h.session->send("   ") == SendResult::Empty);
        CHECK(h.session->send("/buscar clima", std::string{" "}) == SendResult::Empty);
        CHECK(h.session->send("/buscar clima", std::string{"clima"}) ==
              SendResult::MissingSearchKey);
        CHECK(h.session->conversation().entries().empty());
    }
    SECTION("un comando va al modelo tal cual") {
        h.start();
        REQUIRE(h.session->send("/copiar") == SendResult::Sent);
        CHECK(h.session->send("otra") == SendResult::Busy);
        REQUIRE(h.finish());
        CHECK(h.finished.back().saved);
        CHECK(h.session->conversation().entries().front().text == "/copiar");
    }
    SECTION("con búsqueda: text se muestra y se guarda, search va a la búsqueda") {
        h.env.values["CHAT_SEARCH_API_KEY"] = kSearchKey;
        h.start();
        REQUIRE(h.session->send("¿Qué clima hace?", std::string{"clima"}) == SendResult::Sent);
        CHECK(h.session->searching());
        REQUIRE(h.finish());
        REQUIRE(h.finished.back().saved);
        const chatbot::cli::LoadResult loaded =
            h.session->store().load(h.session->conversation().id());
        REQUIRE(loaded.conversation.has_value());
        const auto& user = loaded.conversation->messages.front();
        CHECK(user.content == "¿Qué clima hace?");
        REQUIRE(user.search.has_value());
        CHECK(user.search->response.query == "clima");
        CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Sources);
    }
}

TEST_CASE("Session::Finished: finish_reason, respuesta sin texto y guardado que falla",
          "[session]") {
    Harness h;
    SECTION("finish_reason") {
        h.responses = {stream_with_finish("cortada", "length")};
        h.start();
        REQUIRE(h.exchange("hola"));
        CHECK(h.finished.back().finish_reason == "length");
    }
    SECTION("sin texto: BadResponse con kEmptyAnswer, sin guardar") {
        chatbot::HttpResponse empty;
        empty.status = 200;
        empty.body = "data: [DONE]\n\n";
        h.responses = {empty};
        h.start();
        CHECK_FALSE(h.exchange("hola"));
        CHECK(h.finished.back().restored == "hola");
        REQUIRE(h.finished.back().error.has_value());
        CHECK(h.finished.back().error->kind == chatbot::ErrorKind::BadResponse);
        CHECK(h.finished.back().error->message == chatbot::cli::kEmptyAnswer);
        CHECK(h.finished.back().finish_reason.empty());
    }
    SECTION("guardado que falla: terminó bien, pero saved es false y hay un error") {
        // La carpeta de conversaciones es un archivo: store.save falla.
        std::ofstream{h.path("conversations")} << "x";
        h.start();
        CHECK_FALSE(h.exchange("hola"));
        CHECK(h.finished.back().restored == std::nullopt);
        CHECK(h.finished.back().error == std::nullopt);
        CHECK(h.last_entry().kind == chatbot::cli::EntryKind::Error);
        CHECK(h.last_entry().text.rfind("No se pudo guardar la conversación: ", 0) == 0);
    }
}

TEST_CASE("Session se destruye en menos de 2 s con una petición bloqueada", "[session]") {
    Harness h;
    BlockingTransport* blocking = nullptr;
    h.block(blocking);
    h.start();
    REQUIRE(h.session->submit("hola").type == Type::Started);
    REQUIRE(blocking->started.wait_for(kThreadTimeout));
    const auto begin = std::chrono::steady_clock::now();
    h.session.reset(); // Cancela y hace join.
    chatbot_test::report_latency("~Session", std::chrono::steady_clock::now() - begin);
    CHECK(h.finished.empty()); // Lo que quedó en la cola nunca se ejecuta.
}

namespace {

/// Harness con key de NVIDIA guardada y config.json en nvidia/m1.
void save_nvidia(Harness& h) {
    chatbot::Credentials credentials;
    credentials.keys["nvidia"] = "sk-clave-secreta-de-prueba-1234";
    REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
    REQUIRE_FALSE(chatbot::save_config_file(h.config_path(),
                                            {"nvidia", "https://integrate.api.nvidia.com/v1", "m1"})
                      .has_value());
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Session::switch_model cambia de modelo y de proveedor como F2", "[session][model]") {
    using Status = chatbot::cli::ModelSwitch::Status;
    Harness h;
    save_nvidia(h);
    const std::string credentials_before = read_file(h.credentials_path());

    SECTION("otro modelo del mismo proveedor, sin perder la conversación") {
        h.start(h.load());
        REQUIRE(h.exchange("hola"));
        const std::string id = h.session->conversation().id();
        const std::size_t entries = h.session->conversation().entries().size();
        const chatbot::cli::ModelSwitch got = h.session->switch_model(std::nullopt, "m2");
        CHECK(got.status == Status::Switched);
        CHECK(got.message == "Ahora se usa m2 de NVIDIA NIM.");
        CHECK(h.last_entry().text == got.message);
        CHECK(h.session->model() == "m2");
        CHECK(h.session->conversation().id() == id);
        CHECK(h.session->conversation().entries().size() == entries + 1);
        CHECK(chatbot::load_config_file_values(h.config_path()).value().model == "m2");
        CHECK(read_file(h.credentials_path()) == credentials_before);
    }
    SECTION("a un proveedor local sin key; config.json queda con los dos") {
        h.start(h.load());
        const chatbot::cli::ModelSwitch got = h.session->switch_model("OLLAMA", "qwen3:8b");
        CHECK(got.status == Status::Switched);
        CHECK(got.message == "Ahora se usa qwen3:8b de Ollama (local).");
        const chatbot::ConfigFileValues saved =
            chatbot::load_config_file_values(h.config_path()).value();
        CHECK(saved.provider == "ollama");
        CHECK(saved.base_url == "http://localhost:11434/v1");
        CHECK(saved.model == "qwen3:8b");
        CHECK(h.session->config().base_url == "http://localhost:11434/v1");
        CHECK(read_file(h.credentials_path()) == credentials_before);
        // Y de regreso: la key guardada de NVIDIA sigue sirviendo.
        CHECK(h.session->switch_model("nvidia", "m1").status == Status::Switched);
        CHECK(h.session->config().api_key == "sk-clave-secreta-de-prueba-1234");
    }
    SECTION("sin configurar, a un proveedor con key guardada") {
        std::filesystem::remove(h.config_path()); // Sin config.json: NVIDIA, sin modelo.
        h.start(h.load());
        REQUIRE_FALSE(h.session->configured());
        CHECK(h.session->switch_model("nvidia", "m3").status == Status::Switched);
        CHECK(h.session->configured());
        CHECK(h.session->model() == "m3");
    }
    SECTION("el mismo proveedor y modelo no escribe nada") {
        h.start(h.load());
        const std::string config_before = read_file(h.config_path());
        const std::size_t entries = h.session->conversation().entries().size();
        const chatbot::cli::ModelSwitch got = h.session->switch_model("nvidia", "m1");
        CHECK(got.status == Status::Unchanged);
        CHECK(got.message == "Ya estás usando m1.");
        CHECK(h.clients == 1);
        CHECK(read_file(h.config_path()) == config_before);
        CHECK(h.session->conversation().entries().size() == entries);
    }
    SECTION("errores, sin cambiar nada") {
        h.start(h.load());
        const std::string config_before = read_file(h.config_path());
        CHECK(h.session->switch_model("gemini", "g").message ==
              "Falta la key de Google Gemini. Agrégala en F2 → Proveedor de IA.");
        CHECK(h.session->switch_model("custom:", "x").message ==
              "Proveedor desconocido: custom:. Escribe /model para ver la lista.");
        const chatbot::cli::ModelSwitch unknown = h.session->switch_model("nope", "x");
        CHECK(unknown.status == Status::Invalid);
        CHECK(unknown.message == "Proveedor desconocido: nope. Escribe /model para ver la lista.");
        CHECK(h.session->switch_model(std::nullopt, "").message ==
              "Elige un modelo de la lista o escríbelo.");
        CHECK(read_file(h.config_path()) == config_before);
        CHECK(h.session->model() == "m1");
    }
    SECTION("con CHAT_MODEL, nada") {
        h.env.values["CHAT_MODEL"] = "modelo-del-entorno";
        h.start(h.load());
        CHECK(h.session->model_locked());
        const chatbot::cli::ModelSwitch got = h.session->switch_model(std::nullopt, "m2");
        CHECK(got.status == Status::Invalid);
        CHECK(got.message == "El modelo está fijado por CHAT_MODEL.");
        CHECK(h.session->command_context().model_locked);
        check_status(h.session->submit("/model"), "El modelo está fijado por CHAT_MODEL.");
    }
    SECTION("con CHAT_BASE_URL, el mismo proveedor sí; otro no") {
        h.env.values["CHAT_BASE_URL"] = "https://integrate.api.nvidia.com/v1";
        h.start(h.load());
        const chatbot::cli::ModelSwitch other = h.session->switch_model("ollama", "q");
        CHECK(other.status == Status::Invalid);
        CHECK(other.message == "El proveedor está fijado por CHAT_BASE_URL.");
        CHECK(h.session->switch_model("nvidia", "m2").status == Status::Switched);
        CHECK(h.session->model() == "m2");
    }
    SECTION("con una respuesta en curso, kWaitOrCancel") {
        BlockingTransport* blocking = nullptr;
        h.chat_transport = [&blocking] {
            auto transport = std::make_unique<BlockingTransport>(BlockingTransport::Mode::Block);
            blocking = transport.get();
            return transport;
        };
        h.start(h.load());
        REQUIRE(h.session->submit("hola").type == Type::Started);
        REQUIRE(blocking->started.wait_for(kThreadTimeout));
        const chatbot::cli::ModelSwitch got = h.session->switch_model(std::nullopt, "m2");
        CHECK(got.status == Status::Busy);
        CHECK(got.message == chatbot::cli::kWaitOrCancel);
        check_status(h.session->submit("/model m2"), std::string{chatbot::cli::kWaitOrCancel});
        CHECK(h.session->model() == "m1");
        h.session->cancel();
        REQUIRE(h.finish());
    }
}

TEST_CASE("Session::models_config de un proveedor elegible con uno desconocido en config.json",
          "[session][model]") {
    const std::string provider = GENERATE("custom", "custom:no-existe");
    CAPTURE(provider);
    Harness h;
    chatbot::Credentials credentials;
    credentials.keys["nvidia"] = "sk-clave-secreta-de-prueba-1234";
    REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
    h.start(h.load_with_provider(provider));
    for (const char* id : {"nvidia", "ollama"}) {
        CAPTURE(id);
        REQUIRE(h.session->provider_problem(id) == std::nullopt);
        const chatbot::Result<chatbot::Config> config = h.session->models_config(id);
        REQUIRE(config.is_ok());
        CHECK(config.value().provider == id);
        CHECK(config.value().provider_error.empty());
    }
}

TEST_CASE("Session::provider_choices dice qué proveedores se pueden elegir", "[session][model]") {
    Harness h;
    save_nvidia(h);
    h.start(h.load());
    const auto choices = h.session->provider_choices();
    REQUIRE(choices.is_ok());
    REQUIRE(choices.value().size() == chatbot::providers().size());
    const auto find = [&](std::string_view id) {
        for (const chatbot::cli::ProviderChoice& choice : choices.value()) {
            if (choice.id == id) {
                return choice;
            }
        }
        FAIL("sin " << id);
        return chatbot::cli::ProviderChoice{};
    };
    const auto nvidia = find("nvidia");
    CHECK(nvidia.current);
    CHECK(nvidia.key_saved);
    CHECK(nvidia.selectable);
    CHECK(nvidia.note.empty());
    const auto ollama = find("ollama");
    CHECK_FALSE(ollama.current);
    CHECK_FALSE(ollama.key_saved);
    CHECK(ollama.local);
    CHECK(ollama.selectable);
    const auto openrouter = find("openrouter");
    CHECK_FALSE(openrouter.selectable);
    CHECK(openrouter.note == chatbot::cli::kNoKeyNote);
    CHECK(h.session->provider_problem("ollama") == std::nullopt);
    CHECK(h.session->provider_problem("openrouter") ==
          "Falta la key de OpenRouter. Agrégala en F2 → Proveedor de IA.");

    // models_config: la key guardada de ese proveedor, nunca la de otro.
    const chatbot::Result<chatbot::Config> nim = h.session->models_config("nvidia");
    REQUIRE(nim.is_ok());
    CHECK(nim.value().api_key == "sk-clave-secreta-de-prueba-1234");
    const chatbot::Result<chatbot::Config> local = h.session->models_config("ollama");
    REQUIRE(local.is_ok());
    CHECK(local.value().base_url == "http://localhost:11434/v1");
    CHECK(local.value().api_key.empty());
    CHECK(h.session->models_config("openrouter").is_error());

    SECTION("con CHAT_API_KEY todos tienen key, aunque no esté guardada") {
        h.session.reset();
        h.env.values["CHAT_API_KEY"] = "sk-clave-del-entorno-de-prueba-99";
        h.start(h.load());
        const auto with_env = h.session->provider_choices();
        REQUIRE(with_env.is_ok());
        for (const chatbot::cli::ProviderChoice& choice : with_env.value()) {
            INFO(choice.id);
            CHECK(choice.selectable);
        }
        CHECK(h.session->switch_model("gemini", "g").status ==
              chatbot::cli::ModelSwitch::Status::Switched);
    }
}

TEST_CASE("Session::submit con /model", "[session][model]") {
    Harness h;
    save_nvidia(h);
    h.start(h.load());
    SECTION("sin argumentos abre el selector en el paso de proveedor") {
        const SubmitOutcome got = h.session->submit("/model");
        CHECK(got.type == Type::OpenModelPicker);
        CHECK_FALSE(got.picker_provider.has_value());
        CHECK(got.clear);
        CHECK(got.command == chatbot::cli::ParsedCommand::Type::Model);
    }
    SECTION("con un proveedor elegible, en el paso de modelo") {
        const SubmitOutcome got = h.session->submit("/model Ollama");
        CHECK(got.type == Type::OpenModelPicker);
        CHECK(got.picker_provider == "ollama");
    }
    SECTION("con un proveedor sin key, el error") {
        check_status(h.session->submit("/model gemini"),
                     "Falta la key de Google Gemini. Agrégala en F2 → Proveedor de IA.");
    }
    SECTION("con un modelo, cambia y vacía la caja") {
        const SubmitOutcome got = h.session->submit("/model ollama qwen3:8b");
        CHECK(got.type == Type::Done);
        CHECK(got.clear);
        CHECK(got.action.message == "Ahora se usa qwen3:8b de Ollama (local).");
        CHECK_FALSE(got.action.error);
        CHECK(h.session->model() == "qwen3:8b");
    }
    SECTION("errores de uso y el mismo modelo, a la línea de estado") {
        check_status(h.session->submit("/model a b c"), std::string{chatbot::cli::kModelUsage});
        check_status(h.session->submit("/model x m"),
                     "Proveedor desconocido: x. Escribe /model para ver la lista.");
        check_status(h.session->submit("/model m1"), "Ya estás usando m1.");
    }
}

namespace {

/// config.json con NVIDIA en uso y dos endpoints: "a" (con key) y "b" (local, sin key).
void save_endpoints(Harness& h, const std::string& provider = "nvidia") {
    chatbot::Credentials credentials;
    credentials.keys["nvidia"] = "sk-clave-secreta-de-prueba-1234";
    credentials.keys["custom:a"] = "sk-clave-del-endpoint-a-5678";
    REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
    chatbot::ConfigFileValues values;
    values.provider = provider;
    values.base_url = "https://integrate.api.nvidia.com/v1";
    values.model = "m1";
    values.endpoints = {{"a", "Falso A", "https://a.example/v1"},
                        {"b", "Falso B", "http://localhost:8001/v1"}};
    REQUIRE_FALSE(chatbot::save_config_file(h.config_path(), values).has_value());
}

} // namespace

TEST_CASE("Session: endpoints en provider_choices y switch_model", "[session][model][endpoints]") {
    using Status = chatbot::cli::ModelSwitch::Status;
    Harness h;
    save_endpoints(h);
    h.start(h.load());

    const auto choices = h.session->provider_choices();
    REQUIRE(choices.is_ok());
    const std::size_t table = chatbot::providers().size();
    REQUIRE(choices.value().size() == table + 2); // Sin la fila de agregar.
    const chatbot::cli::ProviderChoice& a = choices.value()[table];
    CHECK(a.id == "custom:a");
    CHECK(a.name == "Falso A");
    CHECK(a.endpoint);
    CHECK(a.base_url == "https://a.example/v1");
    CHECK(a.key_saved);
    CHECK(a.selectable);
    const chatbot::cli::ProviderChoice& b = choices.value()[table + 1];
    CHECK(b.local);
    CHECK_FALSE(b.key_saved);
    CHECK(b.selectable);
    CHECK_FALSE(b.needs_key);

    // Entre endpoints y la tabla, con y sin "custom:".
    chatbot::cli::ModelSwitch got = h.session->switch_model("a", "m-a");
    CHECK(got.status == Status::Switched);
    CHECK(got.message == "Ahora se usa m-a de Falso A.");
    CHECK(h.last_entry().text == got.message);
    CHECK(h.session->config().provider == "custom:a");
    CHECK(h.session->config().base_url == "https://a.example/v1");
    CHECK(h.session->config().api_key == "sk-clave-del-endpoint-a-5678");
    got = h.session->switch_model("ollama", "q");
    CHECK(got.status == Status::Switched);
    got = h.session->switch_model("custom:b", "m-b");
    CHECK(got.message == "Ahora se usa m-b de Falso B.");
    CHECK(h.session->config().base_url == "http://localhost:8001/v1");
    CHECK(h.session->config().api_key.empty());
    got = h.session->switch_model("CUSTOM:A", "m-a");
    CHECK(got.status == Status::Switched);
    CHECK(h.session->config().provider == "custom:a");
    const chatbot::ConfigFileValues saved = chatbot::load_config_file_values(h.config_path()).value();
    CHECK(saved.provider == "custom:a");
    CHECK(saved.endpoints.size() == 2); // Nunca se pierden.
    CHECK(read_file(h.config_path()).find("integrate.api.nvidia.com") == std::string::npos);

    CHECK(h.session->switch_model("custom:zzz", "m").message ==
          "Proveedor desconocido: custom:zzz. Escribe /model para ver la lista.");
    CHECK(h.session->switch_model("zzz", "m").message ==
          "Proveedor desconocido: zzz. Escribe /model para ver la lista.");

    // /model con el id solo o con "custom:" hace lo mismo.
    SubmitOutcome bare = h.session->submit("/model b m-b");
    CHECK(bare.type == Type::Done);
    CHECK(h.session->config().provider == "custom:b");
    SubmitOutcome prefixed = h.session->submit("/model custom:a m-a");
    CHECK(prefixed.type == Type::Done);
    CHECK(h.session->config().provider == "custom:a");
    const SubmitOutcome picker = h.session->submit("/model B");
    CHECK(picker.type == Type::OpenModelPicker);
    CHECK(picker.picker_provider == "custom:b");
}

TEST_CASE("Session: guardar y borrar endpoints", "[session][endpoints]") {
    using Status = chatbot::cli::EndpointDeletion::Status;
    Harness h;
    save_endpoints(h);
    h.start(h.load());

    SECTION("crear uno nuevo con su key") {
        auto form = h.session->provider_form().value();
        form.select_provider(*form.find_provider("custom:"));
        form.set_name("Mi Gateway");
        form.set_base_url("https://gw.example/v1");
        form.set_key("sk-clave-del-gateway-0000");
        form.set_model("qwen3");
        REQUIRE(h.session->save_provider(form) == std::nullopt);
        CHECK(h.last_entry().text == "Ahora se usa qwen3 de Mi Gateway.");
        CHECK(h.session->config().provider == "custom:mi-gateway");
        CHECK(h.session->config().api_key == "sk-clave-del-gateway-0000");
        const auto credentials = chatbot::load_credentials(h.credentials_path()).value();
        CHECK(credentials.keys.at("custom:mi-gateway") == "sk-clave-del-gateway-0000");
        CHECK(chatbot::load_config_file_values(h.config_path()).value().endpoints.size() == 3);
    }
    SECTION("editar nombre y URL no pierde la key") {
        auto form = h.session->provider_form().value();
        form.select_provider(*form.find_provider("a"));
        form.set_name("Falso A nuevo");
        form.set_base_url("https://a2.example/v1");
        form.set_model("m-a");
        REQUIRE(h.session->save_provider(form) == std::nullopt);
        CHECK(h.session->config().base_url == "https://a2.example/v1");
        CHECK(h.session->config().api_key == "sk-clave-del-endpoint-a-5678");
        const auto values = chatbot::load_config_file_values(h.config_path()).value();
        CHECK(values.endpoints[0].id == "a");
        CHECK(values.endpoints[0].name == "Falso A nuevo");
    }
    SECTION("borrar uno que no está en uso: también su key") {
        const chatbot::cli::EndpointDeletion done = h.session->delete_endpoint("custom:a");
        CHECK(done.status == Status::Deleted);
        CHECK(done.message == "Se borró el endpoint Falso A.");
        const auto values = chatbot::load_config_file_values(h.config_path()).value();
        REQUIRE(values.endpoints.size() == 1);
        CHECK(values.endpoints[0].id == "b");
        CHECK(values.provider == "nvidia");
        CHECK(values.base_url == "https://integrate.api.nvidia.com/v1");
        const auto credentials = chatbot::load_credentials(h.credentials_path()).value();
        CHECK_FALSE(credentials.keys.contains("custom:a"));
        CHECK(credentials.keys.contains("nvidia"));
        CHECK(h.session->delete_endpoint("custom:a").message == "Ese endpoint ya no existe.");
        // El id solo también sirve; sin key no toca credentials.json.
        CHECK(h.session->delete_endpoint("b").status == Status::Deleted);
        CHECK(h.session->provider_choices().value().size() == chatbot::providers().size());
    }
    SECTION("no se borra el que está en uso, ni uno que no existe") {
        REQUIRE(h.session->switch_model("a", "m-a").status ==
                chatbot::cli::ModelSwitch::Status::Switched);
        const std::string config_before = read_file(h.config_path());
        const chatbot::cli::EndpointDeletion in_use = h.session->delete_endpoint("custom:a");
        CHECK(in_use.status == Status::Invalid);
        CHECK(in_use.message == "Cambia de proveedor antes de borrar este endpoint.");
        CHECK(h.session->delete_endpoint("custom:").message == "Ese endpoint ya no existe.");
        CHECK(h.session->delete_endpoint("nvidia").message == "Ese endpoint ya no existe.");
        CHECK(read_file(h.config_path()) == config_before);
        CHECK(chatbot::load_credentials(h.credentials_path()).value().keys.contains("custom:a"));
    }
}

TEST_CASE("Session: borrar el último endpoint deja credentials.json sin keys y se vuelve a cargar",
          "[session][endpoints]") {
    Harness h;
    chatbot::Credentials credentials;
    credentials.keys["custom:solo"] = "sk-clave-del-unico-endpoint-1234";
    REQUIRE_FALSE(chatbot::save_credentials(h.credentials_path(), credentials).has_value());
    chatbot::ConfigFileValues values;
    values.provider = "ollama";
    values.base_url = "http://localhost:11434/v1";
    values.model = "qwen3:8b";
    values.endpoints = {{"solo", "Solo", "https://solo.example/v1"}};
    REQUIRE_FALSE(chatbot::save_config_file(h.config_path(), values).has_value());
    h.start(h.load());

    REQUIRE(h.session->delete_endpoint("solo").status ==
            chatbot::cli::EndpointDeletion::Status::Deleted);
    const nlohmann::json written = nlohmann::json::parse(read_file(h.credentials_path()));
    CHECK(written.at("version") == 1);
    CHECK(written.at("keys") == nlohmann::json::object());
    CHECK(read_file(h.config_path()).find("endpoints") == std::string::npos);
    // Se vuelve a cargar sin error, como al arrancar.
    const chatbot::Result<chatbot::Credentials> reloaded =
        chatbot::load_credentials(h.credentials_path());
    REQUIRE(reloaded.is_ok());
    CHECK(reloaded.value().keys.empty());
    const chatbot::Config config = h.load();
    CHECK(config.provider == "ollama");
    CHECK(config.warnings.empty());
    CHECK(h.session->provider_form().is_ok());
}

TEST_CASE("Session: un proveedor que no existe deja la app sin configurar", "[session][endpoints]") {
    Harness h;
    save_endpoints(h, "custom:borrado");
    h.start(h.load());
    CHECK_FALSE(h.session->configured());
    CHECK(h.session->setup_problem() == "El endpoint borrado no existe. Elige un proveedor en F2.");
    check_status(h.session->submit("hola"), h.session->setup_problem());
    // Al elegir uno que sí existe, queda configurada.
    CHECK(h.session->switch_model("a", "m-a").status == chatbot::cli::ModelSwitch::Status::Switched);
    CHECK(h.session->configured());
    CHECK(h.session->setup_problem() == chatbot::cli::kSetupNotice);
}

TEST_CASE("Session: los endpoints ignorados son avisos de arranque", "[session][endpoints]") {
    Harness h;
    std::ofstream{h.config_path()}
        << R"({"provider":"nvidia","model":"m1","endpoints":[{"id":"Mal","name":"x","base_url":"https://x/v1"}]})";
    h.start(h.load());
    REQUIRE(h.session->startup_warnings().size() == 1);
    CHECK(h.session->startup_warnings()[0] ==
          "Se ignoró un endpoint de config.json: el id no es válido.");
}
