#include "settings_binding.h"

#include "app_startup.h"

#include "chatbot/config.h"
#include "chatbot/platform.h"
#include "chatbot/result.h"

#include <utility>

namespace chatbot::cli {

SettingsScreen make_settings_screen(ModelsLoader& loader, const SettingsContext& context) {
    return SettingsScreen(
        loader, context.palette,
        [context](const ProviderSettings& form) {
            std::optional<std::string> error = context.session.save_provider(form);
            if (!error.has_value()) {
                context.on_saved();
            }
            return error;
        },
        [context](const Appearance& chosen) -> std::optional<std::string> {
            const std::optional<std::string> config_path = default_config_path();
            if (!config_path.has_value()) {
                return missing_config_dir_message(current_os());
            }
            // Con NO_COLOR el tema y el fondo no se eligen: se quedan como
            // están en config.json (vacíos no se escriben).
            const bool no_color = no_color_enabled();
            const AppearanceValues values{
                no_color ? std::string{} : std::string{chosen.theme->id},
                no_color ? std::string{} : std::string{background_id(chosen.background)},
                std::string{symbol_mode_id(chosen.symbol_mode)}};
            if (const auto error = save_appearance(*config_path, values)) {
                return error->message;
            }
            // Se aplica sin reiniciar: la caché del historial se invalida
            // sola (su llave incluye el tema, el fondo y los símbolos).
            context.appearance = chosen;
            context.appearance.symbols =
                resolve_symbols(chosen.symbol_mode, context.detected_symbols);
            context.palette = Palette{context.appearance};
            return std::nullopt;
        },
        [context](const std::optional<std::string>& stored) {
            std::optional<std::string> error = context.session.save_system_prompt(stored);
            if (!error.has_value()) {
                context.on_saved();
            }
            return error;
        },
        [context](const std::string& key) {
            std::optional<std::string> error = context.session.save_search_key(key);
            if (!error.has_value()) {
                context.on_saved();
            }
            return error;
        },
        [context] { context.on_close(); });
}

void bind_settings(SettingsScreen& settings, const SettingsContext& context, Session::Post post) {
    settings.set_portable_dir(process_portable_data_dir());
    settings.set_model_params_handlers(
        [context](const ModelParams& draft) { return context.session.check_model_params(draft); },
        [context](const ModelParams& draft) {
            std::optional<std::string> error = context.session.save_model_params(draft);
            if (!error.has_value()) {
                context.on_saved();
            }
            return error;
        });
    settings.set_on_delete_endpoint([context](const std::string& id) {
        SettingsScreen::EndpointDeleted result;
        const EndpointDeletion deletion = context.session.delete_endpoint(id);
        result.message = deletion.message;
        if (deletion.status == EndpointDeletion::Status::Deleted) {
            Result<ProviderSettings> form = context.session.provider_form();
            if (form.is_ok()) {
                result.form = std::move(form).value();
            } else {
                result.message = form.error().message;
            }
        }
        return result;
    });
    settings.set_addons_environment(
        addons_environment(std::move(post), executable_path().value_or("")));
}

std::optional<std::string> open_settings(SettingsScreen& settings, const SettingsContext& context,
                                         std::string notice) {
    Session& session = context.session;
    if (session.busy()) {
        return std::string{kWaitOrCancel};
    }
    if (settings.is_open()) {
        settings.focus();
        return std::nullopt;
    }
    const Result<ProviderSettings> provider = session.provider_form();
    if (provider.is_error()) {
        return provider.error().message;
    }
    const Result<SearchSettings> search = session.search_form();
    if (search.is_error()) {
        return search.error().message;
    }
    context.on_open();
    ModelParamsPage model;
    model.problem = session.model_params_problem();
    model.model = session.config().model;
    model.provider = session.config().provider;
    model.provider_label = provider.value().provider_label();
    model.saved = session.model_params();
    settings.open(provider.value(), session.config(), context.appearance, session.system_prompt(),
                  search.value(), std::move(notice), std::move(model));
    return std::nullopt;
}

} // namespace chatbot::cli
