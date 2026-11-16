#include "interrupt.h"

#include <windows.h>

#include <atomic>

namespace chatbot::cli {

namespace {

std::atomic<bool> interrupted{false};

// Corre en un hilo que crea Windows. El primer Ctrl+C solo marca la bandera; el segundo no se
// atiende y el manejador por defecto mata el proceso.
BOOL WINAPI on_console_event(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    return interrupted.exchange(true) ? FALSE : TRUE;
}

} // namespace

InterruptGuard::InterruptGuard() {
    interrupted.store(false);
    SetConsoleCtrlHandler(on_console_event, TRUE);
}

InterruptGuard::~InterruptGuard() {
    SetConsoleCtrlHandler(on_console_event, FALSE);
}

bool InterruptGuard::requested() {
    return interrupted.load();
}

} // namespace chatbot::cli
