#include "model_picker_view.h"

#include "command_menu_view.h"
#include "chatbot/providers.h"
#include "sidebar_view.h"

#include "chatbot/markdown/markdown.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace chatbot::cli {

namespace {

constexpr std::string_view kIndent = "  ";
constexpr std::string_view kGap = "  ";

std::string spaces(int count) {
    return std::string(static_cast<std::size_t>(std::max(count, 0)), ' ');
}

std::string padded(std::string_view text, int width) {
    std::string out = fit_width(text, width);
    return out + spaces(width - ftxui::string_width(out));
}

/// Una línea sin controles de terminal (los ids vienen del servidor).
std::string clean(std::string_view text) {
    std::string out = md::sanitize(text);
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
}

} // namespace

std::string_view provider_state_text(const ProviderChoice& choice) {
    if (!choice.selectable) {
        return choice.note;
    }
    if (choice.key_saved) {
        return "key guardada";
    }
    return choice.local ? std::string_view{"local"} : std::string_view{};
}

ftxui::Element render_model_picker(const ModelPicker& picker, int width, const Palette& palette,
                                   std::vector<ftxui::Box>* row_boxes) {
    using Kind = ModelPickerRow::Kind;
    width = std::max(width, 1);
    const int indent = ftxui::string_width(kIndent);
    const int gap = ftxui::string_width(kGap);
    const std::string mark = std::string{palette.glyph(Glyph::ChosenMark)} + " ";
    const int mark_width = ftxui::string_width(mark);

    // Proveedores: id y nombre alineados al más largo de la tabla.
    int id_width = 0;
    int left_width = 0;
    for (const ProviderInfo& info : providers()) {
        id_width = std::max(id_width, ftxui::string_width(info.id));
    }
    for (const ProviderInfo& info : providers()) {
        left_width = std::max(left_width, id_width + gap + ftxui::string_width(info.name));
    }
    const int row_width = width - indent - mark_width;
    const bool show_state = row_width - left_width - gap >= kMenuMinDescription;
    const int provider_left = show_state ? left_width : row_width;
    const int provider_state = show_state ? row_width - left_width - gap : 0;

    ftxui::Elements lines;
    lines.push_back(ftxui::separator() | palette.ink(&Theme::border));
    const std::string header =
        picker.model_step()
            ? "Modelos de " + clean(picker.provider_name()) +
                  (picker.can_go_back() ? " · Esc regresa" : " · Esc cierra")
            : std::string{"Elige un proveedor · Esc cierra"};
    lines.push_back(ftxui::text(padded(std::string{kIndent} + header, width)) |
                    palette.ink(&Theme::notice));

    const std::vector<ModelPickerRow> rows = picker.rows();
    const std::size_t first = std::min(picker.first(), rows.size());
    const std::size_t end = std::min(rows.size(), first + ModelPicker::kVisibleRows);
    if (row_boxes != nullptr) {
        row_boxes->assign(end - first, ftxui::Box{0, -1, 0, -1});
    }
    for (std::size_t row = first; row < end; ++row) {
        const ModelPickerRow& item = rows[row];
        std::string line{kIndent};
        bool dim = item.kind == Kind::Notice;
        if (item.kind == Kind::Notice) {
            line += padded(item.text, width - indent);
        } else {
            line += item.current ? mark : spaces(mark_width);
            if (item.kind == Kind::Provider) {
                std::string left = padded(clean(item.choice->id), id_width) +
                                   std::string{kGap} + clean(item.choice->name);
                line += padded(left, provider_left);
                if (show_state) {
                    line += std::string{kGap} +
                            padded(provider_state_text(*item.choice), provider_state);
                }
                dim = !item.choice->selectable;
            } else if (item.kind == Kind::Model) {
                line += padded(clean(item.text), row_width);
            } else {
                line += padded("Usar «" + clean(item.text) + "»", row_width);
            }
        }
        line += spaces(width - ftxui::string_width(line));
        ftxui::Element element = ftxui::text(fit_width(line, width));
        if (dim) {
            element = element | palette.ink(&Theme::notice);
        }
        if (row == picker.cursor() && item.kind != Kind::Notice) {
            element = element | palette.selection();
        }
        if (row_boxes != nullptr) {
            element = element | ftxui::reflect((*row_boxes)[row - first]);
        }
        lines.push_back(std::move(element));
    }
    return ftxui::vbox(std::move(lines));
}

} // namespace chatbot::cli
