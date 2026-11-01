#ifndef CHATBOT_CLI_SETTINGS_SCREEN_H
#define CHATBOT_CLI_SETTINGS_SCREEN_H

#include "addons_panel.h"
#include "model_params_form.h"
#include "models_loader.h"
#include "paste.h"
#include "provider_settings.h"
#include "search_settings.h"
#include "text_area.h"
#include "theme.h"

#include "chatbot/config.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot::cli {

inline constexpr std::string_view kDiscardCancelled = "Cancelado: no se descartó nada.";

/// Lo que "Configuración del modelo" necesita al abrir: el modelo efectivo y
/// sus ajustes guardados (de Session).
struct ModelParamsPage {
    /// Por qué no se puede ajustar (kNoModelForParams), o nullopt.
    std::optional<std::string> problem{std::string{kNoModelForParams}};
    std::string model;
    std::string provider;       ///< El id ("nvidia", "custom:<id>").
    std::string provider_label; ///< Su nombre en pantalla.
    ModelParams saved;
};

/// Pantalla de configuración (FTXUI): categorías a la izquierda
/// ("Proveedor", "Colores y Accesibilidad", "Configuración del modelo",
/// "Búsqueda web" y "Addons", en AddonsPanel) y el formulario de la elegida a la derecha. La lógica del proveedor está en ProviderSettings; la lista de
/// modelos se pide con ModelsLoader, en su hilo. Se dibuja con la paleta
/// vigente (la de main.cpp, que cambia al guardar). Se usa solo desde el
/// hilo de la interfaz.
class SettingsScreen {
public:
    /// Guarda lo del formulario (config.json, credentials.json) y reconstruye
    /// el cliente. Devuelve el error para mostrar, o nullopt si se guardó.
    using OnSave = std::function<std::optional<std::string>(const ProviderSettings&)>;
    /// Guarda el tema y el fondo en config.json y los aplica. Devuelve el
    /// error para mostrar, o nullopt si se guardó.
    using OnSaveAppearance = std::function<std::optional<std::string>(const Appearance&)>;
    /// Guarda las instrucciones de sistema en config.json (nullopt: borrar la
    /// llave, se usan las predeterminadas) y las aplica. Devuelve el error
    /// para mostrar, o nullopt si se guardó.
    using OnSaveSystemPrompt =
        std::function<std::optional<std::string>(const std::optional<std::string>&)>;
    /// Guarda la key de búsqueda web en credentials.json ("search:tavily") y
    /// la aplica. Devuelve el error para mostrar, o nullopt si se guardó.
    using OnSaveSearchKey = std::function<std::optional<std::string>(const std::string&)>;
    /// La pantalla se cerró (guardada o descartada).
    using OnClose = std::function<void()>;
    /// Lo que devuelve OnDeleteEndpoint: el aviso (o el error) para la línea
    /// de estado y, si se borró, el formulario con lo que quedó guardado.
    struct EndpointDeleted {
        std::string message;
        std::optional<ProviderSettings> form;
    };
    /// Borra el endpoint id ("custom:<id>"), como Session::delete_endpoint.
    using OnDeleteEndpoint = std::function<EndpointDeleted(const std::string& id)>;
    /// Session::check_model_params: errores y vista previa del borrador.
    using OnCheckModelParams = std::function<ModelParamsCheck(const ModelParams&)>;
    /// Session::save_model_params: el error para mostrar, o nullopt.
    using OnSaveModelParams = std::function<std::optional<std::string>(const ModelParams&)>;

    /// palette debe vivir más que la pantalla.
    SettingsScreen(ModelsLoader& loader, const Palette& palette, OnSave on_save,
                   OnSaveAppearance on_save_appearance,
                   OnSaveSystemPrompt on_save_system_prompt, OnSaveSearchKey on_save_search_key,
                   OnClose on_close);

    /// Abre con un formulario nuevo. base: la configuración actual (timeout,
    /// volcado, límite del historial) para pedir los modelos. appearance: el
    /// tema y el fondo vigentes. system_prompt: las instrucciones vigentes
    /// (vacías: ninguna). search: la key de búsqueda guardada y la del
    /// entorno. notice: aviso inicial, si hay. model: el modelo efectivo y sus
    /// ajustes (sin él, Parámetros y Avanzado dicen kNoModelForParams). Pide
    /// la lista de modelos si ya hay URL y key.
    void open(ProviderSettings settings, Config base, Appearance appearance,
              std::string system_prompt, SearchSettings search, std::string notice = {},
              ModelParamsPage model = {});
    /// Revisar y guardar los ajustes del modelo; sin llamarla, Parámetros y
    /// Avanzado no se pueden ajustar.
    void set_model_params_handlers(OnCheckModelParams check, OnSaveModelParams save) {
        on_check_model_params_ = std::move(check);
        on_save_model_params_ = std::move(save);
    }
    /// Modo portable: la carpeta datos/ (chatbot::process_portable_data_dir),
    /// que se muestra atenuada al pie de "Proveedor de IA". nullopt (por
    /// defecto): sin modo portable, no se muestra nada.
    void set_portable_dir(std::optional<std::string> dir) { portable_dir_ = std::move(dir); }
    /// [Borrar endpoint]; sin llamarla, el botón no hace nada.
    void set_on_delete_endpoint(OnDeleteEndpoint on_delete) {
        on_delete_endpoint_ = std::move(on_delete);
    }
    /// El sistema para "Addons" (una vez, al arrancar); sin llamarla, la
    /// categoría dice que no están disponibles.
    void set_addons_environment(AddonsEnvironment environment) {
        addons_.set_environment(std::move(environment));
    }
    /// Esc, Cancelar o F2: cierra si no hay cambios; si los hay, pregunta
    /// "¿Descartar los cambios? (s/n)". Devuelve true si se cerró.
    bool request_close();
    /// Cierra sin preguntar y cancela la petición de modelos en curso.
    void close();

    [[nodiscard]] bool is_open() const { return settings_.has_value(); }
    /// Hay una pregunta (s/n) abierta (descartar, borrar un endpoint o de
    /// Addons): todo evento es de ella (yes_no.h).
    [[nodiscard]] bool asking() const {
        return confirm_discard_ || confirm_delete_ || addons_.question().has_value();
    }
    /// El componente (para Container::Tab).
    [[nodiscard]] ftxui::Component component() const { return root_; }
    /// Da el foco al primer campo.
    void focus();
    /// A dónde iría un pegado: el campo de texto con el foco (MultiLine en
    /// las instrucciones y el JSON avanzado, SingleLine en los demás) o None si el
    /// foco no está en un campo, hay una lista abierta o se está
    /// preguntando "¿Descartar los cambios?".
    [[nodiscard]] PasteTarget paste_target() const;
    /// Solo para el registro de teclas (CHAT_DEBUG_KEYS): Focused() de cada
    /// campo de texto y de la raíz de la pantalla.
    [[nodiscard]] std::vector<std::pair<std::string_view, bool>> debug_focus() const;
    /// Aviso bajo el formulario (por ejemplo, un pegado recortado).
    void show_notice(std::string notice) { status_ = std::move(notice); }

private:
    /// Categorías del menú de la izquierda.
    enum Category : int {
        kProvider = 0,
        kAppearance = 1,
        kModel = 2, ///< Configuración del modelo: instrucciones, parámetros y avanzado.
        kSearch = 3,
        kAddons = 4,
    };

    void request_models();
    void save();
    /// Copia al formulario lo que cambió en settings_ (tras cambiar de proveedor).
    void sync_fields();
    /// La lista del proveedor (provider_list), tras abrir, guardar o borrar.
    void sync_providers();
    /// [Borrar endpoint]: pregunta; en el que está en uso, el error de
    /// Session sin preguntar.
    void request_delete();
    void delete_endpoint();
    /// El endpoint elegido es el de config.json.
    [[nodiscard]] bool endpoint_in_use() const;
    /// Tab y Shift+Tab: siguiente campo enfocable.
    void move_focus(int step);
    bool handle_event(const ftxui::Event& event);
    [[nodiscard]] ftxui::Element render() const;
    [[nodiscard]] ftxui::Element render_models() const;
    [[nodiscard]] ftxui::Element render_provider() const;
    [[nodiscard]] ftxui::Element render_appearance() const;
    /// Fila "Símbolos" de Colores y Accesibilidad (también con NO_COLOR).
    [[nodiscard]] ftxui::Element render_symbols() const;
    [[nodiscard]] ftxui::Element render_model() const;
    [[nodiscard]] ftxui::Element render_system_prompt() const;
    [[nodiscard]] ftxui::Element render_params() const;
    [[nodiscard]] ftxui::Element render_advanced() const;
    /// Se pueden ajustar Parámetros y Avanzado (hay modelo y handlers).
    [[nodiscard]] bool model_params_ready() const {
        return !model_page_.problem.has_value() && on_check_model_params_ != nullptr;
    }
    /// Errores del formulario (los que no son número y los de Session) y la
    /// vista previa; se vuelve a revisar solo si el borrador cambió.
    [[nodiscard]] ModelParamsCheck model_check() const;
    [[nodiscard]] bool model_params_dirty() const {
        return model_params_ready() && model_form_.dirty();
    }
    [[nodiscard]] ftxui::Element render_search() const;
    /// Tema y fondo elegidos en el formulario.
    [[nodiscard]] Appearance chosen_appearance() const;
    [[nodiscard]] bool appearance_dirty() const;
    [[nodiscard]] bool system_prompt_dirty() const {
        return prompt_input_->text() != saved_prompt_;
    }
    /// El fondo se puede elegir (el tema elegido tiene fondo propio).
    [[nodiscard]] bool background_enabled() const;

    ModelsLoader& loader_;
    const Palette& palette_;
    OnSave on_save_;
    OnSaveAppearance on_save_appearance_;
    OnSaveSystemPrompt on_save_system_prompt_;
    OnSaveSearchKey on_save_search_key_;
    OnClose on_close_;
    OnDeleteEndpoint on_delete_endpoint_;
    OnCheckModelParams on_check_model_params_;
    OnSaveModelParams on_save_model_params_;
    ModelParamsPage model_page_;
    ModelParamsForm model_form_;
    /// El borrador que revisó Session por última vez y lo que dio (caché
    /// entre cuadros: no se revisa por cada tecla que no cambia nada).
    mutable std::optional<ModelParams> checked_draft_;
    mutable ModelParamsCheck checked_;
    std::optional<ProviderSettings> settings_;
    SearchSettings search_;
    Config base_;
    Appearance saved_appearance_; ///< El vigente al abrir (o al guardar).
    std::string saved_prompt_;    ///< Las instrucciones vigentes al abrir (o al guardar).
    std::string status_;          ///< Aviso o error bajo el formulario.
    bool confirm_discard_ = false; ///< Se preguntó "¿Descartar los cambios?".
    bool confirm_delete_ = false;  ///< Se preguntó "¿Borrar el endpoint …?".
    bool reasoning_open_ = false;  ///< La lista del razonamiento está abierta.
    bool no_color_ = false;
    std::optional<std::string> portable_dir_; ///< set_portable_dir.
    AddonsPanel addons_;

    // Lo que editan los componentes.
    std::vector<std::string> category_names_;
    int category_ = kProvider;
    std::vector<std::string> model_tab_names_;
    int model_tab_ = 0; ///< Instrucciones, Parámetros o Avanzado.
    std::vector<std::string> reasoning_names_;
    std::vector<std::string> theme_names_;
    int theme_selected_ = 0;
    std::vector<std::string> background_names_;
    int background_selected_ = 0;
    std::vector<std::string> symbol_names_; ///< En el orden de kSymbolModes.
    int symbols_selected_ = 0;
    std::vector<std::string> provider_names_;
    std::vector<std::string> provider_ids_; ///< Id de cada endpoint (atenuado); "" en los demás.
    int provider_selected_ = 0;
    bool dropdown_open_ = false;
    std::string name_text_;
    std::string url_text_;
    std::string key_text_;
    std::string filter_text_;
    std::string model_text_;
    std::string search_key_text_;
    mutable std::vector<ftxui::Box> model_row_boxes_;
    mutable ftxui::Box models_box_{0, -1, 0, -1};

    ftxui::Component categories_;
    ftxui::Component theme_list_;
    ftxui::Component background_list_;
    ftxui::Component symbol_list_;
    ftxui::Component dropdown_;
    ftxui::Component name_input_;
    ftxui::Component delete_button_;
    ftxui::Component url_input_;
    ftxui::Component key_input_;
    ftxui::Component filter_input_;
    ftxui::Component refresh_button_;
    ftxui::Component model_input_;
    /// Varias líneas con ajuste de línea (text_area.h): el texto es suyo.
    std::shared_ptr<TextArea> prompt_input_;
    ftxui::Component restore_button_;
    ftxui::Component model_tabs_;
    ftxui::Component reasoning_dropdown_;
    std::vector<ftxui::Component> number_inputs_; ///< En el orden de ModelParamsForm::numbers.
    ftxui::Component restore_all_button_;
    std::shared_ptr<TextArea> extra_input_;
    ftxui::Component search_key_input_;
    ftxui::Component save_button_;
    ftxui::Component cancel_button_;
    ftxui::Component root_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_SETTINGS_SCREEN_H
