#include "model_picker.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace chatbot::cli {

namespace {

std::string lowercase(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string_view trimmed(std::string_view text) {
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

ModelPicker::ModelPicker(ModelsConfig models_config, ModelsLoader::TransportFactory make_transport,
                         ModelsLoader::Post post)
    : models_config_(std::move(models_config)),
      loader_(std::move(make_transport), std::move(post)) {}

ModelPickerAction ModelPicker::open(std::vector<ProviderChoice> choices,
                                    const std::optional<std::string>& provider,
                                    std::string current_model) {
    close();
    choices_ = std::move(choices);
    current_model_ = std::move(current_model);
    open_ = true;
    if (provider.has_value()) {
        const auto it = std::find_if(choices_.begin(), choices_.end(),
                                     [&](const ProviderChoice& c) { return c.id == *provider; });
        if (it != choices_.end()) {
            from_providers_ = false;
            ModelPickerAction action = enter_model_step(*it);
            if (action.type == ModelPickerAction::Type::Status) {
                close();
                return action;
            }
            return {ModelPickerAction::Type::Handled, {}, {}};
        }
    }
    const auto current = std::find_if(choices_.begin(), choices_.end(),
                                      [](const ProviderChoice& c) { return c.current; });
    settle(static_cast<std::size_t>(current - choices_.begin()));
    return {ModelPickerAction::Type::Handled, {}, {}};
}

void ModelPicker::close() {
    loader_.cancel();
    open_ = false;
    model_step_ = false;
    from_providers_ = false;
    filter_.clear();
    cursor_ = 0;
    first_ = 0;
    models_state_ = ModelsState::Idle;
    models_.clear();
    models_error_.clear();
}

std::optional<std::string> ModelPicker::finish(const ModelSwitch& change) {
    using Status = ModelSwitch::Status;
    if (change.status == Status::Switched || change.status == Status::Unchanged) {
        close();
    }
    if (change.status == Status::Switched) {
        return std::nullopt; // El aviso ya está en el historial.
    }
    return change.message;
}

void ModelPicker::update(std::string_view text) {
    if (!open_ || text == filter_) {
        return;
    }
    filter_ = std::string{text};
    settle(0); // Con otro filtro, el cursor empieza arriba.
}

std::vector<ModelPickerRow> ModelPicker::rows() const {
    using Kind = ModelPickerRow::Kind;
    std::vector<ModelPickerRow> out;
    const std::string needle = lowercase(trimmed(filter_));
    if (!model_step_) {
        for (const ProviderChoice& choice : choices_) {
            if (lowercase(choice.id).find(needle) != std::string::npos ||
                lowercase(choice.name).find(needle) != std::string::npos) {
                out.push_back({Kind::Provider, choice.id, &choice, choice.current});
            }
        }
        if (out.empty()) {
            out.push_back({Kind::Notice, std::string{kNoProviders}, nullptr, false});
        }
        return out;
    }
    if (models_state_ == ModelsState::Loading) {
        out.push_back({Kind::Notice, std::string{kLoading}, nullptr, false});
    }
    bool any_model = false;
    for (const std::string& model : models_) {
        if (lowercase(model).find(needle) != std::string::npos) {
            out.push_back({Kind::Model, model, nullptr, provider_current_ && model == current_model_});
            any_model = true;
        }
    }
    if (!any_model && !needle.empty()) {
        // El filtro tal como se escribió (los ids distinguen mayúsculas).
        out.push_back({Kind::UseTyped, std::string{trimmed(filter_)}, nullptr, false});
    } else if (!any_model && models_state_ != ModelsState::Loading) {
        out.push_back({Kind::Notice, std::string{kTypeModel}, nullptr, false});
    }
    return out;
}

bool ModelPicker::has_cursor(const ModelPickerRow& row) {
    return row.kind != ModelPickerRow::Kind::Notice;
}

void ModelPicker::settle(std::size_t preferred) {
    const std::vector<ModelPickerRow> all = rows();
    cursor_ = 0;
    if (preferred < all.size() && has_cursor(all[preferred])) {
        cursor_ = preferred;
    } else {
        const auto it = std::find_if(all.begin(), all.end(), has_cursor);
        cursor_ = it != all.end() ? static_cast<std::size_t>(it - all.begin()) : 0;
    }
    if (cursor_ < first_) {
        first_ = cursor_;
    } else if (cursor_ >= first_ + kVisibleRows) {
        first_ = cursor_ + 1 - kVisibleRows;
    }
    const std::size_t max_first = all.size() > kVisibleRows ? all.size() - kVisibleRows : 0;
    first_ = std::min(first_, max_first);
}

void ModelPicker::select(std::size_t row) {
    const std::vector<ModelPickerRow> all = rows();
    if (open_ && row < all.size() && has_cursor(all[row])) {
        cursor_ = row;
        settle(row);
    }
}

ModelPickerAction ModelPicker::choose(std::size_t row) {
    const std::vector<ModelPickerRow> all = rows();
    if (!open_ || row >= all.size() || !has_cursor(all[row])) {
        return {};
    }
    settle(row);
    return handle(ListKey::Enter);
}

void ModelPicker::scroll(int delta) {
    if (!open_) {
        return;
    }
    const std::vector<ModelPickerRow> all = rows();
    std::size_t row = cursor_;
    for (int step = 0; step < std::abs(delta); ++step) {
        std::size_t next = row;
        do {
            if ((delta < 0 && next == 0) || (delta > 0 && next + 1 >= all.size())) {
                next = row; // Sin dar la vuelta.
                break;
            }
            next = delta < 0 ? next - 1 : next + 1;
        } while (!has_cursor(all[next]));
        row = next;
    }
    settle(row);
}

ModelPickerAction ModelPicker::handle(ListKey key) {
    using Type = ModelPickerAction::Type;
    if (!open_) {
        return {};
    }
    if (key == ListKey::Escape) {
        if (model_step_ && from_providers_) {
            back_to_providers();
            return {Type::ClearInput, {}, {}};
        }
        close();
        return {Type::Closed, {}, {}};
    }
    const std::vector<ModelPickerRow> all = rows();
    std::vector<std::size_t> selectable;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (has_cursor(all[i])) {
            selectable.push_back(i);
        }
    }
    if (key == ListKey::Tab) {
        return {Type::Handled, {}, {}};
    }
    if (key == ListKey::Other || key == ListKey::Delete) {
        return {};
    }
    if (selectable.empty()) {
        return {Type::Handled, {}, {}};
    }
    const auto at = std::find(selectable.begin(), selectable.end(), cursor_);
    const std::size_t pos = at != selectable.end() ? static_cast<std::size_t>(at - selectable.begin()) : 0;
    const std::size_t last = selectable.size() - 1;
    switch (key) {
    case ListKey::Up:
        settle(selectable[pos > 0 ? pos - 1 : last]);
        break;
    case ListKey::Down:
        settle(selectable[pos < last ? pos + 1 : 0]);
        break;
    case ListKey::Home:
    case ListKey::PageUp:
        settle(selectable.front());
        break;
    case ListKey::End:
    case ListKey::PageDown:
        settle(selectable.back());
        break;
    case ListKey::Enter: {
        const ModelPickerRow& row = all[selectable[pos]];
        if (row.kind == ModelPickerRow::Kind::Provider) {
            from_providers_ = true;
            return enter_model_step(*row.choice);
        }
        return {Type::Switch, provider_, row.text};
    }
    case ListKey::Tab:
    case ListKey::Escape:
    case ListKey::Delete:
    case ListKey::Other:
        break;
    }
    return {Type::Handled, {}, {}};
}

ModelPickerAction ModelPicker::enter_model_step(const ProviderChoice& choice) {
    Result<Config> config = models_config_(choice.id);
    if (config.is_error()) {
        return {ModelPickerAction::Type::Status, {}, config.error().message};
    }
    model_step_ = true;
    provider_ = choice.id;
    provider_name_ = choice.name;
    provider_current_ = choice.current;
    filter_.clear();
    models_.clear();
    models_error_.clear();
    models_state_ = ModelsState::Loading;
    first_ = 0;
    settle(0);
    // El loader es miembro: al destruirse cancela y on_done ya no corre.
    loader_.start(config.value(), [this](Result<std::vector<std::string>> result) {
        if (result.is_error()) {
            models_state_ = ModelsState::Failed;
            models_error_ = models_error_text(result.error());
            settle(0);
            return;
        }
        models_state_ = ModelsState::Loaded;
        models_ = std::move(result).value();
        // El cursor empieza en el modelo en uso, si está en la lista.
        const std::vector<ModelPickerRow> all = rows();
        const auto current =
            std::find_if(all.begin(), all.end(), [](const ModelPickerRow& r) { return r.current; });
        settle(static_cast<std::size_t>(current - all.begin()));
    });
    return {ModelPickerAction::Type::ClearInput, {}, {}};
}

void ModelPicker::back_to_providers() {
    loader_.cancel();
    const std::string previous = provider_;
    model_step_ = false;
    from_providers_ = false;
    filter_.clear();
    models_state_ = ModelsState::Idle;
    models_.clear();
    models_error_.clear();
    first_ = 0;
    const auto it = std::find_if(choices_.begin(), choices_.end(),
                                 [&](const ProviderChoice& c) { return c.id == previous; });
    settle(static_cast<std::size_t>(it - choices_.begin()));
}

} // namespace chatbot::cli
