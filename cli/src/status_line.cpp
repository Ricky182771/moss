#include "status_line.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace chatbot::cli {

namespace {

constexpr std::string_view kSeparator = "   ";
constexpr int kSeparatorWidth = 3;

int width_of(const std::string& text) { return ftxui::string_width(text); }

/// text recortado a width columnas con "…" al final, sin partir glifos ni
/// dejar espacios antes del "…". Vacío si no quedan al menos 2 columnas (un
/// "…" solo no dice nada).
std::string shortened(const std::string& text, int width) {
    if (width < 2) {
        return {};
    }
    std::string out;
    int used = 0;
    for (const std::string& glyph : ftxui::Utf8ToGlyphs(text)) {
        const int glyph_width = ftxui::string_width(glyph);
        if (used + glyph_width > width - 1) {
            break;
        }
        out += glyph;
        used += glyph_width;
    }
    out.erase(out.find_last_not_of(' ') + 1);
    return out + "…";
}

} // namespace

ftxui::Element status_line(const std::vector<StatusItem>& items, int width) {
    width = std::max(width, 1);
    // Indicadores que caben, desde el primero; el primero siempre.
    std::vector<bool> shown(items.size(), false);
    int reserved = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i].reserved) {
            continue;
        }
        const int needed = width_of(items[i].text) + (reserved > 0 ? kSeparatorWidth : 0);
        if (reserved == 0 || reserved + needed <= width) {
            shown[i] = true;
            reserved += needed;
        }
    }
    // Avisos con lo que sobra.
    std::vector<std::string> texts(items.size());
    int used = reserved;
    bool full = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].reserved) {
            texts[i] = items[i].text;
            continue;
        }
        if (full || items[i].text.empty()) {
            continue;
        }
        const int separator = used > 0 ? kSeparatorWidth : 0;
        const int room = width - used - separator;
        const int needed = width_of(items[i].text);
        if (needed <= room) {
            texts[i] = items[i].text;
        } else {
            texts[i] = shortened(items[i].text, room);
            full = true;
        }
        if (!texts[i].empty()) {
            shown[i] = true;
            used += separator + width_of(texts[i]);
        }
    }
    ftxui::Elements row;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!shown[i]) {
            continue;
        }
        if (!row.empty()) {
            row.push_back(ftxui::text(std::string{kSeparator}));
        }
        ftxui::Element part = ftxui::text(texts[i]);
        row.push_back(items[i].style ? std::move(part) | items[i].style : std::move(part));
    }
    if (row.empty()) {
        // Alto fijo: sin esto, un hbox vacío mide 0 líneas y la pantalla salta.
        row.push_back(ftxui::text(""));
    }
    return ftxui::hbox(std::move(row));
}

ftxui::Element zoned_status_line(const StatusZones& zones, int width) {
    width = std::max(width, 1);
    const auto styled = [](const StatusItem& item) {
        ftxui::Element part = ftxui::text(item.text);
        return item.style ? std::move(part) | item.style : std::move(part);
    };
    // Primero todo; luego sin hint, sin right y sin left (este último siempre se dibuja).
    struct Layout {
        bool hint;
        bool right;
        bool left;
    };
    for (const Layout layout : {Layout{true, true, true}, Layout{false, true, true},
                                Layout{false, false, true}, Layout{false, false, false}}) {
        if (layout.hint && (zones.hint.empty() || zones.activity.empty())) {
            continue;
        }
        std::vector<StatusItem> items;
        if (!zones.activity.empty()) {
            items.push_back({zones.activity + (layout.hint ? zones.hint : std::string{}),
                             ftxui::nothing, true});
        }
        items.insert(items.end(), zones.center.begin(), zones.center.end());
        int center = 0; // Ancho natural del centro.
        for (const StatusItem& item : items) {
            if (!item.text.empty()) {
                center += (center > 0 ? kSeparatorWidth : 0) + width_of(item.text);
            }
        }
        const bool left = layout.left && !zones.left.text.empty();
        const bool right = layout.right && !zones.right.text.empty();
        int total = center;
        for (const int side : {left ? width_of(zones.left.text) : 0,
                               right ? width_of(zones.right.text) : 0}) {
            if (side > 0) {
                total += (total > 0 ? kSeparatorWidth : 0) + side;
            }
        }
        if (layout.left && total > width) {
            continue;
        }
        ftxui::Elements row{ftxui::text("")}; // Alto fijo de una fila.
        if (left) {
            row.push_back(styled(zones.left));
        }
        if (layout.left && !zones.activity.empty()) {
            // Todo cabe: los avisos siguen junto a left y la actividad va centrada en el ancho,
            // o lo más cerca del centro sin tocar los avisos ni right.
            const std::string activity = items.front().text;
            const int activity_width = width_of(activity);
            int used = left ? width_of(zones.left.text) : 0;
            // Ancho natural de los avisos (el centro sin la actividad).
            const int notices =
                center - activity_width - (center > activity_width ? kSeparatorWidth : 0);
            if (notices > 0) {
                if (left) {
                    row.push_back(ftxui::text(std::string{kSeparator}));
                    used += kSeparatorWidth;
                }
                row.push_back(status_line(zones.center, notices));
                used += notices;
            }
            const int lowest = used + (used > 0 ? kSeparatorWidth : 0);
            const int highest =
                width - activity_width - (right ? width_of(zones.right.text) + kSeparatorWidth : 0);
            const int start = std::max(lowest, std::min((width - activity_width) / 2, highest));
            row.push_back(ftxui::text(std::string(static_cast<std::size_t>(start - used), ' ')));
            row.push_back(ftxui::text(activity));
        } else if (center > 0 || !layout.left) {
            if (left) {
                row.push_back(ftxui::text(std::string{kSeparator}));
            }
            // Con los lados a la vista, el centro cabe entero; si no, toma la fila.
            row.push_back(status_line(items, layout.left ? center : width));
        }
        row.push_back(ftxui::filler());
        if (right) {
            if (left || center > 0) {
                row.push_back(ftxui::text(std::string{kSeparator}));
            }
            row.push_back(styled(zones.right));
        }
        return ftxui::hbox(std::move(row));
    }
    return ftxui::text(""); // No llega: el último intento siempre dibuja.
}

} // namespace chatbot::cli
