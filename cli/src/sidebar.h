#ifndef CHATBOT_CLI_SIDEBAR_H
#define CHATBOT_CLI_SIDEBAR_H

#include "conversation_list.h"
#include "conversation_store.h"

#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// Un día del calendario (sin hora).
struct CalendarDay {
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;

    bool operator==(const CalendarDay&) const = default;
};

/// Día local de un instante (chatbot::local_time); nullopt si no se puede convertir.
[[nodiscard]] std::optional<CalendarDay> local_day(std::time_t time);

/// Día de un updated_at tal como quedó escrito ("2026-10-02T23:58:00-06:00"
/// → 2026-10-02): la fecha local de cuando se guardó, la misma lectura que
/// ConversationList::format_date. nullopt si no es una fecha válida.
[[nodiscard]] std::optional<CalendarDay> day_of(std::string_view iso);

/// Grupo de un día visto desde today: "Hoy", "Ayer", "Últimos 7 días" (de 2
/// a 6 días antes) y si no la fecha corta ("2 oct"; con año si no es el de
/// today: "15 dic 2025"). Un día posterior a today cuenta como "Hoy".
[[nodiscard]] std::string group_label(CalendarDay day, CalendarDay today);

/// Grupo de una conversación de la lista visto desde today: group_label de
/// day_of(updated_at), o Sidebar::kUndated si es ilegible o no tiene fecha.
[[nodiscard]] std::string group_for(const ConversationSummary& summary, CalendarDay today);

/// Opciones del menú de una conversación (clic derecho), en orden.
enum class ConversationMenuItem { Rename, Export, Delete };
inline constexpr std::size_t kConversationMenuSize = 3;
/// "Renombrar", "Exportar" o "Borrar".
[[nodiscard]] std::string_view menu_item_label(ConversationMenuItem item);

/// Una fila de la barra lateral.
struct SidebarRow {
    enum class Kind {
        New,          ///< "+ Nueva  (Ctrl+N)", siempre la primera.
        Settings,     ///< "⚙ Configuración  (F2)", siempre la segunda.
        Header,       ///< Nombre de un grupo por fecha; no se selecciona.
        Conversation, ///< Una conversación; index es su posición en la lista.
        MenuItem,     ///< Opción del menú abierto, debajo de su conversación (index).
    };
    Kind kind = Kind::New;
    std::string text;      ///< Header: el nombre del grupo.
    std::size_t index = 0; ///< Conversation y MenuItem: índice en list().items().
    ConversationMenuItem item = ConversationMenuItem::Rename; ///< MenuItem.
};

/// Modelo de la barra lateral de conversaciones: filas agrupadas por fecha,
/// selección (las filas "+ Nueva" y "⚙ Configuración" o una conversación),
/// navegación con teclado
/// que salta los encabezados, desplazamiento y clic. Reusa ConversationList
/// para la selección, la confirmación de borrado, can_open e is_current. Sin
/// FTXUI; se usa solo desde el hilo de la interfaz.
class Sidebar {
public:
    /// Grupo de las conversaciones sin fecha válida (por ejemplo, ilegibles).
    static constexpr std::string_view kUndated = "Sin fecha";

    /// Al empezar: elementos (ya ordenados por el almacén) y la conversación
    /// abierta, que queda seleccionada si está guardada; si no, "+ Nueva".
    void open(std::vector<ConversationSummary> items, std::string current_id);

    /// Tras guardar, borrar o abrir: conserva la fila seleccionada si sigue
    /// existiendo (si no, la misma posición) y marca la conversación abierta.
    void refresh(std::vector<ConversationSummary> items, std::string current_id);

    /// Filas para el día today, en el orden de la lista (la más reciente
    /// primero). Los grupos sin conversaciones no aparecen.
    [[nodiscard]] std::vector<SidebarRow> rows(CalendarDay today) const;

    /// Índice en rows de la fila seleccionada.
    [[nodiscard]] std::size_t selected_row(const std::vector<SidebarRow>& rows) const;
    [[nodiscard]] bool new_selected() const { return pick_ == Pick::New; }
    [[nodiscard]] bool settings_selected() const { return pick_ == Pick::Settings; }

    /// Procesa una tecla con la barra enfocada. ↑/↓, PgUp/PgDn y Home/End
    /// se mueven saltando encabezados (Home y End van a la primera y a la
    /// última conversación; sin conversaciones, End va a "⚙ Configuración").
    /// Enter abre (Open), pide una nueva (New) o la configuración
    /// (Settings); Supr pide confirmación, que se contesta como dice
    /// yes_no.h ('s' o 'S' borra: Delete; Esc: Cancelled); Esc sin ella
    /// devuelve el foco (Close). En una conversación, 'e' exporta (Export)
    /// y 'r' abre el campo de renombrar (renaming()).
    /// Con el menú abierto: ↑/↓ mueven, Enter elige (Renombrar y Exportar
    /// como 'r' y 'e', Borrar como Supr), Esc lo cierra; otra tecla lo
    /// cierra y hace lo suyo. Con el campo de renombrar: Enter da Rename
    /// (id y text; el campo sigue abierto hasta stop_rename) y Esc lo
    /// cierra; lo demás no se procesa (lo edita la interfaz).
    /// page_size: filas visibles.
    [[nodiscard]] ListAction handle(ListKey key, std::string_view character, std::size_t page_size);

    /// Clic derecho en la línea line de la zona visible: si es una
    /// conversación legible, la selecciona y abre su menú. true si lo abrió.
    bool open_menu(std::size_t line, const std::vector<SidebarRow>& rows);
    [[nodiscard]] bool menu_open() const { return !menu_id_.empty(); }
    /// Al perder el foco.
    void close_menu() { menu_id_.clear(); }

    /// Conversación que se está renombrando, o vacío.
    [[nodiscard]] const std::string& renaming() const { return renaming_id_; }
    /// Texto del campo de renombrar: la interfaz lo edita (ftxui::Input).
    [[nodiscard]] std::string& rename_text() { return rename_text_; }
    [[nodiscard]] const std::string& rename_text() const { return rename_text_; }
    /// Cierra el campo (se guardó, Esc o se perdió el foco).
    void stop_rename();

    /// Primera fila visible.
    [[nodiscard]] std::size_t top() const { return top_; }
    /// Ajusta la vista a view_height filas y la mueve para que la selección
    /// se vea (después de usar el teclado).
    void fit(const std::vector<SidebarRow>& rows, std::size_t view_height);
    /// Rueda: mueve la vista lines filas. Si la selección queda fuera, pasa a
    /// la fila seleccionable visible más cercana.
    void scroll(int lines, const std::vector<SidebarRow>& rows, std::size_t view_height);
    /// Clic en la línea line de la zona visible (0 = la primera visible).
    /// Selecciona la fila y devuelve New, Settings u Open; None si es un encabezado,
    /// una ilegible o no hay fila ahí. En una opción del menú, la elige como
    /// Enter. Cierra el menú, y el campo de renombrar si el clic no es en él.
    [[nodiscard]] ListAction click(std::size_t line, const std::vector<SidebarRow>& rows);

    [[nodiscard]] const ConversationList& list() const { return list_; }
    /// Texto de la confirmación de borrado pendiente, si la hay.
    [[nodiscard]] std::optional<std::string> confirmation() const { return list_.confirmation(); }

private:
    /// Selecciona la fila row (New, Settings o Conversation).
    void select_row(const SidebarRow& row);
    /// Va a la conversación index desde una fila fija.
    void pick_conversation(std::size_t index);
    /// Abre el campo de renombrar de la seleccionada, si es legible.
    void start_rename();
    /// Lo que hace la opción del menú (lo cierra antes).
    [[nodiscard]] ListAction choose(ConversationMenuItem item, std::size_t page_size);

    /// Qué fila está seleccionada: una fija o la conversación de list_.
    enum class Pick { New, Settings, Conversation };
    ConversationList list_;
    Pick pick_ = Pick::New;
    std::size_t top_ = 0;
    std::string menu_id_; ///< Conversación con el menú abierto (la seleccionada).
    std::size_t menu_choice_ = 0;
    std::string renaming_id_;
    std::string rename_text_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_SIDEBAR_H
