// Lo falso para armar un chatbot::cli::CodeMode sin red: guiones SSE con llamadas a herramientas,
// un proyecto temporal y el Harness (SessionFakes, una cola como hilo de la interfaz y git
// falso). Lo usan test_code_mode.cpp, test_code_screen.cpp y test_code_command.cpp.

#ifndef CHATBOT_TEST_CODE_MODE_FAKES_HPP
#define CHATBOT_TEST_CODE_MODE_FAKES_HPP

#include "code_mode.h"

#include "blocking_transport.hpp"
#include "session_fakes.hpp"
#include "temp_dir.hpp"

#include "chatbot/child_process.h"
#include "chatbot/credentials.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chatbot_test {

// --- Guiones SSE (también los de test_code_command.cpp) ---

inline std::string chunk(nlohmann::json delta) {
    nlohmann::json choice = nlohmann::json::object();
    choice["delta"] = std::move(delta);
    nlohmann::json body = nlohmann::json::object();
    body["choices"] = nlohmann::json::array({choice});
    return "data: " + body.dump() + "\n\n";
}

inline const std::string kDone = "data: [DONE]\n\n";

inline chatbot::HttpResponse ok(std::string body) {
    return chatbot::HttpResponse{200, std::move(body), std::nullopt, false, ""};
}

inline chatbot::HttpResponse answer(const std::string& text) {
    return ok(chunk({{"content", text}}) + kDone);
}

struct Call {
    std::string name;
    std::string arguments;
};

/// Una respuesta con texto opcional antes y estas llamadas, cada una en su pedazo.
inline chatbot::HttpResponse calls(const std::vector<Call>& list, const std::string& text = {}) {
    std::string flow = text.empty() ? std::string{} : chunk({{"content", text}});
    for (std::size_t index = 0; index < list.size(); ++index) {
        nlohmann::json function = nlohmann::json::object();
        function["name"] = list[index].name;
        function["arguments"] = list[index].arguments;
        nlohmann::json call = nlohmann::json::object();
        call["index"] = index;
        call["id"] = "call_" + std::to_string(index) + "_" + list[index].name;
        call["type"] = "function";
        call["function"] = function;
        flow += chunk({{"tool_calls", nlohmann::json::array({call})}});
    }
    return ok(flow + "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n" +
              kDone);
}

inline void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream{path, std::ios::binary} << text;
}

inline std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

inline constexpr const char* kNvidiaKey = "sk-clave-secreta-de-prueba-1234";

/// Lo que comparte run_process con la prueba (corre en el hilo de trabajo).
struct GitProbe {
    bool block = false;           ///< ls-files espera la cancelación (si no, git "no existe").
    chatbot::CancelToken listing; ///< Aviso: glob pidió la lista a git.
};

/// Un CodeMode sobre un proyecto temporal; la configuración y las conversaciones en otra
/// carpeta (SessionFakes).
struct CodeModeHarness {
    SessionFakes fakes;
    ScopedTempDir projects;
    std::filesystem::path project = projects.path() / "proy";
    TaskQueue queue;
    std::shared_ptr<GitProbe> git = std::make_shared<GitProbe>();
    std::string home;     ///< CodeModeEnvironment::home (el aviso).
    std::string display;  ///< SessionEnvironment::home (el título).
    std::vector<std::optional<std::string>> finished; ///< Lo de cada OnFinished.
    /// Además, después de anotarlo (la pantalla: CodeScreen::finished).
    std::function<void(std::optional<std::string>)> on_finished;
    std::unique_ptr<chatbot::cli::CodeMode> mode;

    CodeModeHarness() {
        std::filesystem::create_directories(project);
        write_file(project / "a.txt", "alfa\nbeta\n");
        write_file(project / "b.txt", "gamma\n");
    }

    chatbot::cli::CodeModeEnvironment environment() {
        chatbot::cli::SessionEnvironment session = fakes.environment();
        session.home = display;
        return chatbot::cli::CodeModeEnvironment{
            .session = std::move(session),
            .app_data_dir = std::nullopt,
            .portable_dir = std::nullopt,
            .home = home,
        };
    }

    /// Como la pantalla: open_code_project y luego CodeMode. git no existe, salvo con
    /// git->block: entonces el ls-files de glob espera hasta que lo cancelan.
    void start(const chatbot::Config& config) {
        chatbot::cli::CodeModeEnvironment env = environment();
        const chatbot::Result<chatbot::cli::CodeProject> opened =
            chatbot::cli::open_code_project(utf8(project), chatbot::cli::code_paths(env),
                                            [] { return std::time_t{1700000000}; });
        REQUIRE(opened.is_ok());
        chatbot::cli::CodeProject value = opened.value();
        value.prompt_options.git_path = "/fake/git";
        value.prompt_options.run_process = [probe = git](
                                               const std::vector<std::string>& argv,
                                               const chatbot::process::ProcessOptions& options) {
            chatbot::process::ProcessResult result;
            result.status = chatbot::process::ProcessResult::Status::NotFound;
            if (!probe->block || std::find(argv.begin(), argv.end(), "ls-files") == argv.end()) {
                return result;
            }
            probe->listing.cancel();
            const auto deadline = std::chrono::steady_clock::now() + 2 * kThreadTimeout;
            while (options.cancel != nullptr &&
                   !options.cancel->wait_for(std::chrono::milliseconds{20}) &&
                   std::chrono::steady_clock::now() < deadline) {
            }
            result.status = chatbot::process::ProcessResult::Status::Cancelled;
            return result;
        };
        mode = std::make_unique<chatbot::cli::CodeMode>(
            std::move(env), config, std::move(value),
            [this](std::function<void()> task) { queue.post(std::move(task)); },
            [this](std::optional<std::string> restored) {
                finished.push_back(restored);
                if (on_finished) {
                    on_finished(std::move(restored));
                }
            });
    }

    void start() { start(test_config()); }

    /// Vacía la cola hasta el fin del turno número count + 1.
    bool wait_turn(std::size_t count) {
        return queue.run_until([this, count] { return finished.size() > count; });
    }

    void run_turn(const std::string& text) {
        const std::size_t count = finished.size();
        REQUIRE(mode->send(text));
        REQUIRE(wait_turn(count));
    }

    [[nodiscard]] const std::vector<chatbot::cli::Entry>& entries() const {
        return mode->entries();
    }

    [[nodiscard]] std::vector<chatbot::cli::EntryKind> kinds() const {
        std::vector<chatbot::cli::EntryKind> result;
        for (const chatbot::cli::Entry& entry : entries()) {
            result.push_back(entry.kind);
        }
        return result;
    }

    /// Con key de NVIDIA guardada y config.json en nvidia/m1 (como test_session).
    void save_nvidia() const {
        chatbot::Credentials credentials;
        credentials.keys["nvidia"] = kNvidiaKey;
        REQUIRE_FALSE(chatbot::save_credentials(fakes.credentials_path(), credentials).has_value());
        REQUIRE_FALSE(chatbot::save_config_file(fakes.config_path(),
                                                {"nvidia", "https://integrate.api.nvidia.com/v1",
                                                 "m1"})
                          .has_value());
    }
};

} // namespace chatbot_test

#endif // CHATBOT_TEST_CODE_MODE_FAKES_HPP
