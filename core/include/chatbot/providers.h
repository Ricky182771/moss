#ifndef CHATBOT_PROVIDERS_H
#define CHATBOT_PROVIDERS_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace chatbot {

/// Un proveedor compatible con la API de OpenAI.
struct ProviderInfo {
    std::string_view id;       ///< Lo que se guarda en config.json ("provider").
    std::string_view name;     ///< Nombre en pantalla.
    std::string_view base_url;
    bool needs_key = true; ///< false: servidor local (la key es opcional).
};

/// Tabla de proveedores, en el orden de la pantalla. Las URL base se
/// confirmaron en la documentación de cada proveedor (octubre de 2026).
/// Único lugar del código con esta tabla.
[[nodiscard]] std::span<const ProviderInfo> providers();

/// Índice del proveedor con ese id exacto en providers(), o nullopt.
[[nodiscard]] std::optional<std::size_t> provider_index(std::string_view id);

/// Un endpoint personalizado de config.json ("endpoints"). Su proveedor (en
/// config.json, el RPC y la llave de credentials.json) es "custom:<id>".
struct Endpoint {
    std::string id; ///< Fijo: no cambia al editar el nombre o la URL.
    std::string name;
    std::string base_url;
};

/// Prefijo del proveedor de un endpoint: "custom:<id>".
inline constexpr std::string_view kEndpointPrefix = "custom:";
/// Tope de endpoints guardados.
inline constexpr std::size_t kMaxEndpoints = 20;
/// Largo máximo del nombre, en puntos de código.
inline constexpr std::size_t kMaxEndpointName = 40;

/// "custom:" + id.
[[nodiscard]] std::string endpoint_provider(std::string_view id);
/// El id de un proveedor "custom:<id>" (puede quedar vacío), o nullopt si
/// provider no empieza con "custom:".
[[nodiscard]] std::optional<std::string_view> endpoint_id(std::string_view provider);
/// [a-z0-9][a-z0-9-]{0,31}, distinto de los ids de providers() y de "custom".
[[nodiscard]] bool valid_endpoint_id(std::string_view id);
/// Por qué no sirve el nombre (vacío, más de kMaxEndpointName puntos de
/// código, UTF-8 inválido o con controles), o nullopt. No revisa si se repite.
[[nodiscard]] std::optional<std::string> endpoint_name_problem(std::string_view name);
/// Los nombres son iguales sin distinguir mayúsculas (ASCII y Latin-1:
/// "Á" == "á").
[[nodiscard]] bool same_endpoint_name(std::string_view a, std::string_view b);
/// Id para un endpoint nuevo con ese nombre: minúsculas, sin acentos, lo
/// demás a "-", sin "-" repetidos ni en los extremos, a lo más 32
/// caracteres ("endpoint" si queda vacío); si choca con la tabla, "custom"
/// o uno de existing, "-2", "-3", …
[[nodiscard]] std::string make_endpoint_id(std::string_view name,
                                           std::span<const Endpoint> existing);

} // namespace chatbot

#endif // CHATBOT_PROVIDERS_H
