// ModelPicker: el selector de /model sin FTXUI. La lista de modelos sale de
// su ModelsLoader, con el post en una TaskQueue que la prueba vacía.

#include "model_picker.h"

#include "blocking_transport.hpp"
#include "fake_transport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using chatbot::cli::ListKey;
using chatbot::cli::ModelPicker;
using chatbot::cli::ModelPickerAction;
using chatbot::cli::ModelPickerRow;
using chatbot::cli::ModelsState;
using chatbot::cli::ProviderChoice;
using chatbot_test::BlockingTransport;
using chatbot_test::TaskQueue;
using Action = ModelPickerAction::Type;
using Kind = ModelPickerRow::Kind;

std::vector<ProviderChoice> choices() {
    return {
        {.id = "nvidia", .name = "NVIDIA NIM", .current = true, .key_saved = true,
         .selectable = true},
        {.id = "openrouter", .name = "OpenRouter", .note = "sin key"},
        {.id = "ollama", .name = "Ollama (local)", .needs_key = false, .local = true,
         .selectable = true},
        {.id = "custom:lab", .name = "Servidor del lab", .base_url = "https://lab.example/v1",
         .endpoint = true, .note = "sin key"},
    };
}

chatbot::HttpResponse models(const std::string& body) {
    chatbot::HttpResponse response;
    response.status = 200;
    response.body = body;
    return response;
}

/// Reenvía a un transporte compartido (el loader crea uno por petición).
class Forward final : public chatbot::Transport {
public:
    explicit Forward(std::shared_ptr<chatbot::Transport> target) : target_(std::move(target)) {}
    [[nodiscard]] chatbot::HttpResponse send(const chatbot::HttpRequest& request) override {
        return target_->send(request);
    }
    [[nodiscard]] chatbot::HttpResponse send_stream(
        const chatbot::HttpRequest& request, const chatbot::StreamCallback& on_chunk) override {
        return target_->send_stream(request, on_chunk);
    }

private:
    std::shared_ptr<chatbot::Transport> target_;
};

struct Harness {
    TaskQueue queue;
    /// Lo que responde cada petición de modelos.
    std::function<std::unique_ptr<chatbot::Transport>()> transport = [] {
        auto fake = std::make_unique<chatbot_test::FakeTransport>();
        fake->responses = {models(R"({"data":[{"id":"qwen3:8b"},{"id":"llama3"},{"id":"m1"}]})")};
        return fake;
    };
    std::vector<std::string> asked; ///< Proveedores de cada models_config.
    std::unique_ptr<ModelPicker> picker;

    Harness() {
        picker = std::make_unique<ModelPicker>(
            [this](std::string_view provider) -> chatbot::Result<chatbot::Config> {
                asked.emplace_back(provider);
                if (provider == "openrouter") {
                    return chatbot::ChatError{chatbot::ErrorKind::Config, 0,
                                              "Falta la key de OpenRouter.", std::nullopt};
                }
                chatbot::Config config;
                config.base_url = "http://localhost:11434/v1";
                config.model = "x";
                return config;
            },
            [this] { return transport(); },
            [this](ModelsLoader::Task task) { queue.post(std::move(task)); });
    }

    bool loaded() {
        return queue.run_until([this] { return picker->models_state() != ModelsState::Loading; });
    }

    [[nodiscard]] std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (const ModelPickerRow& row : picker->rows()) {
            out.push_back(row.text);
        }
        return out;
    }
    [[nodiscard]] ModelPickerRow at_cursor() const { return picker->rows().at(picker->cursor()); }

    using ModelsLoader = chatbot::cli::ModelsLoader;
};

} // namespace

TEST_CASE("ModelPicker: paso de proveedor, filtro y filas que no se eligen", "[model_picker]") {
    Harness h;
    CHECK(h.picker->open(choices(), std::nullopt, "m1").type == Action::Handled);
    REQUIRE(h.picker->is_open());
    CHECK_FALSE(h.picker->model_step());
    CHECK(h.texts() == std::vector<std::string>{"nvidia", "openrouter", "ollama", "custom:lab"});
    // El cursor empieza en el actual, que va marcado.
    CHECK(h.at_cursor().text == "nvidia");
    CHECK(h.at_cursor().current);

    SECTION("lo escrito filtra por id o nombre, sin distinguir mayúsculas") {
        h.picker->update("LOCAL");
        CHECK(h.texts() == std::vector<std::string>{"ollama"});
        h.picker->update("zzz");
        REQUIRE(h.picker->rows().size() == 1);
        CHECK(h.picker->rows()[0].kind == Kind::Notice);
        CHECK(h.picker->rows()[0].text == ModelPicker::kNoProviders);
        CHECK(h.picker->handle(ListKey::Enter).type == Action::Handled);
    }
    SECTION("↑/↓ dan la vuelta; Tab no hace nada; lo demás pasa") {
        CHECK(h.picker->handle(ListKey::Up).type == Action::Handled);
        CHECK(h.at_cursor().text == "custom:lab");
        (void)h.picker->handle(ListKey::Down);
        CHECK(h.at_cursor().text == "nvidia");
        CHECK(h.picker->handle(ListKey::Tab).type == Action::Handled);
        CHECK(h.picker->handle(ListKey::Other).type == Action::Pass);
    }
    SECTION("Enter en uno que no se puede elegir: el error, sin cambiar de paso") {
        (void)h.picker->handle(ListKey::Down);
        const ModelPickerAction got = h.picker->handle(ListKey::Enter);
        CHECK(got.type == Action::Status);
        CHECK(got.text == "Falta la key de OpenRouter.");
        CHECK_FALSE(h.picker->model_step());
        CHECK(h.picker->is_open());
    }
    SECTION("Esc en el paso de proveedor cierra") {
        CHECK(h.picker->handle(ListKey::Escape).type == Action::Closed);
        CHECK_FALSE(h.picker->is_open());
        CHECK(h.picker->handle(ListKey::Down).type == Action::Pass);
    }
}

TEST_CASE("ModelPicker: paso de modelo, carga, filtro y Usar «…»", "[model_picker]") {
    Harness h;
    (void)h.picker->open(choices(), std::nullopt, "m1");
    // nvidia es el actual: su lista marca m1.
    CHECK(h.picker->handle(ListKey::Enter).type == Action::ClearInput);
    CHECK(h.picker->model_step());
    CHECK(h.picker->can_go_back());
    CHECK(h.picker->provider_name() == "NVIDIA NIM");
    CHECK(h.asked == std::vector<std::string>{"nvidia"});
    CHECK(h.picker->models_state() == ModelsState::Loading);
    CHECK(h.texts() == std::vector<std::string>{std::string{ModelPicker::kLoading}});
    CHECK(h.picker->handle(ListKey::Enter).type == Action::Handled); // Nada que elegir.
    REQUIRE(h.loaded());
    CHECK(h.picker->models_state() == ModelsState::Loaded);
    // ModelsLoader devuelve la lista ordenada.
    CHECK(h.texts() == std::vector<std::string>{"llama3", "m1", "qwen3:8b"});
    CHECK(h.at_cursor().text == "m1");
    CHECK(h.at_cursor().current);

    SECTION("Enter elige el modelo") {
        const ModelPickerAction got = h.picker->handle(ListKey::Enter);
        CHECK(got.type == Action::Switch);
        CHECK(got.provider == "nvidia");
        CHECK(got.text == "m1");
    }
    SECTION("el filtro deja solo los que contienen el texto") {
        h.picker->update("QWEN");
        CHECK(h.texts() == std::vector<std::string>{"qwen3:8b"});
        CHECK(h.at_cursor().kind == Kind::Model);
    }
    SECTION("si el filtro no deja nada, la última fila es Usar «…» con el texto tal cual") {
        h.picker->update(" Mi-Modelo/Raro:1 ");
        REQUIRE(h.picker->rows().size() == 1);
        CHECK(h.at_cursor().kind == Kind::UseTyped);
        const ModelPickerAction got = h.picker->handle(ListKey::Enter);
        CHECK(got.type == Action::Switch);
        CHECK(got.text == "Mi-Modelo/Raro:1");
    }
    SECTION("Esc regresa al paso de proveedor, con el cursor en el de antes") {
        CHECK(h.picker->handle(ListKey::Escape).type == Action::ClearInput);
        CHECK_FALSE(h.picker->model_step());
        CHECK(h.picker->is_open());
        CHECK(h.at_cursor().text == "nvidia");
        CHECK(h.picker->handle(ListKey::Escape).type == Action::Closed);
    }
}

TEST_CASE("ModelPicker: la lista falla o no la hay", "[model_picker]") {
    Harness h;
    h.transport = [] {
        auto fake = std::make_unique<chatbot_test::FakeTransport>();
        chatbot::HttpResponse missing;
        missing.status = 404;
        missing.body = R"({"error":{"message":"no hay"}})";
        fake->responses = {missing};
        return fake;
    };
    (void)h.picker->open(choices(), std::string{"ollama"}, "m1");
    CHECK(h.picker->model_step());
    CHECK_FALSE(h.picker->can_go_back());
    REQUIRE(h.loaded());
    CHECK(h.picker->models_state() == ModelsState::Failed);
    CHECK(h.picker->models_error().starts_with("["));
    // Sin texto: el aviso para escribirlo; con texto, Usar «…».
    CHECK(h.texts() == std::vector<std::string>{std::string{ModelPicker::kTypeModel}});
    h.picker->update("qwen3:8b");
    CHECK(h.at_cursor().kind == Kind::UseTyped);
    const ModelPickerAction got = h.picker->choose(0);
    CHECK(got.type == Action::Switch);
    CHECK(got.provider == "ollama");
    CHECK(got.text == "qwen3:8b");
    // Se llegó directo al paso de modelo: Esc cierra.
    CHECK(h.picker->handle(ListKey::Escape).type == Action::Closed);
}

TEST_CASE("ModelPicker: abrir en un proveedor que no se puede elegir no abre", "[model_picker]") {
    Harness h;
    const ModelPickerAction got = h.picker->open(choices(), std::string{"openrouter"}, "m1");
    CHECK(got.type == Action::Status);
    CHECK(got.text == "Falta la key de OpenRouter.");
    CHECK_FALSE(h.picker->is_open());
}

TEST_CASE("ModelPicker: regresar o cerrar cancela la lista en curso", "[model_picker]") {
    Harness h;
    auto blocking = std::make_shared<BlockingTransport>(BlockingTransport::Mode::Block);
    h.transport = [blocking] { return std::make_unique<Forward>(blocking); };
    (void)h.picker->open(choices(), std::nullopt, "m1");
    (void)h.picker->handle(ListKey::Enter);
    REQUIRE(blocking->started.wait_for(chatbot_test::kThreadTimeout));
    // Esc regresa: la petición se cancela y su resultado ya no llega.
    CHECK(h.picker->handle(ListKey::Escape).type == Action::ClearInput);
    CHECK(h.picker->models_state() == ModelsState::Idle);
    // Otro proveedor: otra petición.
    h.transport = [] {
        auto fake = std::make_unique<chatbot_test::FakeTransport>();
        fake->responses = {models(R"({"data":[{"id":"llama3"}]})")};
        return fake;
    };
    (void)h.picker->handle(ListKey::End); // El endpoint.
    (void)h.picker->handle(ListKey::Up);  // ollama
    CHECK(h.picker->handle(ListKey::Enter).type == Action::ClearInput);
    REQUIRE(h.loaded());
    CHECK(h.texts() == std::vector<std::string>{"llama3"});
    // Cerrar con una petición en curso también la cancela.
    (void)h.picker->handle(ListKey::Escape);
    h.transport = [blocking] { return std::make_unique<Forward>(blocking); };
    (void)h.picker->handle(ListKey::Enter);
    h.picker->close();
    CHECK_FALSE(h.picker->is_open());
    CHECK(h.picker->models_state() == ModelsState::Idle);
    h.picker.reset(); // Une los hilos: la cancelación llega en menos de 2 s.
}

TEST_CASE("ModelPicker: ventana de filas, ratón y rueda", "[model_picker]") {
    Harness h;
    h.transport = [] {
        std::string body = R"({"data":[)";
        for (int i = 0; i < 20; ++i) {
            body += std::string{i == 0 ? "" : ","} + R"({"id":"m)" + (i < 10 ? "0" : "") +
                    std::to_string(i) + R"("})";
        }
        body += "]}";
        auto fake = std::make_unique<chatbot_test::FakeTransport>();
        fake->responses = {models(body)};
        return fake;
    };
    (void)h.picker->open(choices(), std::string{"ollama"}, "");
    REQUIRE(h.loaded());
    REQUIRE(h.picker->rows().size() == 20);
    CHECK(h.picker->cursor() == 0);
    CHECK(h.picker->first() == 0);
    (void)h.picker->handle(ListKey::End);
    CHECK(h.picker->cursor() == 19);
    CHECK(h.picker->first() == 20 - ModelPicker::kVisibleRows);
    h.picker->scroll(3); // Sin dar la vuelta.
    CHECK(h.picker->cursor() == 19);
    h.picker->scroll(-15);
    CHECK(h.picker->cursor() == 4);
    CHECK(h.picker->first() == 4);
    h.picker->select(6); // Hover.
    CHECK(h.picker->cursor() == 6);
    const ModelPickerAction got = h.picker->choose(7);
    CHECK(got.type == Action::Switch);
    CHECK(got.text == "m07");
    CHECK(h.picker->choose(99).type == Action::Pass);
}

TEST_CASE("ModelPicker::finish cierra con éxito y deja abierto con error", "[model_picker]") {
    using Status = chatbot::cli::ModelSwitch::Status;
    Harness h;
    (void)h.picker->open(choices(), std::string{"ollama"}, "m1");
    // Un error: el aviso y el selector sigue abierto.
    CHECK(h.picker->finish({Status::Busy, "Espera"}) == "Espera");
    CHECK(h.picker->is_open());
    // Ya era el actual: cierra y dice el aviso.
    CHECK(h.picker->finish({Status::Unchanged, "Ya estás usando m1."}) == "Ya estás usando m1.");
    CHECK_FALSE(h.picker->is_open());
    // Cambió: cierra y el aviso ya está en el historial.
    (void)h.picker->open(choices(), std::string{"ollama"}, "m1");
    CHECK(h.picker->finish({Status::Switched, "Ahora se usa x de y."}) == std::nullopt);
    CHECK_FALSE(h.picker->is_open());
}
