#ifndef CHATBOT_CLI_SCREEN_INPUT_H
#define CHATBOT_CLI_SCREEN_INPUT_H

#include "key_log.h"
#include "paste.h"
#include "request_runner.h"
#include "scroll.h"
#include "settings_screen.h"
#include "text_area.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace chatbot::cli {

// Lo que comparten la pantalla del chat y la del modo código para manejar la
// terminal y la entrada: salir, suspender, el pegado, la caja y el scroll del
// historial. Usa ftxui::component: solo en el ejecutable moss.

/// Líneas que mueve cada paso de la rueda del ratón.
inline constexpr int kWheelStep = 3;

/// Atajos de pegado de Windows: Ctrl+V (Ctrl+Shift+V llega igual, como el
/// byte 0x16) y Shift+Insert (ESC [ 2 ; 2 ~ = 1b 5b 32 3b 32 7e, que FTXUI
/// v7.0.3 entrega tal cual como Event::Special). Bytes confirmados en conhost
/// (Windows 11 LTSC 2024) con CHAT_DEBUG_KEYS.
[[nodiscard]] bool is_paste_shortcut(const ftxui::Event& event);

/// screen.Post(tarea) y un Event::Custom para redibujar: el Post de Session,
/// ModelsLoader, ModelPicker y Addons. screen debe vivir más que lo devuelto.
[[nodiscard]] RequestRunner::Post screen_post(ftxui::App& screen);

/// El loop de la pantalla y el pegado entre corchetes de la terminal (DECSET
/// 2004): se activa con la primera tarea del loop, cuando FTXUI ya instaló la
/// terminal (en conhost, el modo VT de salida lo activa FTXUI) y se desactiva
/// antes de salir del loop o de suspender. Todo va por std::cout, el canal de
/// FTXUI, entre cuadros. Se usa solo desde el hilo de la interfaz.
class ScreenControl {
public:
    /// screen debe vivir más que este objeto.
    explicit ScreenControl(ftxui::App& screen) : screen_(screen) {}

    /// Ctrl+C: desactiva el pegado entre corchetes y sale del loop.
    void quit();
    /// Al empezar cada evento: de vuelta de Ctrl+Z (fg), el shell pudo haber
    /// desactivado el pegado entre corchetes; se activa otra vez.
    void resume();
    /// POSIX, Ctrl+Z: FTXUI suspende el proceso (SIGTSTP) y el shell no debe
    /// recibir las marcas. true si era eso: el evento sigue a FTXUI.
    [[nodiscard]] bool suspend(const ftxui::Event& event);
    /// Corre el loop con root: Ctrl+C llega como evento y el pegado entre
    /// corchetes se activa con la primera tarea.
    void run(const ftxui::Component& root);

private:
    ftxui::App& screen_;
    bool paste_mode_lost_ = false; ///< POSIX: se suspendió con Ctrl+Z (se desactivó).
};

/// A dónde va lo que no es una tecla de la pantalla: el pegado (entre
/// corchetes o con un atajo), sus avisos y el registro de teclas. El pegado
/// va a la configuración si está abierta; si no, a la caja; con la barra de
/// conversaciones enfocada, se ignora. Se usa solo desde el hilo de la
/// interfaz.
class ScreenInput {
public:
    /// sidebar_focused: true si la barra de conversaciones tiene el foco;
    /// vacía, nunca (pantalla sin barra). settings, input y flash (el aviso
    /// de la línea de estado) deben vivir más que este objeto.
    ScreenInput(SettingsScreen& settings, TextArea& input, std::string& flash,
                std::function<bool()> sidebar_focused = {});

    /// El acumulador del pegado entre corchetes: el renderer le avisa de
    /// cada cuadro (BracketedPaste::frame).
    [[nodiscard]] BracketedPaste& bracketed_paste() { return bracketed_paste_; }

    /// A dónde iría un pegado según el foco (paste.h).
    [[nodiscard]] PasteTarget paste_target() const;
    /// Aviso en la línea de estado o, con la configuración abierta, bajo su
    /// formulario (la línea de estado no se ve).
    void notify(std::string_view notice);
    /// Inserta un pegado donde está el foco: un solo Event::Character con
    /// todo el texto, que Input::HandleCharacter inserta de una vez en el
    /// cursor. Así sirve en la caja y en todos los campos de la
    /// configuración. Con la barra lateral, se ignora. true si insertó algo.
    bool deliver_paste(std::string_view raw, bool truncated);
    /// Pegado entre corchetes: lo que llega entre las marcas es texto, aunque
    /// sea Enter, Tab o Ctrl+C. true si el evento fue del pegado (ya se
    /// manejó).
    [[nodiscard]] bool feed_paste(const ftxui::Event& event);
    /// Windows, Ctrl+V y Shift+Insert: conhost no pega con la entrada en modo
    /// VT (la activa FTXUI) y la app lee el portapapeles. En Linux la terminal
    /// pega sola. true si era uno de esos atajos (ya se manejó).
    [[nodiscard]] bool paste_shortcut(const ftxui::Event& event, KeyLog& key_log);
    /// Escribe el evento en el registro de teclas (CHAT_DEBUG_KEYS, key_log.h).
    void log_event(KeyLog& key_log, ftxui::Event& event); // mouse() no es const.

private:
    [[nodiscard]] bool sidebar_focused() const { return sidebar_focused_ && sidebar_focused_(); }

    SettingsScreen& settings_;
    TextArea& input_;
    std::string& flash_;
    std::function<bool()> sidebar_focused_;
    BracketedPaste bracketed_paste_;
};

/// Teclas de la caja: "\" + Enter hace el salto en lugar de enviar, Alt+Enter
/// también (donde llega: en Windows es pantalla completa), y ←/→, ↑/↓ y Tab
/// son siempre suyos (si llegaran al contenedor, moverían el foco). true si
/// la caja lo usó.
[[nodiscard]] bool input_key(const ftxui::Event& event, TextArea& input);

/// Teclas del historial: PgUp y PgDn; Home/End solo con la caja vacía (con
/// texto mueven el cursor, los maneja la caja). true si movió el historial.
[[nodiscard]] bool scroll_key(const ftxui::Event& event, Scroll& scroll, const TextArea& input);

/// La rueda: sobre la caja la desplaza si su texto no cabe; si no, mueve el
/// historial kWheelStep líneas. true si era la rueda.
[[nodiscard]] bool wheel_event(ftxui::Event& event, Scroll& scroll, TextArea& input);

} // namespace chatbot::cli

#endif // CHATBOT_CLI_SCREEN_INPUT_H
