#include "interrupt.h"

#include <atomic>
#include <csignal>

namespace chatbot::cli {

namespace {

std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free, "se usa dentro de un manejador de señal");
struct sigaction previous_action {};

void on_interrupt(int) {
    interrupted.store(true);
}

} // namespace

InterruptGuard::InterruptGuard() {
    interrupted.store(false);
    struct sigaction action {};
    action.sa_handler = on_interrupt;
    sigemptyset(&action.sa_mask);
    // SA_RESETHAND: después del primer Ctrl+C vuelve el manejador por defecto, y el segundo mata.
    action.sa_flags = static_cast<int>(SA_RESETHAND);
    sigaction(SIGINT, &action, &previous_action);
}

InterruptGuard::~InterruptGuard() {
    sigaction(SIGINT, &previous_action, nullptr);
}

bool InterruptGuard::requested() {
    return interrupted.load();
}

} // namespace chatbot::cli
