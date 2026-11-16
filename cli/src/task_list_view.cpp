#include "task_list_view.h"

#include "sidebar_view.h" // fit_width

#include "chatbot/markdown/markdown.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatbot::cli {

namespace {

/// Una tarea ya medida para una fila.
struct Piece {
    const TaskItem* task = nullptr;
    std::string mark;
    std::string text; ///< Filtrado, en una línea y, si no cabía, recortado.
    int width = 0;    ///< Columnas de mark + " " + text.
};

constexpr std::string_view kGapText = "  "; ///< Entre dos tareas de la misma fila.
constexpr int kGap = 2;                      ///< Sus columnas.

Piece piece_of(const TaskItem& task, int width, const Palette& palette) {
    Glyph mark = Glyph::TaskOpen;
    std::string_view text = task.content;
    if (task.state == TaskState::Done) {
        mark = Glyph::TaskDone;
    } else if (task.state == TaskState::Active) {
        mark = Glyph::TaskActive;
        if (!task.active_form.empty()) {
            text = task.active_form;
        }
    }
    Piece piece{&task, std::string{palette.glyph(mark)}, md::sanitize(text), 0};
    std::replace(piece.text.begin(), piece.text.end(), '\n', ' ');
    const int mark_width = ftxui::string_width(piece.mark) + 1;
    piece.text = fit_width(piece.text, width - mark_width);
    piece.width = mark_width + ftxui::string_width(piece.text);
    return piece;
}

int row_width(const std::vector<Piece>& row) {
    int used = 0;
    for (const Piece& piece : row) {
        used += (used > 0 ? kGap : 0) + piece.width;
    }
    return used;
}

} // namespace

ftxui::Element render_task_list(const std::vector<TaskItem>& tasks, int width,
                                const Palette& palette) {
    width = std::max(width, 1);
    std::vector<std::vector<Piece>> rows;
    for (const TaskItem& task : tasks) {
        Piece piece = piece_of(task, width, palette);
        if (rows.empty() || row_width(rows.back()) + kGap + piece.width > width) {
            rows.emplace_back();
        }
        rows.back().push_back(std::move(piece));
    }
    std::size_t hidden = 0;
    std::string more;
    const auto max_rows = static_cast<std::size_t>(kTaskListMaxRows);
    if (rows.size() > max_rows) {
        for (std::size_t r = max_rows; r < rows.size(); ++r) {
            hidden += rows[r].size();
        }
        rows.resize(max_rows);
        // "… N más" al final de la última fila: salen tareas hasta que quepa.
        std::vector<Piece>& last = rows.back();
        while (true) {
            more = "… " + std::to_string(hidden) + " más";
            const int used = row_width(last);
            if (last.empty() || used + kGap + ftxui::string_width(more) <= width) {
                break;
            }
            last.pop_back();
            ++hidden;
        }
        more = fit_width(more, width); // Sola en una fila muy angosta tampoco se pasa.
    }

    const ftxui::Decorator notice = palette.ink(&Theme::notice);
    ftxui::Elements lines;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        ftxui::Elements row;
        for (const Piece& piece : rows[r]) {
            if (!row.empty()) {
                row.push_back(ftxui::text(kGapText));
            }
            switch (piece.task->state) {
            case TaskState::Done:
                row.push_back(ftxui::text(piece.mark + " " + piece.text) | notice);
                break;
            case TaskState::Active:
                row.push_back(ftxui::text(piece.mark) | palette.ink(&Theme::heading_accent));
                row.push_back(ftxui::text(" " + piece.text) | ftxui::bold);
                break;
            case TaskState::Pending:
                row.push_back(ftxui::text(piece.mark + " " + piece.text));
                break;
            }
        }
        if (hidden > 0 && r + 1 == rows.size()) {
            if (!row.empty()) {
                row.push_back(ftxui::text(kGapText));
            }
            row.push_back(ftxui::text(more) | notice);
        }
        lines.push_back(ftxui::hbox(std::move(row)));
    }
    return ftxui::vbox(std::move(lines));
}

} // namespace chatbot::cli
