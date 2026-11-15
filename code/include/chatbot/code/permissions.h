#ifndef CHATBOT_CODE_PERMISSIONS_H
#define CHATBOT_CODE_PERMISSIONS_H

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace chatbot::code {

/// Qué hacer con una llamada antes de ejecutarla (guias/18-moss-code.md, sección 5).
enum class Permission {
    Allow, ///< Se ejecuta sin preguntar.
    Ask,   ///< Se pregunta (sin interfaz, se rechaza sin esperar).
    Deny,  ///< Se rechaza sin preguntar.
};

/// Decide el permiso de cada llamada. Se usa solo desde el hilo de la interfaz.
/// C1 no tiene herramientas que cambien nada: read, glob, grep y todo_write se permiten (la raíz
/// ya la impone ProjectRoot) y cualquier otra pregunta. C3 llena la regla con los modos y las
/// reglas de .moss/.
class PermissionEngine {
public:
    /// name y arguments tal como los mandó el modelo (arguments: un objeto JSON).
    using Rule = std::function<Permission(std::string_view name, std::string_view arguments)>;

    /// La regla de C1 (arriba).
    PermissionEngine();
    explicit PermissionEngine(Rule rule);

    [[nodiscard]] Permission decide(std::string_view name, std::string_view arguments) const;

    /// "Permitir siempre": desde ahora, lo que la regla pregunte de name se permite en esta
    /// sesión (Deny sigue ganando). Sin escribir nada en disco.
    void allow_always(std::string_view name);

private:
    Rule rule_;
    std::set<std::string, std::less<>> always_;
};

} // namespace chatbot::code

#endif // CHATBOT_CODE_PERMISSIONS_H
