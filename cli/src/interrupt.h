#ifndef CHATBOT_CLI_INTERRUPT_H
#define CHATBOT_CLI_INTERRUPT_H

namespace chatbot::cli {

/// Avisa de Ctrl+C mientras vive, sin matar el proceso: el primero solo marca requested(); el
/// segundo vuelve al manejador por defecto y mata el proceso (por si el aviso no basta). Al
/// destruirla se restaura el manejador de antes. Solo hay una a la vez (moss code -p).
/// Implementación por sistema: interrupt_posix.cpp o interrupt_windows.cpp (CMake elige).
class InterruptGuard {
public:
    InterruptGuard();
    ~InterruptGuard();
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

    /// true desde el primer Ctrl+C. Segura entre hilos y desde un manejador de señal.
    [[nodiscard]] static bool requested();
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_INTERRUPT_H
