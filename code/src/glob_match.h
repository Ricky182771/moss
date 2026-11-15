#ifndef CHATBOT_CODE_SRC_GLOB_MATCH_H
#define CHATBOT_CODE_SRC_GLOB_MATCH_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::code {

/// Un patrón glob compilado una vez para compararlo con muchas rutas (glob, y luego grep).
/// Sintaxis: `*` (no cruza '/'), `?` (un carácter, no '/'), `**` (cruza carpetas; `**/` también
/// admite cero carpetas), clases `[abc]` `[a-z]` `[!x]` y llaves `{a,b}` (anidables), `\x` literal.
/// Una `[` sin su `]` y unas llaves sin cerrar son texto literal. Todo por caracteres Unicode.
/// Sin retroceso: simula el autómata, O(patrón × texto) en el peor caso. Topes contra patrones
/// hostiles: un patrón de más de kMaxPatternBytes o una ruta de más de kMaxTextBytes no
/// coinciden con nada, y las llaves dan como mucho kMaxAlternatives variantes.
/// shortcut: sin mayúsculas solo para ASCII (ignore_case), cuando haga falta Unicode completo.
class GlobPattern {
public:
    static constexpr std::size_t kMaxPatternBytes = 512;
    static constexpr std::size_t kMaxTextBytes = 4096;
    static constexpr std::size_t kMaxAlternatives = 64;

    /// ignore_case: Windows. Nunca falla (un patrón raro es literal).
    GlobPattern(std::string_view pattern, bool ignore_case);

    /// Compara con path (relativa, con '/'). Un patrón sin '/' se compara con el nombre del
    /// archivo (lo que sigue al último '/') a cualquier profundidad; con '/', con toda la ruta.
    [[nodiscard]] bool matches(std::string_view path) const;

private:
    enum class Kind { Literal, AnyOne, Star, StarStar, StarStarSlash, Class };
    struct Range {
        char32_t first;
        char32_t last;
    };
    struct Token {
        Kind kind = Kind::Literal;
        char32_t character = 0; ///< Literal.
        bool negated = false;   ///< Class.
        std::vector<Range> ranges; ///< Class.
    };
    using Program = std::vector<Token>;

    [[nodiscard]] bool run(const Program& program, std::u32string_view text) const;
    [[nodiscard]] bool in_class(const Token& token, char32_t c) const;

    std::vector<Program> programs_; ///< Una por variante de llaves.
    bool whole_path_ = false;       ///< El patrón tiene '/'.
    bool ignore_case_ = false;
    bool usable_ = true;
};

/// Atajo para pruebas y usos sueltos: GlobPattern(pattern, ignore_case).matches(path).
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view path,
                              bool ignore_case = false);

} // namespace chatbot::code

#endif // CHATBOT_CODE_SRC_GLOB_MATCH_H
