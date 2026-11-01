#include "settings_screen.h"

#include "input_style.h"
#include "list_style.h"
#include "yes_no.h"

#include "chatbot/error.h"
#include "chatbot/markdown/markdown.h"
#include "chatbot/system_prompt.h"

#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace chatbot::cli {

namespace {

/// Ancho de la columna de etiquetas del formulario.
constexpr int kLabelWidth = 12;
/// Filas del aviso bajo la URL base (base_url_hint): caben a 100 columnas.
constexpr int kHintRows = 2;
/// Ancho de la columna de categorías ("● Configuración del modelo" y un espacio).
constexpr int kSectionsWidth = 27;
/// Etiquetas de Parámetros ("Pen. frecuencia" y un espacio).
constexpr int kParamLabelWidth = 16;
/// Campos numéricos: lo que mide el placeholder "Predeterminado".
constexpr int kNumberWidth = 14;
/// Alto del JSON avanzado: crece hasta aquí y después se desplaza.
constexpr int kExtraRows = 6;
/// Ancho del campo de la key.
constexpr int kKeyWidth = 24;
/// Alto mínimo del campo de las instrucciones (si la terminal lo permite).
constexpr int kPromptRows = 5;
/// "Símbolos: Automático / Unicode / Sencillos", en ese orden.
constexpr SymbolMode kSymbolModes[] = {SymbolMode::Auto, SymbolMode::Unicode,
                                       SymbolMode::Simple};

/// Etiqueta de un campo en su columna; con el foco, con la selección.
ftxui::Element label(std::string_view text, bool focused, const Palette& palette,
                     int width = kLabelWidth) {
    return ftxui::hbox({field_label(text, focused, palette)}) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
}

/// Texto de una línea, sin controles de terminal.
std::string clean(std::string_view text) {
    std::string out = md::sanitize(text);
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
}

ftxui::InputOption field_option(const Palette& palette, std::function<void()> on_change,
                                std::function<void()> on_enter = [] {}) {
    ftxui::InputOption option;
    option.multiline = false;
    // Sin fondo invertido, igual que la caja de la conversación.
    option.transform = [&palette](ftxui::InputState state) {
        return input_transform(std::move(state.element), state.hovered, state.focused,
                               state.is_placeholder, palette);
    };
    option.on_change = std::move(on_change);
    option.on_enter = std::move(on_enter);
    return option;
}

/// Botón "[ Guardar ]": con el foco, con la selección.
ftxui::ButtonOption button_option(const Palette& palette) {
    ftxui::ButtonOption option;
    option.transform = [&palette](const ftxui::EntryState& state) {
        return button_label(state.label, state.focused, palette);
    };
    return option;
}

/// Filas de una lista de FTXUI (Menu, Radiobox): cursor y elegido como en
/// list_style.h. elegido: state.active en un Menu, state.state en un Radiobox.
std::function<ftxui::Element(const ftxui::EntryState&)> row_transform(const Palette& palette,
                                                                      bool chosen_is_state) {
    return [&palette, chosen_is_state](const ftxui::EntryState& state) {
        return list_row(clean(state.label), state.focused,
                        chosen_is_state ? state.state : state.active, palette);
    };
}

} // namespace

SettingsScreen::SettingsScreen(ModelsLoader& loader, const Palette& palette, OnSave on_save,
                               OnSaveAppearance on_save_appearance,
                               OnSaveSystemPrompt on_save_system_prompt,
                               OnSaveSearchKey on_save_search_key, OnClose on_close)
    : loader_(loader), palette_(palette), on_save_(std::move(on_save)),
      on_save_appearance_(std::move(on_save_appearance)),
      on_save_system_prompt_(std::move(on_save_system_prompt)),
      on_save_search_key_(std::move(on_save_search_key)), on_close_(std::move(on_close)),
      addons_(
          palette, [this](std::string notice) { status_ = std::move(notice); },
          [this] { save(); }) {
    category_names_ = {"Proveedor", "Colores y Accesibilidad", "Configuración del modelo",
                       "Búsqueda web", "Addons"};
    model_tab_names_ = {"Instrucciones", "Parámetros", "Avanzado"};
    for (const std::string_view choice : kReasoningChoices) {
        reasoning_names_.emplace_back(choice);
    }
    for (const Theme& theme : themes()) {
        theme_names_.emplace_back(theme.name);
    }
    background_names_ = {"Del tema", "Transparente"};
    symbol_names_ = {"Automático", "Unicode", "Sencillos"};

    // Categorías: el cursor y la elegida se mueven juntos (Menu).
    ftxui::MenuOption categories = ftxui::MenuOption::Vertical();
    categories.entries = &category_names_;
    categories.selected = &category_;
    categories.entries_option.transform = row_transform(palette_, false);
    categories.on_change = [this] {
        if (category_ == kAddons) {
            addons_.show(); // El estado, la primera vez que se ve.
        }
    };
    categories_ = ftxui::Menu(categories);

    ftxui::RadioboxOption theme_list;
    theme_list.entries = &theme_names_;
    theme_list.selected = &theme_selected_;
    theme_list.transform = row_transform(palette_, true);
    theme_list_ = ftxui::Radiobox(theme_list);

    ftxui::RadioboxOption background_list;
    background_list.entries = &background_names_;
    background_list.selected = &background_selected_;
    background_list.transform = row_transform(palette_, true);
    background_list_ = ftxui::Radiobox(background_list);

    ftxui::RadioboxOption symbol_list;
    symbol_list.entries = &symbol_names_;
    symbol_list.selected = &symbols_selected_;
    symbol_list.transform = row_transform(palette_, true);
    symbol_list_ = ftxui::Radiobox(symbol_list);

    ftxui::DropdownOption dropdown;
    // FTXUI 7.0.3 toma el estado de checkbox.checked, no de open.
    dropdown.checkbox.checked = &dropdown_open_;
    dropdown.radiobox.entries = &provider_names_;
    dropdown.radiobox.selected = &provider_selected_;
    dropdown.radiobox.on_change = [this] {
        if (!settings_) {
            return;
        }
        loader_.cancel(); // La lista era de otro proveedor.
        settings_->select_provider(static_cast<std::size_t>(provider_selected_));
        sync_fields();
        status_.clear();
        if (settings_->is_new_endpoint()) {
            dropdown_open_ = false;
            name_input_->TakeFocus(); // "Agregar endpoint…": se empieza por el nombre.
        }
    };
    // Los endpoints, con su id atenuado.
    dropdown.radiobox.transform = [this](const ftxui::EntryState& state) {
        ftxui::Element row =
            ftxui::hbox({chosen_mark(state.state, palette_), ftxui::text(clean(state.label))});
        const auto index = static_cast<std::size_t>(state.index);
        if (state.index >= 0 && index < provider_ids_.size() && !provider_ids_[index].empty()) {
            row = ftxui::hbox({std::move(row),
                               ftxui::text("  " + provider_ids_[index]) | palette_.ink(&Theme::notice)});
        }
        return state.focused ? row | palette_.selection() : row;
    };
    const auto closed_transform = [this](const ftxui::EntryState& state) {
        const std::string arrow{
            palette_.glyph(state.state ? Glyph::DropdownOpen : Glyph::DropdownClosed)};
        ftxui::Element element = ftxui::hbox({ftxui::text("[ "), ftxui::text(state.label) | ftxui::flex,
                                              ftxui::text(" " + arrow + "]")});
        return state.focused ? element | ftxui::bold : element;
    };
    const auto open_transform = [this](bool open, ftxui::Element checkbox,
                                       ftxui::Element radiobox) {
        if (!open) {
            return checkbox;
        }
        return ftxui::vbox({std::move(checkbox),
                            palette_.vscroll(std::move(radiobox)) | ftxui::frame |
                                ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 10)});
    };
    dropdown.checkbox.transform = closed_transform;
    dropdown.transform = open_transform;
    dropdown_ = ftxui::Dropdown(dropdown);

    // Razonamiento: la misma lista desplegable que la del proveedor.
    ftxui::DropdownOption reasoning;
    reasoning.checkbox.checked = &reasoning_open_;
    reasoning.radiobox.entries = &reasoning_names_;
    reasoning.radiobox.selected = &model_form_.reasoning;
    reasoning.radiobox.transform = [this](const ftxui::EntryState& state) {
        ftxui::Element row =
            ftxui::hbox({chosen_mark(state.state, palette_), ftxui::text(state.label)});
        return state.focused ? row | palette_.selection() : row;
    };
    reasoning.checkbox.transform = closed_transform;
    reasoning.transform = open_transform;
    reasoning_dropdown_ = ftxui::Dropdown(reasoning);

    name_input_ = ftxui::Input(&name_text_, "nombre del endpoint", field_option(palette_, [this] {
                                   if (settings_) {
                                       (void)settings_->set_name(name_text_);
                                   }
                               }));
    ftxui::ButtonOption delete_option;
    // En el endpoint en uso, atenuado: al activarlo dice por qué no se borra.
    delete_option.transform = [this](const ftxui::EntryState& state) {
        ftxui::Element label = button_label(state.label, state.focused, palette_);
        return endpoint_in_use() ? label | palette_.ink(&Theme::notice) : label;
    };
    delete_button_ = ftxui::Button("Borrar endpoint", [this] { request_delete(); }, delete_option);
    url_input_ = ftxui::Input(&url_text_, "https://mi-servidor/v1",
                              field_option(
                                  palette_,
                                  [this] {
                                      if (settings_ && settings_->set_base_url(url_text_)) {
                                          loader_.cancel(); // La lista era de otra URL.
                                      }
                                  },
                                  [this] { request_models(); }));
    ftxui::InputOption key_option = field_option(
        palette_,
        [this] {
            if (settings_) {
                (void)settings_->set_key(key_text_);
            }
        },
        [this] { request_models(); });
    key_option.password = true; // Nunca se dibuja la key en claro.
    key_input_ = ftxui::Input(&key_text_, "escribe la API key", key_option);
    filter_input_ = ftxui::Input(&filter_text_, "", field_option(
                                                        palette_,
                                                        [this] {
                                                            if (settings_) {
                                                                settings_->set_filter(filter_text_);
                                                            }
                                                        },
                                                        [this] {
                                                            if (settings_ &&
                                                                settings_->pick_highlighted()) {
                                                                model_text_ = settings_->model();
                                                            }
                                                        }));
    refresh_button_ =
        ftxui::Button("Actualizar", [this] { request_models(); }, button_option(palette_));
    model_input_ = ftxui::Input(&model_text_, "escribe el modelo", field_option(palette_, [this] {
                                    if (settings_) {
                                        (void)settings_->set_model(model_text_);
                                    }
                                }));
    // Varias líneas con ajuste de línea: Enter agrega un salto de línea, como
    // antes con InputOption::multiline. Ocupa el alto que deja la sección
    // (al menos kPromptRows filas, menos si la terminal no da) y se desplaza
    // con su barra.
    TextAreaOption prompt_option;
    prompt_option.palette = &palette_;
    prompt_option.min_rows = kPromptRows;
    prompt_option.max_rows = 0;
    prompt_option.enter = EnterKey::Newline;
    prompt_option.placeholder = [](int) { return std::string{"escribe las instrucciones"}; };
    prompt_input_ = make_text_area(std::move(prompt_option));
    restore_button_ = ftxui::Button(
        "Restaurar predeterminado",
        [this] {
            prompt_input_->set_text(std::string{kDefaultSystemPrompt}, 0);
            status_.clear();
        },
        button_option(palette_));
    // Pestañas de Configuración del modelo: ←/→ o clic (Tab mueve el foco, como en todo F2).
    ftxui::MenuOption tabs = ftxui::MenuOption::Horizontal();
    tabs.entries = &model_tab_names_;
    tabs.selected = &model_tab_;
    tabs.elements_infix = [] { return ftxui::text(" "); };
    tabs.entries_option.transform = [this](const ftxui::EntryState& state) {
        ftxui::Element tab = ftxui::text(state.active ? "[" + state.label + "]"
                                                      : " " + state.label + " ");
        if (state.active) {
            tab = tab | ftxui::bold;
        } else {
            tab = tab | palette_.ink(&Theme::notice);
        }
        return state.focused ? tab | palette_.selection() : tab;
    };
    model_tabs_ = ftxui::Menu(tabs);
    for (std::string& number : model_form_.numbers) {
        number_inputs_.push_back(
            ftxui::Input(&number, "Predeterminado", field_option(palette_, [] {})));
    }
    restore_all_button_ = ftxui::Button(
        "Restaurar todo",
        [this] {
            model_form_.restore_all();
            extra_input_->set_text("", 0);
            status_.clear();
        },
        button_option(palette_));
    TextAreaOption extra_option;
    extra_option.palette = &palette_;
    extra_option.min_rows = 3;
    extra_option.max_rows = kExtraRows;
    extra_option.enter = EnterKey::Newline;
    extra_option.on_change = [this] { model_form_.extra = extra_input_->text(); };
    extra_option.placeholder = [](int columns) {
        return std::string{columns >= 30 ? R"({"chat_template_kwargs": {…}})" : "{…}"};
    };
    extra_input_ = make_text_area(std::move(extra_option));

    ftxui::InputOption search_key_option =
        field_option(palette_, [this] { (void)search_.set_key(search_key_text_); });
    search_key_option.password = true; // Nunca se dibuja la key en claro.
    search_key_input_ =
        ftxui::Input(&search_key_text_, "escribe la key de Tavily", search_key_option);
    save_button_ = ftxui::Button("Guardar", [this] { save(); }, button_option(palette_));
    cancel_button_ =
        ftxui::Button("Cancelar", [this] { (void)request_close(); }, button_option(palette_));

    const ftxui::Component provider_form = ftxui::Container::Vertical({
        dropdown_,
        ftxui::Maybe(name_input_, [this] { return settings_ && settings_->is_endpoint(); }),
        ftxui::Maybe(url_input_, [this] { return settings_ && settings_->base_url_editable(); }),
        ftxui::Maybe(key_input_, [this] { return settings_ && !settings_->key_locked(); }),
        ftxui::Maybe(delete_button_,
                     [this] {
                         return settings_ && settings_->is_endpoint() &&
                                !settings_->is_new_endpoint();
                     }),
        ftxui::Container::Horizontal({filter_input_, refresh_button_}),
        ftxui::Maybe(model_input_, [this] { return settings_ && !settings_->model_locked(); }),
    });
    const ftxui::Component appearance_form = ftxui::Container::Vertical({
        ftxui::Maybe(theme_list_, [this] { return !no_color_; }),
        // Con un tema sin fondo propio, el fondo no aplica: no toma el foco.
        ftxui::Maybe(background_list_, [this] { return background_enabled(); }),
        // Los símbolos se eligen también con NO_COLOR.
        symbol_list_,
    });
    const ftxui::Component prompt_form =
        ftxui::Container::Vertical({prompt_input_, restore_button_});
    ftxui::Components params_fields{reasoning_dropdown_};
    params_fields.insert(params_fields.end(), number_inputs_.begin(), number_inputs_.end());
    params_fields.push_back(restore_all_button_);
    const auto ready = [this] { return model_params_ready(); };
    const ftxui::Component model_form = ftxui::Container::Vertical({
        model_tabs_,
        ftxui::Container::Tab({prompt_form,
                               ftxui::Maybe(ftxui::Container::Vertical(params_fields), ready),
                               ftxui::Maybe(ftxui::Container::Vertical({extra_input_}), ready)},
                              &model_tab_),
    });
    const ftxui::Component search_form = ftxui::Container::Vertical({
        ftxui::Maybe(search_key_input_, [this] { return !search_.key_locked(); }),
    });
    const ftxui::Component form = ftxui::Container::Vertical({
        ftxui::Container::Tab(
            {provider_form, appearance_form, model_form, search_form, addons_.component()},
            &category_),
        ftxui::Container::Horizontal({save_button_, cancel_button_}),
    });
    root_ = ftxui::CatchEvent(
        ftxui::Renderer(ftxui::Container::Horizontal({categories_, form}),
                        [this] { return render(); }),
        [this](const ftxui::Event& event) { return handle_event(event); });
}

void SettingsScreen::open(ProviderSettings settings, Config base, Appearance appearance,
                          std::string system_prompt, SearchSettings search, std::string notice,
                          ModelParamsPage model) {
    settings_ = std::move(settings);
    base_ = std::move(base);
    no_color_ = no_color_enabled();
    if (no_color_) {
        const Appearance plain = resolve_appearance("", "");
        appearance.theme = plain.theme;
        appearance.background = plain.background;
    }
    saved_appearance_ = appearance;
    category_ = kProvider;
    theme_selected_ = 0;
    const std::span<const Theme> all = themes();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (&all[i] == appearance.theme) {
            theme_selected_ = static_cast<int>(i);
        }
    }
    background_selected_ = appearance.background == BackgroundMode::Transparent ? 1 : 0;
    symbols_selected_ = 0;
    for (std::size_t i = 0; i < std::size(kSymbolModes); ++i) {
        if (kSymbolModes[i] == appearance.symbol_mode) {
            symbols_selected_ = static_cast<int>(i);
        }
    }
    saved_prompt_ = system_prompt;
    // Con el cursor al principio: se ve el inicio de las instrucciones.
    prompt_input_->set_text(std::move(system_prompt), 0);
    search_ = std::move(search);
    search_key_text_.clear();
    model_page_ = std::move(model);
    model_form_ = ModelParamsForm{model_page_.saved}; // Los campos apuntan a sus textos.
    extra_input_->set_text(model_form_.extra, 0);
    checked_draft_.reset();
    model_tab_ = 0;
    reasoning_open_ = false;
    addons_.open(base_.model);
    dropdown_open_ = false;
    confirm_discard_ = false;
    confirm_delete_ = false;
    sync_providers();
    sync_fields();
    status_ = std::move(notice);
    focus();
    if (settings_->can_request_models()) {
        request_models();
    }
}

void SettingsScreen::focus() { categories_->TakeFocus(); }

PasteTarget SettingsScreen::paste_target() const {
    if (!settings_ || confirm_discard_ || confirm_delete_ || dropdown_open_ || reasoning_open_ ||
        addons_.question()) {
        return PasteTarget::None;
    }
    if (addons_.text_field_focused()) {
        return PasteTarget::SingleLine;
    }
    if (prompt_input_->Focused() || extra_input_->Focused()) {
        return PasteTarget::MultiLine;
    }
    for (const ftxui::Component& field : number_inputs_) {
        if (field->Focused()) {
            return PasteTarget::SingleLine;
        }
    }
    for (const ftxui::Component& field :
         {name_input_, url_input_, key_input_, filter_input_, model_input_, search_key_input_}) {
        if (field->Focused()) {
            return PasteTarget::SingleLine;
        }
    }
    return PasteTarget::None;
}

std::vector<std::pair<std::string_view, bool>> SettingsScreen::debug_focus() const {
    return {{"root", root_->Focused()},
            {"name", name_input_->Focused()},
            {"url", url_input_->Focused()},
            {"key", key_input_->Focused()},
            {"filter", filter_input_->Focused()},
            {"model", model_input_->Focused()},
            {"prompt", prompt_input_->Focused()},
            {"extra", extra_input_->Focused()},
            {"search_key", search_key_input_->Focused()}};
}

Appearance SettingsScreen::chosen_appearance() const {
    Appearance appearance;
    appearance.theme = &themes()[static_cast<std::size_t>(theme_selected_)];
    appearance.background =
        background_selected_ == 1 ? BackgroundMode::Transparent : BackgroundMode::FromTheme;
    appearance.symbol_mode = kSymbolModes[static_cast<std::size_t>(symbols_selected_)];
    // Quien guarda resuelve symbols (Automático depende del sistema).
    appearance.symbols = saved_appearance_.symbols;
    return appearance;
}

bool SettingsScreen::appearance_dirty() const {
    const Appearance chosen = chosen_appearance();
    if (chosen.symbol_mode != saved_appearance_.symbol_mode) {
        return true;
    }
    if (no_color_) {
        return false; // Tema y fondo no se eligen con NO_COLOR.
    }
    return chosen.theme != saved_appearance_.theme ||
           chosen.background != saved_appearance_.background;
}

bool SettingsScreen::background_enabled() const {
    return !no_color_ && has_own_background(themes()[static_cast<std::size_t>(theme_selected_)]);
}

bool SettingsScreen::request_close() {
    if (settings_ &&
        (settings_->dirty() || appearance_dirty() || system_prompt_dirty() || search_.dirty() ||
         addons_.dirty() || model_params_dirty())) {
        confirm_discard_ = true;
        return false;
    }
    close();
    return true;
}

void SettingsScreen::close() {
    loader_.cancel();
    settings_.reset();
    key_text_.clear();
    search_ = SearchSettings{};
    search_key_text_.clear();
    confirm_discard_ = false;
    confirm_delete_ = false;
    dropdown_open_ = false;
    reasoning_open_ = false;
    status_.clear();
    addons_.close();
    on_close_();
}

void SettingsScreen::sync_providers() {
    provider_names_.clear();
    provider_ids_.clear();
    for (const ProviderEntry& entry : settings_->provider_list()) {
        provider_names_.push_back(entry.name);
        provider_ids_.push_back(entry.endpoint ? std::string{*endpoint_id(entry.id)} : std::string{});
    }
    provider_selected_ = static_cast<int>(settings_->provider());
}

bool SettingsScreen::endpoint_in_use() const {
    return settings_ && settings_->is_endpoint() && !settings_->is_new_endpoint() &&
           settings_->provider() == settings_->saved_provider();
}

void SettingsScreen::request_delete() {
    if (!settings_ || !settings_->is_endpoint() || settings_->is_new_endpoint() ||
        !on_delete_endpoint_) {
        return;
    }
    if (endpoint_in_use()) {
        delete_endpoint(); // Session no lo borra y dice por qué.
        return;
    }
    confirm_delete_ = true;
}

void SettingsScreen::delete_endpoint() {
    confirm_delete_ = false;
    EndpointDeleted result = on_delete_endpoint_(settings_->provider_id());
    status_ = std::move(result.message);
    if (result.form.has_value()) {
        loader_.cancel(); // La lista era del endpoint borrado.
        settings_ = std::move(result.form);
        sync_providers();
        sync_fields();
        dropdown_->TakeFocus();
    }
}

void SettingsScreen::sync_fields() {
    name_text_ = settings_->name();
    url_text_ = settings_->base_url();
    key_text_ = settings_->key();
    filter_text_ = settings_->filter();
    model_text_ = settings_->model();
}

void SettingsScreen::request_models() {
    if (!settings_) {
        return;
    }
    if (!settings_->can_request_models()) {
        status_ = std::string{kModelsNeedUrlAndKey};
        return;
    }
    status_.clear();
    settings_->models_loading();
    loader_.start(settings_->request_config(base_),
                  [this](const Result<std::vector<std::string>>& result) {
                      if (!settings_) {
                          return;
                      }
                      if (result.is_ok()) {
                          settings_->models_loaded(result.value());
                      } else {
                          settings_->models_failed(models_error_text(result.error()));
                      }
                  });
}

void SettingsScreen::save() {
    if (!settings_) {
        return;
    }
    const bool appearance_changed = appearance_dirty();
    const bool prompt_changed = system_prompt_dirty();
    const bool search_changed = search_.dirty();
    const bool addons_changed = addons_.pending();
    const bool params_changed = model_params_dirty();
    // Las instrucciones, la key de búsqueda y los addons se revisan antes de
    // guardar nada: un error no deja guardada solo una parte.
    if (prompt_changed) {
        if (const std::optional<std::string> error =
                validate_system_prompt(prompt_input_->text())) {
            status_ = *error;
            return;
        }
    }
    if (const std::optional<std::string> error = search_.validate()) {
        status_ = *error;
        return;
    }
    if (const std::optional<std::string> error = addons_.check()) {
        status_ = *error;
        return;
    }
    std::optional<ModelParams> params;
    if (params_changed) {
        if (!model_check().problems.empty()) {
            status_ = "Revisa los ajustes marcados en Configuración del modelo.";
            return;
        }
        params = model_form_.draft();
    }
    if (addons_.ask_before_saving()) {
        return; // "Se descartarán: …": al contestar que sí, se vuelve a guardar.
    }
    // Si solo cambió la apariencia, las instrucciones o la key de búsqueda,
    // el proveedor no se guarda (ni se reconstruye el cliente); si no cambió
    // nada, se guarda como siempre.
    const bool provider_changed =
        settings_->dirty() || (!appearance_changed && !prompt_changed && !search_changed &&
                               !addons_changed && !params_changed);
    if (provider_changed) {
        if (const std::optional<std::string> error = settings_->validate()) {
            status_ = *error; // Antes de escribir nada.
            return;
        }
    }
    // Los ajustes antes que el proveedor: guardar el proveedor puede cambiar
    // de modelo, y estos son del modelo con que se abrió F2.
    if (params.has_value()) {
        if (const std::optional<std::string> error = on_save_model_params_(*params)) {
            status_ = *error;
            return;
        }
        model_form_.mark_saved();
    }
    if (provider_changed) {
        if (const std::optional<std::string> error = on_save_(*settings_)) {
            status_ = *error;
            return;
        }
        settings_->mark_saved();
        sync_providers(); // Un endpoint nuevo ya está en la lista.
    }
    if (appearance_changed) {
        const Appearance chosen = chosen_appearance();
        if (const std::optional<std::string> error = on_save_appearance_(chosen)) {
            status_ = *error;
            return;
        }
        saved_appearance_ = chosen;
    }
    if (prompt_changed) {
        if (const std::optional<std::string> error =
                on_save_system_prompt_(system_prompt_to_store(prompt_input_->text()))) {
            status_ = *error;
            return;
        }
        saved_prompt_ = prompt_input_->text();
    }
    if (const auto update = search_.credential_update()) {
        if (const std::optional<std::string> error = on_save_search_key_(update->second)) {
            status_ = *error;
            return;
        }
        search_.mark_saved();
        search_key_text_.clear();
    }
    if (addons_changed) {
        if (const std::optional<std::string> error = addons_.commit()) {
            status_ = *error;
            return;
        }
        // Se queda abierta: las preguntas y el resultado del servicio salen
        // en la línea de estado.
        return;
    }
    close();
}

void SettingsScreen::move_focus(int step) {
    // Las categorías, los campos de la categoría elegida y los botones.
    std::vector<ftxui::Component> order{categories_};
    if (category_ == kAppearance) {
        if (!no_color_) {
            order.push_back(theme_list_);
        }
        if (background_enabled()) {
            order.push_back(background_list_);
        }
        order.push_back(symbol_list_);
    } else if (category_ == kModel) {
        order.push_back(model_tabs_);
        if (model_tab_ == 0) {
            order.push_back(prompt_input_);
            order.push_back(restore_button_);
        } else if (model_params_ready() && model_tab_ == 1) {
            order.push_back(reasoning_dropdown_);
            order.insert(order.end(), number_inputs_.begin(), number_inputs_.end());
            order.push_back(restore_all_button_);
        } else if (model_params_ready()) {
            order.push_back(extra_input_);
        }
    } else if (category_ == kSearch) {
        if (!search_.key_locked()) {
            order.push_back(search_key_input_);
        }
    } else if (category_ == kAddons) {
        for (const ftxui::Component& component : addons_.focus_order()) {
            order.push_back(component);
        }
    } else {
        order.push_back(dropdown_);
        if (settings_->is_endpoint()) {
            order.push_back(name_input_);
        }
        if (settings_->base_url_editable()) {
            order.push_back(url_input_);
        }
        if (!settings_->key_locked()) {
            order.push_back(key_input_);
        }
        if (settings_->is_endpoint() && !settings_->is_new_endpoint()) {
            order.push_back(delete_button_);
        }
        order.push_back(filter_input_);
        order.push_back(refresh_button_);
        if (!settings_->model_locked()) {
            order.push_back(model_input_);
        }
    }
    order.push_back(save_button_);
    order.push_back(cancel_button_);
    const auto count = static_cast<long long>(order.size());
    long long current = 0;
    for (long long i = 0; i < count; ++i) {
        if (order[static_cast<std::size_t>(i)]->Focused()) {
            current = i;
            break;
        }
    }
    current = ((current + step) % count + count) % count;
    order[static_cast<std::size_t>(current)]->TakeFocus();
}

bool SettingsScreen::handle_event(const ftxui::Event& event) {
    if (!settings_) {
        return false;
    }
    if (asking()) {
        // La regla de yes_no.h: también la del Event::Custom que llega tras
        // cada post (el mismo que trae la pregunta del servicio o la lista
        // de modelos).
        const YesNo reply = yes_no_answer(event == ftxui::Event::Escape,
                                          event.is_character() ? event.character() : std::string{});
        if (reply == YesNo::Ignore) {
            return true;
        }
        if (addons_.question()) {
            if (reply == YesNo::Cancel) {
                addons_.cancel_question();
            } else {
                addons_.answer(reply == YesNo::Yes);
            }
        } else if (confirm_delete_) {
            if (reply == YesNo::Yes) {
                delete_endpoint();
            } else {
                confirm_delete_ = false;
                if (reply == YesNo::Cancel) {
                    status_ = "Cancelado: no se borró el endpoint.";
                }
            }
        } else if (reply == YesNo::Yes) {
            close();
        } else {
            confirm_discard_ = false;
            if (reply == YesNo::Cancel) {
                status_ = std::string{kDiscardCancelled};
            }
        }
        return true;
    }
    if (dropdown_open_ || reasoning_open_) {
        if (event != ftxui::Event::Tab && event != ftxui::Event::TabReverse) {
            return false; // ↑/↓, Enter y Esc son de la lista abierta.
        }
        dropdown_open_ = false;
        reasoning_open_ = false;
    }
    if (event == ftxui::Event::Escape) {
        (void)request_close();
        return true;
    }
    if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
        move_focus(event == ftxui::Event::Tab ? 1 : -1);
        return true;
    }
    if (category_ == kAddons && addons_.handle_event(event)) {
        return true;
    }
    if (category_ == kProvider && event.is_mouse() &&
        settings_->models_state() == ModelsState::Loaded && !model_row_boxes_.empty()) {
        auto mouse_event = event;
        const auto& mouse = mouse_event.mouse();
        if (models_box_.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) {
                filter_input_->TakeFocus();
                settings_->move_highlight(mouse.button == ftxui::Mouse::WheelUp ? -1 : 1);
                return true;
            }
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                for (std::size_t i = 0; i < model_row_boxes_.size(); ++i) {
                    if (model_row_boxes_[i].Contain(mouse.x, mouse.y)) {
                        filter_input_->TakeFocus();
                        settings_->move_highlight(static_cast<int>(i) -
                                                   static_cast<int>(settings_->highlighted()));
                        if (settings_->pick_highlighted()) {
                            model_text_ = settings_->model();
                        }
                        return true;
                    }
                }
            }
        }
    }
    if (category_ == kProvider && filter_input_->Focused()) {
        // Con el filtro, ↑/↓ (y PgUp/PgDn) mueven el resaltado de la lista.
        const std::pair<const ftxui::Event*, int> moves[] = {
            {&ftxui::Event::ArrowUp, -1},
            {&ftxui::Event::ArrowDown, 1},
            {&ftxui::Event::PageUp, -10},
            {&ftxui::Event::PageDown, 10},
        };
        for (const auto& [move, delta] : moves) {
            if (event == *move) {
                settings_->move_highlight(delta);
                return true;
            }
        }
    }
    return false;
}

ftxui::Element SettingsScreen::render_models() const {
    model_row_boxes_.clear();
    const ProviderSettings& s = *settings_;
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    switch (s.models_state()) {
    case ModelsState::Idle:
        return ftxui::text(s.can_request_models()
                               ? "Pulsa Actualizar para pedir la lista."
                               : "Escribe la URL base y la API key, y pulsa Actualizar.") |
               notice;
    case ModelsState::Loading:
        return ftxui::text("Cargando modelos…");
    case ModelsState::Failed:
        return ftxui::vbox({
            ftxui::paragraph(clean(s.models_error())) | ftxui::bold,
            ftxui::text(s.model_locked() ? "El modelo lo define CHAT_MODEL."
                                         : "Puedes escribir el modelo a mano en «Modelo».") |
                notice,
        });
    case ModelsState::Loaded:
        break;
    }
    const std::vector<std::string> models = s.filtered_models();
    if (models.empty()) {
        return ftxui::text(s.filter().empty() ? "El servidor no devolvió modelos."
                                              : "Ningún modelo coincide con el filtro.") |
               notice;
    }
    std::vector<std::string> labels;
    std::optional<std::size_t> chosen;
    labels.reserve(models.size());
    for (std::size_t i = 0; i < models.size(); ++i) {
        labels.push_back(clean(models[i]));
        if (models[i] == s.effective_model()) {
            chosen = i;
        }
    }
    // La lista tiene el foco mientras lo tiene el filtro: ↑/↓ mueven el cursor.
    return choice_list(labels, s.highlighted(), chosen, filter_input_->Focused(), palette_,
                        &model_row_boxes_);
}

ftxui::Element SettingsScreen::render_provider() const {
    const ProviderSettings& s = *settings_;
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);

    ftxui::Element url;
    if (s.base_url_editable()) {
        url = url_input_->Render() | ftxui::flex;
    } else {
        url = ftxui::hbox({
            ftxui::text(clean(s.effective_base_url())) | ftxui::flex_shrink,
            s.base_url_locked() ? ftxui::text("  (definido por CHAT_BASE_URL)") | notice
                                : ftxui::emptyElement(),
        });
    }

    // Aviso de URL sin ruta: en un endpoint sus kHintRows filas están siempre,
    // vacías si no hay aviso, para que escribir la URL no mueva los campos de
    // abajo. En pantallas angostas lo que no cabe se recorta.
    ftxui::Element url_hint = ftxui::emptyElement();
    if (s.is_endpoint()) {
        const std::optional<std::string> hint = s.base_url_hint();
        url_hint = ftxui::hbox({
                       ftxui::text("") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kLabelWidth),
                       ftxui::paragraph(hint.value_or("")) | notice | ftxui::flex,
                   }) |
                   ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kHintRows);
    }

    // La key nunca se dibuja: el campo muestra "•" y el estado, enmascarado.
    ftxui::Element key;
    if (s.key_locked()) {
        key = ftxui::text(s.key_status()) | notice;
    } else {
        key = ftxui::hbox({
            key_input_->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kKeyWidth),
            ftxui::text("  " + s.key_status()) | notice,
        });
    }

    ftxui::Element model;
    if (s.model_locked()) {
        model = ftxui::hbox({ftxui::text(clean(s.effective_model())),
                             ftxui::text("  (definido por CHAT_MODEL)") | notice});
    } else {
        model = model_input_->Render() | ftxui::flex;
    }

    return ftxui::vbox({
        ftxui::text("Proveedor de IA") | ftxui::bold,
        ftxui::text(""),
        ftxui::hbox({label("Proveedor", dropdown_->Focused(), palette_),
                     dropdown_->Render() | ftxui::flex}),
        s.is_endpoint() ? ftxui::hbox({label("Nombre", name_input_->Focused(), palette_),
                                       name_input_->Render() | ftxui::flex})
                        : ftxui::emptyElement(),
        ftxui::hbox({label("URL base", url_input_->Focused(), palette_), std::move(url)}),
        std::move(url_hint),
        ftxui::hbox({label("API key", key_input_->Focused(), palette_), std::move(key)}),
        // En un endpoint guardado, el botón va en la fila vacía: la página no crece.
        s.is_endpoint() && !s.is_new_endpoint()
            ? ftxui::hbox({label("", false, palette_), delete_button_->Render()})
            : ftxui::text(""),
        ftxui::hbox({label("Modelos", false, palette_),
                     field_label("Filtrar:", filter_input_->Focused(), palette_),
                     ftxui::text(" "), filter_input_->Render() | ftxui::flex, ftxui::text(" "),
                     refresh_button_->Render()}),
        render_models() | ftxui::flex | ftxui::reflect(models_box_),
        ftxui::hbox({label("Modelo", model_input_->Focused(), palette_), std::move(model)}),
        portable_dir_.has_value()
            ? ftxui::vbox({ftxui::text(""),
                           ftxui::text("Modo portable: datos en " + clean(*portable_dir_)) |
                               notice})
            : ftxui::emptyElement(),
    });
}

ftxui::Element SettingsScreen::render_symbols() const {
    return ftxui::hbox({
        label("Símbolos", symbol_list_->Focused(), palette_),
        ftxui::vbox({
            symbol_list_->Render(),
            ftxui::paragraph("Automático: Sencillos solo en la consola de Windows 10 "
                             "(sin Windows Terminal).") |
                palette_.ink(&Theme::notice),
        }) | ftxui::flex,
    });
}

ftxui::Element SettingsScreen::render_appearance() const {
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    if (no_color_) {
        return ftxui::vbox({
            ftxui::text("Colores y Accesibilidad") | ftxui::bold,
            ftxui::text(""),
            ftxui::hbox({label("Tema", false, palette_), ftxui::text("De la terminal") | notice}),
            ftxui::hbox({label("Fondo", false, palette_), ftxui::text("Transparente") | notice}),
            ftxui::text("Desactivado por NO_COLOR") | notice,
            ftxui::text(""),
            render_symbols(),
            ftxui::text(""),
            ftxui::text("Se aplica al guardar.") | notice,
            ftxui::filler(),
        });
    }
    ftxui::Element background;
    if (background_enabled()) {
        background = background_list_->Render();
    } else {
        // No aplica: las opciones, sin cursor ni elegido, en texto secundario.
        ftxui::Elements rows;
        for (const std::string& name : background_names_) {
            rows.push_back(ftxui::text(std::string{kNotChosenMark} + name) | notice);
        }
        rows.push_back(ftxui::text("No aplica: este tema usa el fondo de la terminal.") | notice);
        background = ftxui::vbox(std::move(rows));
    }
    return ftxui::vbox({
        ftxui::text("Colores y Accesibilidad") | ftxui::bold,
        ftxui::text(""),
        ftxui::hbox({label("Tema", theme_list_->Focused(), palette_),
                     theme_list_->Render() | ftxui::flex}),
        ftxui::text(""),
        ftxui::hbox({label("Fondo", background_list_->Focused(), palette_),
                     std::move(background) | ftxui::flex}),
        ftxui::text(""),
        render_symbols(),
        ftxui::text(""),
        ftxui::text("Se aplica al guardar.") | notice,
        ftxui::filler(),
    });
}

ftxui::Element SettingsScreen::render_system_prompt() const {
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    const std::string& prompt = prompt_input_->text();
    const std::size_t bytes = prompt.size();
    ftxui::Element counter = ftxui::text(std::to_string(bytes) + " / " +
                                         std::to_string(kMaxSystemPromptBytes) + " bytes");
    counter = bytes > kMaxSystemPromptBytes ? counter | ftxui::bold : counter | notice;

    // Lo que impediría guardar o merece un aviso, sin esperar a Guardar (el
    // campo dibuja un control como U+FFFD, pero no dice por qué no se
    // guardaría). Un error que ya está en la línea de estado no se repite.
    ftxui::Elements checks;
    if (const std::optional<std::string> error = validate_system_prompt(prompt);
        error.has_value() && *error != status_) {
        checks.push_back(ftxui::paragraph(clean(*error)) | ftxui::bold);
    }
    if (const std::optional<std::string> warning =
            system_prompt_limit_warning(prompt, base_.history_limit_bytes)) {
        checks.push_back(ftxui::paragraph(clean(*warning)) | notice);
    }

    return ftxui::vbox({
        // El campo ocupa el alto que sobra (flex) y se puede encoger: los
        // botones de abajo nunca quedan fuera de la pantalla.
        ftxui::hbox({label("Texto", prompt_input_->Focused(), palette_),
                     prompt_input_->Render()}) |
            ftxui::flex,
        ftxui::hbox({label("", false, palette_), std::move(counter)}),
        ftxui::hbox({label("", false, palette_),
                     ftxui::text("Vacío: sin instrucciones de sistema") | notice}),
        ftxui::hbox({label("", false, palette_), ftxui::text("Para todos los modelos") | notice}),
        ftxui::hbox({label("", false, palette_), ftxui::vbox(std::move(checks)) | ftxui::flex}),
        ftxui::hbox({label("", false, palette_), restore_button_->Render()}),
    });
}

ModelParamsCheck SettingsScreen::model_check() const {
    std::vector<FieldProblem> parse_problems;
    const ModelParams draft = model_form_.draft(&parse_problems);
    if (!checked_draft_.has_value() || *checked_draft_ != draft) {
        // Solo cuando cambió el borrador: el JSON avanzado no se vuelve a
        // leer en cada cuadro.
        checked_ = on_check_model_params_(draft);
        checked_draft_ = draft;
    }
    if (parse_problems.empty()) {
        return checked_;
    }
    // Lo que no es número va primero; Session no lo vio (quedó en Predeterminado).
    ModelParamsCheck check{std::move(parse_problems), std::nullopt};
    check.problems.insert(check.problems.end(), checked_.problems.begin(), checked_.problems.end());
    return check;
}

ftxui::Element SettingsScreen::render_model() const {
    std::string title = "Configuración del modelo";
    if (!model_page_.problem.has_value()) {
        title += " — " + clean(model_page_.model) + " (" + clean(model_page_.provider_label) + ")";
    }
    ftxui::Element page;
    if (model_tab_ == 0) {
        page = render_system_prompt();
    } else if (!model_params_ready()) {
        page = ftxui::paragraph(clean(model_page_.problem.value_or(
                   std::string{kNoModelForParams}))) |
               palette_.ink(&Theme::notice);
    } else if (model_tab_ == 1) {
        page = render_params();
    } else {
        page = render_advanced();
    }
    return ftxui::vbox({
        ftxui::text(title) | ftxui::bold,
        // Angosta: la barra se desplaza para que la pestaña elegida se vea.
        model_tabs_->Render() | ftxui::xframe,
        std::move(page) | ftxui::flex,
    });
}

ftxui::Element SettingsScreen::render_params() const {
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    const ModelParamsCheck check = model_check();
    const auto problem_of = [&check](std::string_view field) -> const std::string* {
        for (const FieldProblem& problem : check.problems) {
            if (problem.field == field) {
                return &problem.message;
            }
        }
        return nullptr;
    };
    const auto row = [this](std::string_view name, bool focused, ftxui::Element content) {
        return ftxui::hbox({label(name, focused, palette_, kParamLabelWidth), std::move(content)});
    };
    ftxui::Elements rows{
        row("Razonamiento", reasoning_dropdown_->Focused(),
            reasoning_dropdown_->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 20)),
        row("", false, ftxui::paragraph(reasoning_help(model_page_.provider)) | notice | ftxui::flex),
    };
    const std::vector<ParamLimit> limits = model_param_limits(model_page_.provider);
    for (std::size_t i = 0; i < limits.size() && i < number_inputs_.size(); ++i) {
        const ftxui::Component& input = number_inputs_[i];
        ftxui::Element hint = ftxui::text(limit_hint(limits[i])) | notice;
        if (const std::string* problem = problem_of(limits[i].field)) {
            hint = ftxui::paragraph(clean(*problem)) | ftxui::bold | ftxui::flex;
        }
        rows.push_back(row(param_label(limits[i].field), input->Focused(),
                           ftxui::hbox({input->Render() |
                                            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kNumberWidth),
                                        ftxui::text("  "), std::move(hint) | ftxui::flex})));
        if (limits[i].field == "top_k") {
            rows.push_back(row("", false,
                               ftxui::paragraph("No todos los proveedores lo aceptan.") | notice |
                                   ftxui::flex));
        }
    }
    rows.push_back(row("", false, restore_all_button_->Render()));
    return ftxui::vbox(std::move(rows));
}

ftxui::Element SettingsScreen::render_advanced() const {
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    const ModelParamsCheck check = model_check();
    ftxui::Element below = ftxui::paragraph("Se mezcla al final del cuerpo (JSON Merge Patch): "
                                            "gana sobre el formulario y null quita una llave.") |
                           notice;
    for (const FieldProblem& problem : check.problems) {
        if (problem.field == "extra") {
            below = ftxui::paragraph(clean(problem.message)) | ftxui::bold;
        }
    }
    ftxui::Element preview;
    if (check.preview.has_value()) {
        ftxui::Elements lines;
        std::string_view rest = *check.preview;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            lines.push_back(ftxui::text(clean(rest.substr(0, end))));
            rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        }
        preview = ftxui::vbox(std::move(lines)) | ftxui::frame;
    } else {
        preview = ftxui::text("Corrige los errores para ver el cuerpo.") | notice;
    }
    return ftxui::vbox({
        ftxui::hbox({label("JSON", extra_input_->Focused(), palette_), extra_input_->Render() | ftxui::flex}),
        ftxui::hbox({label("", false, palette_), std::move(below) | ftxui::flex}),
        ftxui::text("Vista previa (lo que se manda, sin la conversación)") | ftxui::bold,
        std::move(preview) | ftxui::flex,
    });
}

ftxui::Element SettingsScreen::render_search() const {
    const ftxui::Decorator notice = palette_.ink(&Theme::notice);
    // La key nunca se dibuja: el campo muestra "•" y el estado, enmascarado.
    ftxui::Element key;
    if (search_.key_locked()) {
        key = ftxui::text(search_.key_status()) | notice;
    } else {
        key = ftxui::hbox({
            search_key_input_->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kKeyWidth),
            ftxui::text("  " + search_.key_status()) | notice,
        });
    }
    return ftxui::vbox({
        ftxui::text("Búsqueda web") | ftxui::bold,
        ftxui::text(""),
        ftxui::hbox({label("API key", search_key_input_->Focused(), palette_), std::move(key)}),
        ftxui::text(""),
        ftxui::hbox({label("", false, palette_),
                     ftxui::paragraph("Tavily: 1,000 búsquedas gratis al mes. Solo se envía la "
                                      "consulta escrita después de /buscar, nunca la "
                                      "conversación.") |
                         notice | ftxui::flex}),
        ftxui::filler(),
    });
}

ftxui::Element SettingsScreen::render() const {
    if (!settings_) {
        return ftxui::emptyElement();
    }
    ftxui::Element status;
    if (addons_.question()) {
        status = ftxui::paragraph(addons_.question()->text) | ftxui::bold;
    } else if (confirm_discard_) {
        status = ftxui::text("¿Descartar los cambios? (s/n)") | ftxui::bold;
    } else if (confirm_delete_) {
        const std::string name = settings_->provider_list()[settings_->provider()].name;
        status = ftxui::paragraph("¿Borrar el endpoint " + clean(name) + "? (s/n)") |
                 ftxui::bold;
    } else if (!status_.empty()) {
        status = ftxui::paragraph(clean(status_)) | ftxui::bold;
    } else if (category_ == kAppearance) {
        status = ftxui::text("Tab: campo · ↑/↓: mover · Enter: elegir · Esc: cerrar") |
                 palette_.ink(&Theme::notice);
    } else if (category_ == kModel && model_tabs_->Focused()) {
        status = ftxui::text("Flechas: pestaña · Tab: campo · Esc: cerrar") |
                 palette_.ink(&Theme::notice);
    } else if (category_ == kModel && model_tab_ == 1) {
        status = ftxui::text("Tab: campo · Vacío: predeterminado · Esc: cerrar") |
                 palette_.ink(&Theme::notice);
    } else if (category_ == kModel) {
        status = ftxui::text("Tab: campo · Enter: nueva línea · Esc: cerrar") |
                 palette_.ink(&Theme::notice);
    } else if (category_ == kSearch || category_ == kAddons) {
        status = ftxui::text("Tab: campo · Esc: cerrar") | palette_.ink(&Theme::notice);
    } else {
        status = ftxui::text("Tab: campo · ↑/↓ y Enter: modelo · Esc: cerrar") |
                 palette_.ink(&Theme::notice);
    }

    ftxui::Element page;
    switch (category_) {
    case kAppearance:
        page = render_appearance();
        break;
    case kModel:
        page = render_model();
        break;
    case kSearch:
        page = render_search();
        break;
    case kAddons:
        page = addons_.render();
        break;
    default:
        page = render_provider();
        break;
    }
    ftxui::Element form = ftxui::vbox({
        std::move(page) | ftxui::flex,
        ftxui::text(""),
        ftxui::hbox({ftxui::filler(), save_button_->Render(), ftxui::text("  "),
                     cancel_button_->Render()}),
        std::move(status),
    });
    ftxui::Element sections =
        categories_->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kSectionsWidth);
    // El borde y los separadores en el color border; lo de adentro, no.
    ftxui::Element body = ftxui::hbox({std::move(sections),
                                       ftxui::separator() | palette_.ink(&Theme::border),
                                       ftxui::text(" "), std::move(form) | ftxui::flex}) |
                          palette_.inside_border();
    return ftxui::window(ftxui::text(" Configuración ") | ftxui::bold | palette_.inside_border(),
                         std::move(body), ftxui::LIGHT) |
           palette_.ink(&Theme::border) | ftxui::flex;
}

} // namespace chatbot::cli
