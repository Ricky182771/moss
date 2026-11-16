#ifndef CHATBOT_CLI_KEY_EVENTS_H
#define CHATBOT_CLI_KEY_EVENTS_H

#include "conversation_list.h"
#include "key_log.h"
#include "paste.h"

#include <ftxui/component/event.hpp>

namespace chatbot::cli {

// Traducciones de ftxui::Event a los tipos sin FTXUI de la interfaz. Usan
// ftxui::component: solo se compilan en el ejecutable moss.

/// Traduce una tecla de FTXUI a la de la barra de conversaciones.
ListKey list_key(const ftxui::Event& event);

/// Traduce un evento de FTXUI para el acumulador del pegado entre corchetes.
PasteKey paste_key(const ftxui::Event& event);

/// Traduce un evento de FTXUI para el registro de teclas (key_log.h). La
/// vista apunta a event: debe vivir mientras se use.
KeyLogEvent key_log_event(ftxui::Event& event); // mouse() no es const.

} // namespace chatbot::cli

#endif // CHATBOT_CLI_KEY_EVENTS_H
