#include "glob_match.h"

#include "chatbot/utf8.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace chatbot::code {

namespace {

/// Código por código; un byte inválido pasa a un valor aparte (sustituto suelto: nunca coincide con
/// un carácter válido ni con '/').
std::u32string decode(std::string_view text) {
    std::u32string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t before = i;
        if (const std::optional<std::uint32_t> code = utf8::next_code_point(text, i)) {
            out.push_back(static_cast<char32_t>(*code));
        } else {
            out.push_back(static_cast<char32_t>(0xDC00 + static_cast<unsigned char>(text[before])));
            i = before + 1;
        }
    }
    return out;
}

char32_t lower(char32_t c) {
    return c >= U'A' && c <= U'Z' ? c + (U'a' - U'A') : c;
}

char32_t upper(char32_t c) {
    return c >= U'a' && c <= U'z' ? c - (U'a' - U'A') : c;
}

/// Reemplaza el primer par de llaves cerrado por cada una de sus alternativas, recursivamente.
/// Las llaves sin cerrar quedan como texto. out no pasa de max variantes.
void expand_braces(const std::string& pattern, std::vector<std::string>& out, std::size_t max) {
    if (out.size() >= max) {
        return;
    }
    for (std::size_t open = 0; open < pattern.size(); ++open) {
        if (pattern[open] == '\\') {
            ++open;
            continue;
        }
        if (pattern[open] != '{') {
            continue;
        }
        std::size_t depth = 0;
        std::size_t close = std::string::npos;
        std::vector<std::size_t> commas;
        for (std::size_t i = open + 1; i < pattern.size(); ++i) {
            const char c = pattern[i];
            if (c == '\\') {
                ++i;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                if (depth == 0) {
                    close = i;
                    break;
                }
                --depth;
            } else if (c == ',' && depth == 0) {
                commas.push_back(i);
            }
        }
        if (close == std::string::npos) {
            continue;
        }
        const std::string prefix = pattern.substr(0, open);
        const std::string suffix = pattern.substr(close + 1);
        commas.push_back(close);
        std::size_t start = open + 1;
        for (const std::size_t end : commas) {
            expand_braces(prefix + pattern.substr(start, end - start) + suffix, out, max);
            start = end + 1;
        }
        return;
    }
    out.push_back(pattern);
}

} // namespace

GlobPattern::GlobPattern(std::string_view pattern, bool ignore_case) : ignore_case_(ignore_case) {
    if (pattern.starts_with("./")) {
        pattern.remove_prefix(2);
    }
    if (pattern.size() > kMaxPatternBytes) {
        usable_ = false;
        return;
    }
    whole_path_ = pattern.find('/') != std::string_view::npos;
    std::vector<std::string> variants;
    expand_braces(std::string{pattern}, variants, kMaxAlternatives);

    for (const std::string& variant : variants) {
        const std::u32string p = decode(variant);
        Program program;
        for (std::size_t i = 0; i < p.size();) {
            Token token;
            const char32_t c = p[i];
            if (c == U'\\' && i + 1 < p.size()) {
                token.character = p[i + 1];
                i += 2;
            } else if (c == U'*') {
                std::size_t stars = 0;
                while (i < p.size() && p[i] == U'*') {
                    ++stars;
                    ++i;
                }
                token.kind = Kind::Star;
                if (stars > 1) {
                    token.kind = Kind::StarStar;
                    if (i < p.size() && p[i] == U'/') {
                        token.kind = Kind::StarStarSlash;
                        ++i;
                    }
                }
            } else if (c == U'?') {
                token.kind = Kind::AnyOne;
                ++i;
            } else if (c == U'[') {
                std::size_t j = i + 1;
                token.negated = j < p.size() && (p[j] == U'!' || p[j] == U'^');
                j += token.negated ? 1 : 0;
                bool first = true;
                while (j < p.size() && (p[j] != U']' || first)) {
                    first = false;
                    char32_t low = p[j];
                    if (low == U'\\' && j + 1 < p.size()) {
                        low = p[++j];
                    }
                    if (j + 2 < p.size() && p[j + 1] == U'-' && p[j + 2] != U']') {
                        token.ranges.push_back({low, p[j + 2]});
                        j += 3;
                    } else {
                        token.ranges.push_back({low, low});
                        ++j;
                    }
                }
                if (j < p.size()) {
                    token.kind = Kind::Class;
                    i = j + 1;
                } else { // Sin cerrar: '[' literal.
                    token.ranges.clear();
                    token.negated = false;
                    token.character = c;
                    ++i;
                }
            } else {
                token.character = c;
                ++i;
            }
            program.push_back(std::move(token));
        }
        programs_.push_back(std::move(program));
    }
}

bool GlobPattern::in_class(const Token& token, char32_t c) const {
    const auto contains = [&token](char32_t x) {
        for (const Range& range : token.ranges) {
            if (x >= range.first && x <= range.last) {
                return true;
            }
        }
        return false;
    };
    const bool found = contains(c) || (ignore_case_ && (contains(lower(c)) || contains(upper(c))));
    return found != token.negated;
}

bool GlobPattern::run(const Program& program, std::u32string_view text) const {
    const std::size_t count = program.size();
    std::vector<char> now(count + 1, 0);
    std::vector<char> next(count + 1, 0);
    // Los comodines pueden no consumir nada: de i pasan a i + 1 (solo hacia adelante).
    const auto close = [&](std::vector<char>& states) {
        for (std::size_t i = 0; i < count; ++i) {
            const Kind kind = program[i].kind;
            if (states[i] && (kind == Kind::Star || kind == Kind::StarStar ||
                              kind == Kind::StarStarSlash)) {
                states[i + 1] = 1;
            }
        }
    };
    now[0] = 1;
    close(now);
    for (const char32_t c : text) {
        std::fill(next.begin(), next.end(), char{0});
        bool any = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (!now[i]) {
                continue;
            }
            const Token& token = program[i];
            switch (token.kind) {
            case Kind::Literal:
                if (c == token.character ||
                    (ignore_case_ && lower(c) == lower(token.character))) {
                    next[i + 1] = 1;
                }
                break;
            case Kind::AnyOne:
                next[i + 1] = c != U'/' ? 1 : next[i + 1];
                break;
            case Kind::Class:
                if (c != U'/' && in_class(token, c)) {
                    next[i + 1] = 1;
                }
                break;
            case Kind::Star:
                next[i] = c != U'/' ? 1 : next[i];
                break;
            case Kind::StarStar:
                next[i] = 1;
                break;
            case Kind::StarStarSlash:
                next[i] = 1;
                next[i + 1] = c == U'/' ? 1 : next[i + 1];
                break;
            }
        }
        close(next);
        for (const char state : next) {
            any = any || state;
        }
        if (!any) {
            return false;
        }
        now.swap(next);
    }
    return now[count] != 0;
}

bool GlobPattern::matches(std::string_view path) const {
    if (!usable_ || path.size() > kMaxTextBytes) {
        return false;
    }
    if (!whole_path_) {
        path.remove_prefix(path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
    }
    const std::u32string text = decode(path);
    for (const Program& program : programs_) {
        if (run(program, text)) {
            return true;
        }
    }
    return false;
}

bool glob_match(std::string_view pattern, std::string_view path, bool ignore_case) {
    return GlobPattern{pattern, ignore_case}.matches(path);
}

} // namespace chatbot::code
