#ifndef CHATBOT_CLI_CODE_SCREEN_H
#define CHATBOT_CLI_CODE_SCREEN_H

#include "code_mode.h"
#include "glyphs.h"
#include "history_view.h"
#include "key_log.h"
#include "models_loader.h"
#include "request_runner.h"
#include "screen_input.h"
#include "scroll.h"
#include "settings_binding.h"
#include "settings_screen.h"
#include "text_area.h"
#include "theme.h"
#include "title_bar.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/terminal.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chatbot::cli {

// La pantalla de Moss Code (guias/18-moss-code.md, secciones 1 y 9): una columna con el título,
// el historial, la línea de estado, la lista de tareas y la caja. La lógica es de CodeMode; aquí
// solo se dibuja y se reparten las teclas. Usa ftxui::component: ejecutable moss y
// chatbot_ui_tests. Solo con CHATBOT_BUILD_CODE.

/// Ayuda de teclas de la barra de título del modo código.
inline constexpr TitleHelp kCodeTitleHelp{"Ctrl+O salida completa", "Ctrl+O"};
/// Junto a la actividad de la línea de estado, si cabe.
inline constexpr std::string_view kCancelHint = " (Esc para cancelar)";

/// Lo que usa CodeScreen. Todo lo referido debe vivir más que la pantalla.
struct CodeScreenContext {
    CodeMode& mode;
    ModelsLoader& models_loader; ///< La lista de modelos de F2.
    Appearance& appearance;      ///< La vigente; cambia al guardar la apariencia en F2.
    Palette& palette;            ///< Con lo que se dibuja todo; cambia con la apariencia.
    Symbols detected_symbols = Symbols::Unicode; ///< StartupAppearance::detected_symbols.
    RequestRunner::Post post;                    ///< screen_post: F2 → Addons.
    /// La terminal (run_code_screen): Ctrl+C, Ctrl+Z, el pegado y el loop. nullptr en las
    /// pruebas: entonces esas teclas no hacen nada.
    ftxui::App* app = nullptr;
    /// Columnas y filas de la pantalla (fijas en las pruebas).
    std::function<ftxui::Dimensions()> size = ftxui::Terminal::Size;
};

/// La pantalla de Moss Code sin la terminal: component() se dibuja y recibe eventos igual en una
/// ftxui::Screen de prueba. Se usa solo desde el hilo de la interfaz; no se copia ni se mueve
/// (sus componentes apuntan a ella). Se destruye antes que el CodeMode y el ModelsLoader.
class CodeScreen {
public:
    explicit CodeScreen(CodeScreenContext context);
    CodeScreen(const CodeScreen&) = delete;
    CodeScreen& operator=(const CodeScreen&) = delete;
    CodeScreen(CodeScreen&&) = delete;
    CodeScreen& operator=(CodeScreen&&) = delete;

    /// La raíz: el dibujo y todas las teclas y el ratón.
    [[nodiscard]] ftxui::Component component() const { return root_; }
    /// Al abrir: sin proveedor, F2 abierto con setup_problem() (y warning, el aviso de la
    /// apariencia); si no, el foco en la caja y warning en la línea de estado.
    void start(const std::string& warning);
    /// El OnFinished de CodeMode: el texto que no llegó al modelo vuelve a la caja si está vacía.
    void finished(std::optional<std::string> restored);
    /// Corre el loop de la terminal hasta Ctrl+C (solo con app).
    void run();

    [[nodiscard]] TextArea& input() { return *input_area_; }
    [[nodiscard]] const SettingsScreen& settings() const { return settings_; }
    /// El aviso de la línea de estado (dura hasta la siguiente tecla).
    [[nodiscard]] const std::string& flash() const { return flash_; }

private:
    void send();
    void open_settings(std::string notice);
    [[nodiscard]] ftxui::Element render();
    [[nodiscard]] bool handle_mouse(ftxui::Event event); // mouse() no es const.
    [[nodiscard]] bool handle_event(ftxui::Event event);

    CodeMode& mode_;
    Palette& palette_;
    std::function<ftxui::Dimensions()> size_;
    ftxui::App* app_;
    std::optional<ScreenControl> control_;
    Scroll scroll_;
    HistoryView history_;
    ftxui::Box export_box_; ///< La de title_bar: sin [Exportar], siempre vacía.
    std::string flash_;
    int right_tab_ = 0; ///< 0: el modo código; 1: la configuración.
    std::shared_ptr<TextArea> input_area_;
    SettingsContext settings_context_;
    SettingsScreen settings_;
    ScreenInput screen_input_;
    KeyLog key_log_;
    ftxui::Component root_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_CODE_SCREEN_H
