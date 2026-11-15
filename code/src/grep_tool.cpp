#include "chatbot/child_process.h"
#include "chatbot/platform.h"
#include "chatbot/utf8.h"

#include "glob_match.h"
#include "output_cap.h"
#include "tool_internal.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace chatbot::code {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::size_t kMaxResults = 200;
constexpr std::int64_t kMaxContext = 10;
/// Archivos más grandes no se buscan (en rg, --max-filesize 1M).
constexpr std::size_t kMaxFileBytes = 1024 * 1024;
/// Líneas más largas no se buscan (minificados, datos): con std::regex costarían demasiado.
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxShownLineChars = 500;
constexpr std::size_t kMaxPatternBytes = 1000;
/// Tope del tamaño estimado del autómata de la búsqueda propia (pattern_problem).
constexpr std::size_t kMaxPatternWeight = 1000;
/// Una repetición contada más grande ya es demasiado (y así el producto no se desborda).
constexpr std::size_t kMaxRepeat = 100000;
constexpr std::chrono::milliseconds kRgTimeout{30000};
constexpr std::size_t kMaxShownBytes = 200;
constexpr std::size_t kMaxShownErrorBytes = 1000;

constexpr std::string_view kDescription =
    "Searches file contents with a regular expression. Stick to the syntax that every engine "
    "shares: literals, . , character classes ([a-z], [^0-9]), \\d \\w \\s \\b, quantifiers "
    "(* + ? {n,m}), alternation (a|b), groups and the anchors ^ and $ (start and end of a line). "
    "Lookahead, lookbehind and backreferences are not supported. output_mode \"files\" (default) "
    "lists the files with a match; \"content\" shows the matching lines as path:line:text (the "
    "context lines as path-line-text, and -- between groups); \"count\" shows "
    "path:matching_lines. Returns up to 200 matches (200 files in files and count modes), files "
    "sorted by path. In a git repository it respects .gitignore. Binary files, files over 1 MiB "
    "and lines over 4 KiB are not searched.";

constexpr std::string_view kSchema = R"({
    "type": "object",
    "properties": {
        "pattern": {"type": "string", "description": "Regular expression to search for."},
        "path": {"type": "string",
                 "description": "File or directory to search, relative to the project root or absolute. Default: the project root."},
        "glob": {"type": "string",
                 "description": "Only search files whose path (relative to path) matches this glob, e.g. \"*.cpp\" or \"src/**/*.{h,cpp}\"."},
        "ignore_case": {"type": "boolean", "description": "Case-insensitive search. Default false."},
        "output_mode": {"type": "string", "enum": ["files", "content", "count"],
                        "description": "files (default), content or count."},
        "context": {"type": "integer", "minimum": 0,
                    "description": "Lines of context before and after each match in content mode, 0 to 10. Default 0."}
    },
    "required": ["pattern"]
})";

enum class Mode { Files, Content, Count };

struct Line {
    std::uint64_t number = 0;
    bool match = false;
    std::string text; ///< Sin el '\n' final y sin recortar.
};

/// Lo que se encontró en un archivo, igual con rg que con la búsqueda propia.
struct FileHits {
    std::string display;
    std::size_t matches = 0; ///< Líneas que coinciden (en Files basta con 1).
    std::vector<Line> lines; ///< Solo en Content: coincidencias y contexto, en orden.
};

/// Los archivos con coincidencias o el resultado final (error, cancelación).
using Outcome = std::variant<std::vector<FileHits>, ToolResult>;

struct Query {
    std::string pattern;
    bool ignore_case = false;
    Mode mode = Mode::Files;
    std::size_t context = 0;
    fs::path base;      ///< Carpeta o archivo, ya resuelto.
    fs::path glob_base; ///< Contra la que se compara glob: base, o su carpeta si es un archivo.
    bool base_is_file = false;
    std::optional<GlobPattern> glob;

    [[nodiscard]] bool keeps_relative(std::string_view relative) const {
        return !glob || glob->matches(relative);
    }
    [[nodiscard]] bool keeps(const fs::path& path) const {
        return keeps_relative(generic_text(path.lexically_relative(glob_base)));
    }
};

/// Lo que cuenta contra el tope: coincidencias en Content, archivos en Files y Count.
std::size_t units(const FileHits& file, Mode mode) {
    return mode == Mode::Content ? file.matches : 1;
}

ToolResult invalid_pattern(const std::string& why) {
    return ToolResult{"Invalid regular expression: " + why, "Patrón inválido", ToolStatus::Error};
}

ToolResult too_complex() {
    return invalid_pattern("the pattern is too complex for the built-in search (nested "
                           "repetition counts); simplify it.");
}

/// Orden por ruta, con '/' antes que cualquier otro carácter ("a/b" antes que "a-b", como
/// rg --sort path).
bool path_less(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        const auto key = [](char c) { return c == '/' ? 0 : static_cast<unsigned char>(c) + 1; };
        return key(x) < key(y);
    });
}

// ---- Búsqueda propia ----------------------------------------------------------------------

/// Si en pattern[at] empieza una repetición contada ({n}, {n,} o {n,m}): cuántas copias hace
/// como mucho del átomo anterior y dónde cierra.
std::optional<std::pair<std::size_t, std::size_t>> counted_repeat(std::string_view pattern,
                                                                  std::size_t at) {
    std::size_t i = at + 1;
    const auto number = [&](std::size_t& value) {
        const std::size_t start = i;
        value = 0;
        for (; i < pattern.size() && pattern[i] >= '0' && pattern[i] <= '9'; ++i) {
            value = std::min(kMaxRepeat + 1,
                             value * 10 + static_cast<std::size_t>(pattern[i] - '0'));
        }
        return i > start;
    };
    std::size_t low = 0;
    if (!number(low)) {
        return std::nullopt;
    }
    std::size_t high = low;
    if (i < pattern.size() && pattern[i] == ',') {
        ++i;
        if (!number(high)) {
            high = low + 1; // {n,}: n copias y una con estrella.
        }
    }
    if (i >= pattern.size() || pattern[i] != '}') {
        return std::nullopt;
    }
    return std::pair{std::max(low, high), i};
}

/// El error si la búsqueda propia no acepta pattern. Con __polynomial, std::regex de libstdc++
/// simula el autómata en vez de retroceder (nada exponencial), pero su costo por carácter crece
/// con el tamaño del autómata, que las repeticiones contadas multiplican ("(a{100}){100}"), y
/// cada lookahead lanza otra búsqueda por posición. Estima el tamaño sumando los átomos y los
/// grupos (cientos de "(.)" también son cientos de estados) y multiplicando por las repeticiones
/// contadas; sobrestimar basta.
std::optional<ToolResult> pattern_problem(std::string_view pattern) {
    std::vector<std::size_t> groups{0}; // Peso de cada grupo abierto.
    std::size_t last = 0;               // Peso del último átomo, al que se aplica una repetición.
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        std::size_t atom = 1;
        if (c == '\\') {
            ++i;
        } else if (c == '[') {
            for (++i; i < pattern.size() && pattern[i] != ']'; ++i) {
                if (pattern[i] == '\\') {
                    ++i;
                }
            }
        } else if (c == '(') {
            const std::string_view head = pattern.substr(i, 3);
            if (head == "(?=" || head == "(?!") {
                return invalid_pattern("lookahead is not supported.");
            }
            groups.push_back(0);
            last = 0;
            continue;
        } else if (c == ')' && groups.size() > 1) {
            atom = groups.back() + 2; // Lo de dentro más sus estados de inicio y de fin.
            groups.pop_back();
        } else if (c == '*' || c == '+' || c == '?') {
            groups.back() += 1;
            continue;
        } else if (c == '|') {
            continue;
        } else if (c == '{') {
            if (const auto repeat = counted_repeat(pattern, i)) {
                if (repeat->first > kMaxRepeat) {
                    return too_complex();
                }
                if (repeat->first > 0) {
                    groups.back() += last * (repeat->first - 1);
                    last *= repeat->first;
                }
                i = repeat->second;
                if (groups.back() > kMaxPatternWeight) {
                    return too_complex();
                }
                continue;
            }
        }
        groups.back() += atom;
        last = atom;
        if (groups.back() > kMaxPatternWeight) {
            return too_complex();
        }
    }
    return std::nullopt;
}

/// La expresión para buscar línea por línea, o el error. __polynomial (extensión de libstdc++,
/// la de GCC, Clang y MinGW aquí) elige el ejecutor que simula el autómata en vez del que
/// retrocede: "(a+)+$" ya no es exponencial (las referencias hacia atrás lanzan
/// error_complexity). Aun así regex_search lo intentaría desde cada posición (cuadrático), así
/// que se busca con "[\s\S]*(?:patrón)" desde el inicio solamente (match_continuous): una sola
/// pasada, O(línea × autómata). En una línea sola, ^ y $ siguen siendo su inicio y su final.
std::variant<std::regex, ToolResult> compile_pattern(const std::string& pattern,
                                                     bool ignore_case) {
    if (std::optional<ToolResult> problem = pattern_problem(pattern)) {
        return std::move(*problem);
    }
    // nosubs: solo importa si hay coincidencia; con grupos que capturan, el ejecutor copiaría sus
    // resultados en cada estado y cada carácter.
    std::regex::flag_type flags =
        std::regex::ECMAScript | std::regex::nosubs | std::regex_constants::__polynomial;
    if (ignore_case) {
        flags |= std::regex::icase;
    }
    try {
        // Primero solo: un ")" de más cerraría el grupo de abajo y daría otro patrón válido.
        const std::regex alone{pattern, flags};
        return std::regex{"[\\s\\S]*(?:" + pattern + ")", flags};
    } catch (const std::regex_error& error) {
        if (error.code() == std::regex_constants::error_complexity) {
            return invalid_pattern("backreferences are not supported.");
        }
        return invalid_pattern(std::string{error.what()} + ".");
    }
}

/// Las coincidencias de un archivo de texto; nullopt si se canceló.
std::optional<FileHits> search_text(std::string_view bytes, const std::regex& regex,
                                    const Query& query, const CancelToken& cancel) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start < bytes.size();) {
        std::size_t end = bytes.find('\n', start);
        end = end == std::string_view::npos ? bytes.size() : end;
        lines.push_back(bytes.substr(start, end - start));
        start = end + 1;
    }
    std::vector<std::size_t> matched;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (cancel.is_cancelled()) {
            return std::nullopt;
        }
        const std::string_view line = lines[i];
        if (line.size() > kMaxLineBytes ||
            !std::regex_search(line.begin(), line.end(), regex,
                               std::regex_constants::match_continuous)) {
            continue;
        }
        matched.push_back(i);
        if (query.mode == Mode::Files) {
            break;
        }
    }
    FileHits hits;
    hits.matches = matched.size();
    if (query.mode == Mode::Content) {
        std::size_t emitted = 0; // Las líneas antes de esta ya salieron.
        for (const std::size_t m : matched) {
            const std::size_t from = std::max(m - std::min(m, query.context), emitted);
            const std::size_t to = std::min(lines.size(), m + query.context + 1);
            for (std::size_t j = from; j < to; ++j) {
                hits.lines.push_back(Line{j + 1,
                                          std::binary_search(matched.begin(), matched.end(), j),
                                          std::string{lines[j]}});
            }
            emitted = std::max(emitted, to);
        }
    }
    return hits;
}

/// std::regex sobre los archivos de la lista del proyecto (la misma de glob), leídos con
/// read_file. Se detiene al pasar el tope: con eso basta para avisar que hay más.
Outcome search_own(const Query& query, ToolContext& context,
                   const std::optional<std::string>& git_path) {
    std::variant<std::regex, ToolResult> compiled =
        compile_pattern(query.pattern, query.ignore_case);
    if (auto* error = std::get_if<ToolResult>(&compiled)) {
        return std::move(*error);
    }
    const std::regex& regex = std::get<std::regex>(compiled);

    std::vector<WalkFile> files;
    if (query.base_is_file) {
        if (query.keeps(query.base)) {
            files.push_back({query.base, context.root.display(query.base)});
        }
    } else {
        ProjectFiles listed = list_project_files(
            context, query.base,
            [&query](std::string_view relative) { return query.keeps_relative(relative); },
            git_path);
        if (const auto* error = std::get_if<PathError>(&listed)) {
            return path_error_result(*error);
        }
        files = std::move(std::get<std::vector<WalkFile>>(listed));
    }
    std::sort(files.begin(), files.end(), [](const WalkFile& a, const WalkFile& b) {
        return path_less(a.display, b.display);
    });

    std::vector<FileHits> found;
    std::size_t total = 0;
    for (const WalkFile& file : files) {
        if (context.cancel.is_cancelled()) {
            return cancelled_result();
        }
        const ReadResult read = context.root.read_file(file.path, kMaxFileBytes);
        const auto* content = std::get_if<FileContent>(&read);
        if (content == nullptr || content->truncated ||
            content->bytes.find('\0') != std::string::npos) {
            continue;
        }
        std::optional<FileHits> hits = search_text(content->bytes, regex, query, context.cancel);
        if (!hits) {
            return cancelled_result();
        }
        if (hits->matches == 0) {
            continue;
        }
        hits->display = file.display;
        total += units(*hits, query.mode);
        found.push_back(std::move(*hits));
        if (total > kMaxResults) {
            break;
        }
    }
    if (context.cancel.is_cancelled()) {
        return cancelled_result();
    }
    return found;
}

// ---- rg ------------------------------------------------------------------------------------

std::string decode_base64(std::string_view text) {
    std::string out;
    unsigned int buffer = 0;
    int bits = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            value = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            value = c - '0' + 52;
        } else if (c == '+') {
            value = 62;
        } else if (c == '/') {
            value = 63;
        }
        if (value < 0) {
            continue; // '=' de relleno.
        }
        buffer = (buffer << 6U) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> static_cast<unsigned int>(bits)) & 0xFFU);
            buffer &= (1U << static_cast<unsigned int>(bits)) - 1U;
        }
    }
    return out;
}

/// data[key] de un mensaje de rg: {"text": "..."} o, si no es UTF-8, {"bytes": "<base64>"}.
std::optional<std::string> rg_data(const json& data, const char* key) {
    const auto field = data.find(key);
    if (field == data.end() || !field->is_object()) {
        return std::nullopt;
    }
    if (const auto text = field->find("text"); text != field->end() && text->is_string()) {
        return text->get<std::string>();
    }
    if (const auto bytes = field->find("bytes"); bytes != field->end() && bytes->is_string()) {
        return decode_base64(bytes->get<std::string>());
    }
    return std::nullopt;
}

struct RgParsed {
    std::vector<FileHits> files;
    std::size_t units = 0;
    bool complete = false; ///< Llegó el mensaje "summary": rg terminó de buscar.
};

/// La salida de rg --json (un mensaje por línea): match y context de un archivo, luego su end.
/// Todo es texto ajeno: cada ruta pasa por resolve() y la que falla se salta; un archivo con
/// binary_offset (rg vio un NUL) se descarta entero, como en la búsqueda propia.
RgParsed parse_rg(std::string_view out, const Query& query, const ProjectRoot& root) {
    RgParsed parsed;
    FileHits current;
    for (std::size_t start = 0; start < out.size();) {
        std::size_t end = out.find('\n', start);
        end = end == std::string_view::npos ? out.size() : end;
        const json message = json::parse(out.substr(start, end - start), nullptr, false);
        start = end + 1;
        if (!message.is_object()) {
            continue; // También la última línea, cortada por el tope de salida.
        }
        const auto type_field = message.find("type");
        const auto data_field = message.find("data");
        if (type_field == message.end() || !type_field->is_string() ||
            data_field == message.end() || !data_field->is_object()) {
            continue;
        }
        const std::string type = type_field->get<std::string>();
        const json& data = *data_field;
        if (type == "summary") {
            parsed.complete = true;
        } else if (type == "match" || type == "context") {
            std::optional<std::string> text = rg_data(data, "lines");
            const auto number = data.find("line_number");
            if (!text || number == data.end() || !number->is_number_unsigned()) {
                continue;
            }
            if (!text->empty() && text->back() == '\n') {
                text->pop_back();
            }
            const bool match = type == "match";
            if (match && text->size() > kMaxLineBytes) {
                continue; // La búsqueda propia no busca en líneas así.
            }
            current.matches += match ? 1 : 0;
            if (query.mode == Mode::Content) {
                current.lines.push_back(
                    Line{number->get<std::uint64_t>(), match, std::move(*text)});
            }
        } else if (type == "end") {
            FileHits file = std::exchange(current, FileHits{});
            const auto binary = data.find("binary_offset");
            const std::optional<std::string> path = rg_data(data, "path");
            if (file.matches == 0 || !path || (binary != data.end() && !binary->is_null())) {
                continue;
            }
            const ResolvedPath resolved = root.resolve(*path);
            const auto* canonical = std::get_if<fs::path>(&resolved);
            if (canonical == nullptr || !query.keeps(*canonical)) {
                continue;
            }
            file.display = root.display(*canonical);
            parsed.units += units(file, query.mode);
            parsed.files.push_back(std::move(file));
        }
    }
    return parsed;
}

/// text como glob literal de rg (globset: '\' escapa cualquier carácter). nullopt si trae
/// controles o no es UTF-8: eso no se puede escribir con seguridad.
std::optional<std::string> glob_literal(std::string_view text) {
    if (!utf8::is_valid(text)) {
        return std::nullopt;
    }
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20U || byte == 0x7FU) {
            return std::nullopt;
        }
        const bool plain = byte >= 0x80U || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '-' || c == '_';
        if (!plain) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

/// path es dir o está dentro (rutas absolutas con '/').
bool is_under(std::string_view path, std::string_view dir) {
    return dir == "/" || path == dir ||
           (path.starts_with(dir) && path.size() > dir.size() && path[dir.size()] == '/');
}

/// Las exclusiones (--glob !…) de las carpetas bloqueadas que están dentro de la raíz, para que
/// rg ni las lea; resolve() en parse_rg sigue siendo la segunda barrera. rg compara sus globs con
/// la ruta sin el prefijo de su carpeta actual (la nuestra) si lo tiene, y si no, con la ruta
/// absoluta (comprobado con ripgrep 15.1): por eso "!/<relativa a la carpeta actual>" cuando la
/// bloqueada queda debajo de ella, y siempre "!**/<absoluta>". nullopt (búsqueda propia) si un
/// nombre no se puede escribir como glob o si la carpeta actual está dentro de una bloqueada.
std::optional<std::vector<std::string>> blocked_globs(const ProjectRoot& root) {
    std::vector<std::string> globs;
    const std::vector<std::string> inside = root.blocked_within();
    if (inside.empty()) {
        return globs;
    }
    std::error_code error;
    const std::string cwd = utf8_text(fs::current_path(error));
    const std::string root_text = utf8_text(root.path());
    for (const std::string& relative : inside) {
        const std::string absolute = root_text + "/" + relative;
        if (!error && is_under(cwd, absolute)) {
            return std::nullopt;
        }
        const std::optional<std::string> anywhere = glob_literal(absolute.substr(1));
        if (!anywhere) {
            return std::nullopt;
        }
        std::vector<std::string> patterns{"**/" + *anywhere};
        if (!error && is_under(absolute, cwd)) {
            const std::optional<std::string> below =
                glob_literal(absolute.substr(cwd == "/" ? 1 : cwd.size() + 1));
            if (!below) {
                return std::nullopt;
            }
            patterns.push_back("/" + *below);
        }
        for (const std::string& pattern : patterns) {
            globs.push_back("!" + pattern);
            globs.push_back("!" + pattern + "/**");
        }
    }
    return globs;
}

/// rg --json, sin shell. nullopt: rg no sirvió y toca la búsqueda propia.
/// Por qué --json y no texto: las rutas pueden traer ':' o saltos de línea, y una línea puede no
/// ser UTF-8; en JSON llegan sin ambigüedad (texto o base64), con el número de línea aparte, el
/// final de cada archivo (con binary_offset si vio un NUL) y un "summary" que dice que la salida
/// está completa. Si el tope de 1 MiB la corta, la última línea no parsea y se ignora.
std::optional<Outcome> search_rg(const Query& query, ToolContext& context,
                                 const std::string& rg) {
    std::optional<std::vector<std::string>> excluded = blocked_globs(context.root);
    if (!excluded) {
        return std::nullopt;
    }
    std::vector<std::string> argv{rg,
                                  "--json",
                                  "--no-config",
                                  "--sort",
                                  "path",
                                  "--hidden",
                                  "--glob",
                                  "!.git",
                                  "--max-filesize",
                                  "1M",
                                  query.ignore_case ? "--ignore-case" : "--case-sensitive"};
    for (std::string& glob : *excluded) {
        argv.push_back("--glob");
        argv.push_back(std::move(glob));
    }
    if (query.mode == Mode::Content && query.context > 0) {
        argv.push_back("--context");
        argv.push_back(std::to_string(query.context));
    }
    argv.push_back("--");
    argv.push_back(query.pattern);
    argv.push_back(utf8_text(query.base));

    const process::ProcessResult result =
        context.run_process(argv, helper_options(kRgTimeout, context.cancel));
    if (context.cancel.is_cancelled() ||
        result.status == process::ProcessResult::Status::Cancelled) {
        return Outcome{cancelled_result()};
    }
    if (result.status != process::ProcessResult::Status::Exited) {
        return std::nullopt;
    }
    if (result.exit_code == 2 && result.err.find("regex parse error") != std::string::npos) {
        return Outcome{invalid_pattern(shorten(result.err, kMaxShownErrorBytes))};
    }
    RgParsed parsed = parse_rg(result.out, query, context.root);
    const bool truncated = result.out.size() >= process::kMaxProcessOutput;
    // 0: hubo coincidencias; 1: ninguna; 2: algún error, pero con "summary" terminó de buscar.
    const bool finished =
        parsed.complete || ((result.exit_code == 0 || result.exit_code == 1) && !truncated);
    if (!finished && !(truncated && parsed.units > kMaxResults)) {
        return std::nullopt;
    }
    return Outcome{std::move(parsed.files)};
}

// ---- Resultado -----------------------------------------------------------------------------

std::string files_es(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " archivo" : " archivos");
}

std::string matches_es(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " coincidencia" : " coincidencias");
}

ToolResult format_hits(std::vector<FileHits> files, const Query& query,
                       const std::string& where) {
    if (files.empty()) {
        return ToolResult{"No matches for the pattern \"" + shorten(query.pattern, kMaxShownBytes) +
                              "\" in " + where,
                          "Sin coincidencias", ToolStatus::Ok};
    }
    std::stable_sort(files.begin(), files.end(), [](const FileHits& a, const FileHits& b) {
        return path_less(a.display, b.display);
    });
    std::size_t total = 0;
    for (const FileHits& file : files) {
        total += units(file, query.mode);
    }
    const bool more = total > kMaxResults;

    std::string text;
    std::size_t shown = 0;
    std::size_t matches = 0;
    if (query.mode == Mode::Content) {
        bool stop = false;
        for (auto file = files.begin(); file != files.end() && !stop; ++file) {
            std::optional<std::uint64_t> previous;
            for (const Line& line : file->lines) {
                const bool contiguous = previous && line.number == *previous + 1;
                // Al llegar al tope, solo el contexto que sigue a la última coincidencia.
                if (shown == kMaxResults && (line.match || !contiguous)) {
                    stop = true;
                    break;
                }
                if (query.context > 0 && !text.empty() && !contiguous) {
                    text += "--\n";
                }
                const char separator = line.match ? ':' : '-';
                text += file->display;
                text += separator;
                text += std::to_string(line.number);
                text += separator;
                text += shown_line(line.text, kMaxShownLineChars);
                text += '\n';
                previous = line.number;
                shown += line.match ? 1 : 0;
            }
        }
        matches = shown;
    } else {
        for (const FileHits& file : files) {
            if (shown == kMaxResults) {
                break;
            }
            text += file.display;
            if (query.mode == Mode::Count) {
                text += ':' + std::to_string(file.matches);
            }
            text += '\n';
            ++shown;
            matches += file.matches;
        }
    }

    std::string display;
    if (query.mode == Mode::Content) {
        display = more ? "más de " + matches_es(kMaxResults) : matches_es(matches);
    } else if (more) {
        display = "más de " + files_es(kMaxResults);
    } else {
        display = query.mode == Mode::Files ? files_es(shown)
                                            : matches_es(matches) + " en " + files_es(shown);
    }
    if (more) {
        text += "\n(Showing the first " + std::to_string(kMaxResults) +
                (query.mode == Mode::Content ? " matches" : " files") +
                "; there are more. Narrow the pattern, the path or the glob to see the rest.)";
    } else {
        text.pop_back();
    }
    return ToolResult{std::move(text), std::move(display), ToolStatus::Ok};
}

std::string summarize_grep(const json& arguments) {
    std::string summary =
        "\"" + shorten(arguments.at("pattern").get<std::string>(), kMaxShownBytes) + "\"";
    if (arguments.contains("path")) {
        summary += " en " + shorten(arguments.at("path").get<std::string>(), kMaxShownBytes);
    }
    return summary;
}

} // namespace

ToolResult run_grep(const json& arguments, ToolContext& context,
                    const std::optional<std::string>& rg_path,
                    const std::optional<std::string>& git_path) {
    Query query;
    query.pattern = arguments.at("pattern").get<std::string>();
    query.ignore_case = arguments.value("ignore_case", false);
    const std::string mode = arguments.value("output_mode", std::string{"files"});
    query.mode = mode == "content" ? Mode::Content : mode == "count" ? Mode::Count : Mode::Files;
    query.context =
        static_cast<std::size_t>(std::min(arguments.value("context", std::int64_t{0}), kMaxContext));
    if (query.pattern.size() > kMaxPatternBytes) {
        return invalid_pattern("the pattern is longer than " + std::to_string(kMaxPatternBytes) +
                               " bytes.");
    }

    query.base = context.root.path();
    if (arguments.contains("path")) {
        const ResolvedPath resolved = context.root.resolve(arguments.at("path").get<std::string>());
        if (const auto* error = std::get_if<PathError>(&resolved)) {
            return path_error_result(*error);
        }
        query.base = std::get<fs::path>(resolved);
        std::error_code error;
        query.base_is_file = fs::is_regular_file(query.base, error);
        if (!query.base_is_file && !fs::is_directory(query.base, error)) {
            return ToolResult{"Path does not exist or is not a file or directory: " +
                                  context.root.display(query.base),
                              "No existe", ToolStatus::Error};
        }
    }
    query.glob_base = query.base_is_file ? query.base.parent_path() : query.base;
    if (arguments.contains("glob")) {
        query.glob.emplace(arguments.at("glob").get<std::string>(), current_os() == Os::Windows);
    }

    std::optional<Outcome> outcome;
    try {
        // Un NUL no cabe en argv: ese patrón solo con la búsqueda propia.
        if (rg_path && query.pattern.find('\0') == std::string::npos) {
            outcome = search_rg(query, context, *rg_path);
        }
        if (!outcome) {
            outcome = search_own(query, context, git_path);
        }
    } catch (const std::regex_error&) {
        return too_complex(); // error_stack o error_complexity al buscar.
    }
    if (auto* result = std::get_if<ToolResult>(&*outcome)) {
        return std::move(*result);
    }
    return format_hits(std::move(std::get<std::vector<FileHits>>(*outcome)), query,
                       context.root.display(query.base));
}

std::shared_ptr<const Tool> make_grep_tool() {
    return std::make_shared<const Tool>(Tool{
        "grep", std::string{kDescription}, json::parse(kSchema), "Buscar", summarize_grep,
        [](const json& arguments, ToolContext& context) {
            return run_grep(arguments, context, find_program("rg"), find_program("git"));
        }});
}

} // namespace chatbot::code
