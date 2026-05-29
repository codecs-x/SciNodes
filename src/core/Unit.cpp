#include "Unit.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace scinodes {

namespace {

// Tabla de unidades reconocidas por el parser.  Los exponentes están
// en (m, kg, s, A, K, mol, cd).  La magnitud es el factor hacia SI base
// canónica (m, kg, s, A, K, mol, cd y sus combinaciones).
//
// Esta tabla NO impone orden; el parser hace longest-match en cada
// posición y elige el símbolo más largo que coincida.  rad/deg/rpm
// pueden parecer redundantes pero distinguen por magnitud (deg = π/180,
// rpm = 2π/60) — la gramática los lee como tres símbolos canónicos,
// dimensionalmente compatibles cuando lo son.
struct UnitEntry {
    std::string_view symbol;
    Unit             value;
};

const std::vector<UnitEntry>& unitTable() {
    // Notación: cada Unit::exp tiene 8 posiciones desde etapa 6I.L:
    //   [0]=m  [1]=kg  [2]=s  [3]=A  [4]=K  [5]=mol  [6]=cd  [7]=rad
    // El "rad" exponent es la 8ª dim "fantasma" — el SI puro la trata
    // como adimensional, pero la mantenemos en el análisis para que
    // rad/s sea distinguible de Hz.
    static const std::vector<UnitEntry> t = {
        // Multi-carácter primero (longest-match favorece estos).
        { "rad", Unit{ {0,0,0,0,0,0,0,1}, 1.0 } },               // angle base
        { "mol", Unit{ {0,0,0,0,0,1,0,0}, 1.0 } },
        { "deg", Unit{ {0,0,0,0,0,0,0,1}, 3.14159265358979323846 / 180.0 } },
        { "rpm", Unit{ {0,0,-1,0,0,0,0,1}, 2.0 * 3.14159265358979323846 / 60.0 } },
        { "Hz",  Unit{ {0,0,-1,0,0,0,0,0}, 1.0 } },                // s⁻¹ sin angle
        { "Pa",  Unit{ {-1,1,-2,0,0,0,0,0}, 1.0 } },
        { "kg",  Unit{ {0,1,0,0,0,0,0,0}, 1.0 } },
        { "cd",  Unit{ {0,0,0,0,0,0,1,0}, 1.0 } },
        // ASCII-first: el display elige el primer match del table, así
        // que "Ohm" antes que "Ω" rinde el nombre tipeable.  Ambos
        // siguen siendo parseables para mantener compatibilidad con
        // .scn previos que escribían el símbolo griego.
        { "Ohm", Unit{ {2,1,-3,-2,0,0,0,0}, 1.0 } },              // ASCII canónico
        { "Ω",   Unit{ {2,1,-3,-2,0,0,0,0}, 1.0 } },              // alias U+03A9
        // Single-character bases SI + derivados.
        { "m",   Unit{ {1,0,0,0,0,0,0,0}, 1.0 } },
        { "g",   Unit{ {0,1,0,0,0,0,0,0}, 1e-3 } },                // gram = 1e-3 kg
        { "s",   Unit{ {0,0,1,0,0,0,0,0}, 1.0 } },
        { "A",   Unit{ {0,0,0,1,0,0,0,0}, 1.0 } },
        { "K",   Unit{ {0,0,0,0,1,0,0,0}, 1.0 } },
        { "V",   Unit{ {2,1,-3,-1,0,0,0,0}, 1.0 } },
        { "W",   Unit{ {2,1,-3,0,0,0,0,0}, 1.0 } },
        { "J",   Unit{ {2,1,-2,0,0,0,0,0}, 1.0 } },
        { "N",   Unit{ {1,1,-2,0,0,0,0,0}, 1.0 } },
        { "T",   Unit{ {0,1,-2,-1,0,0,0,0}, 1.0 } },
        { "H",   Unit{ {2,1,-2,-2,0,0,0,0}, 1.0 } },
        { "C",   Unit{ {0,0,1,1,0,0,0,0}, 1.0 } },
        { "F",   Unit{ {-2,-1,4,2,0,0,0,0}, 1.0 } },
    };
    return t;
}

// Tabla de prefijos SI.  El parser busca un prefijo SÓLO cuando el
// reconocimiento directo del símbolo falla — evita ambigüedad para
// "m" (meter) vs "milli".  "da" antes que "d" para longest-match.
struct PrefixEntry {
    std::string_view symbol;
    double           factor;
};

const std::vector<PrefixEntry>& prefixTable() {
    static const std::vector<PrefixEntry> t = {
        { "da", 1e1 },
        { "Y",  1e24 }, { "Z", 1e21 }, { "E", 1e18 }, { "P", 1e15 },
        { "T",  1e12 }, { "G", 1e9  }, { "M", 1e6  }, { "k", 1e3  },
        { "h",  1e2  },
        { "d",  1e-1 }, { "c", 1e-2 }, { "m", 1e-3 },
        { "μ",  1e-6 }, { "u", 1e-6 },                          // U+03BC + ASCII alias
        { "n",  1e-9 }, { "p", 1e-12 }, { "f", 1e-15 },
        { "a",  1e-18 },{ "z", 1e-21 }, { "y", 1e-24 },
    };
    return t;
}

// `·` (U+00B7) en UTF-8 ocupa 2 bytes: 0xC2 0xB7.  Lo aceptamos junto
// al ASCII `*`.  Esta constante centraliza el byte-stream.
constexpr std::string_view kCenterDot = "\xC2\xB7";

class UnitParser {
public:
    UnitParser(std::string_view t) : text(t) {}

    ParseUnitResult parse() {
        skipWhitespace();
        if (pos >= text.size()) {
            return { Unit{}, "Empty unit expression" };
        }
        Unit u = parseExpr();
        skipWhitespace();
        if (!errorMsg.empty()) return { Unit{}, errorMsg };
        if (pos < text.size()) {
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                "Unexpected character at position %zu: '%c'",
                pos, text[pos]);
            return { Unit{}, std::string(buf) };
        }
        return { u, "" };
    }

private:
    std::string_view text;
    size_t           pos = 0;
    std::string      errorMsg;

    // expr ::= term ( ('·' | '*') term | '/' term )*
    Unit parseExpr() {
        Unit u = parseTerm();
        while (errorMsg.empty()) {
            skipWhitespace();
            if (consume(kCenterDot) || consumeChar('*')) {
                skipWhitespace();
                u = u * parseTerm();
            } else if (consumeChar('/')) {
                skipWhitespace();
                u = u / parseTerm();
            } else break;
        }
        return u;
    }

    // term ::= base ('^' integer)?
    Unit parseTerm() {
        Unit b = parseBase();
        skipWhitespace();
        if (consumeChar('^')) {
            int n = 0;
            if (!parseInteger(n)) {
                setError("Expected integer after '^'");
                return Unit{};
            }
            return b.pow(n);
        }
        return b;
    }

    // base ::= ( prefix? unit_name ) | '(' expr ')'
    Unit parseBase() {
        skipWhitespace();
        if (consumeChar('(')) {
            Unit u = parseExpr();
            skipWhitespace();
            if (!consumeChar(')')) {
                setError("Expected ')'");
                return Unit{};
            }
            return u;
        }

        // 1. Try direct unit-name match (longest first).
        size_t directLen = 0;
        Unit   directUnit;
        for (const auto& e : unitTable()) {
            if (e.symbol.size() > directLen && startsWith(e.symbol)) {
                directLen = e.symbol.size();
                directUnit = e.value;
            }
        }
        // Aceptamos el match directo sii lo que sigue es operador,
        // paréntesis cierre o fin de string — sin esto "ms" parsearía
        // como "m" (meter) dejando "s" sin operador y disparando error.
        if (directLen > 0) {
            size_t afterDirect = pos + directLen;
            if (isBoundary(afterDirect)) {
                pos = afterDirect;
                return directUnit;
            }
        }

        // 2. Try prefix + unit-name.
        for (const auto& pfx : prefixTable()) {
            if (!startsWith(pfx.symbol)) continue;
            size_t afterPfx = pos + pfx.symbol.size();
            // Buscar unit-name después del prefijo (longest first).
            size_t nameLen = 0;
            Unit   nameUnit;
            for (const auto& e : unitTable()) {
                if (e.symbol.size() > nameLen &&
                    afterPfx + e.symbol.size() <= text.size() &&
                    text.substr(afterPfx, e.symbol.size()) == e.symbol) {
                    nameLen = e.symbol.size();
                    nameUnit = e.value;
                }
            }
            if (nameLen > 0 && isBoundary(afterPfx + nameLen)) {
                Unit u = nameUnit;
                u.magnitude *= pfx.factor;
                pos = afterPfx + nameLen;
                return u;
            }
        }

        // Si llegamos acá, no reconocimos nada.
        char buf[80];
        std::snprintf(buf, sizeof(buf),
            "Unknown unit symbol at position %zu", pos);
        setError(std::string(buf));
        return Unit{};
    }

    bool parseInteger(int& out) {
        size_t start = pos;
        bool   neg   = false;
        if (pos < text.size() && text[pos] == '-') { neg = true; ++pos; }
        size_t digitStart = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            ++pos;
        if (pos == digitStart) { pos = start; return false; }
        long val = 0;
        for (size_t i = digitStart; i < pos; ++i)
            val = val * 10 + (text[i] - '0');
        out = static_cast<int>(neg ? -val : val);
        return true;
    }

    void skipWhitespace() {
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' ||
                text[pos] == '\n' || text[pos] == '\r')) ++pos;
    }

    bool startsWith(std::string_view s) const {
        return text.size() - pos >= s.size() &&
               text.substr(pos, s.size()) == s;
    }

    bool consume(std::string_view s) {
        if (!startsWith(s)) return false;
        pos += s.size();
        return true;
    }

    bool consumeChar(char c) {
        if (pos < text.size() && text[pos] == c) { ++pos; return true; }
        return false;
    }

    // ¿La posición `p` es un "boundary" — operador, fin de string,
    // paréntesis o whitespace?  Determina si un match de unit-name
    // puede aceptarse o necesita extenderse (prefix+name).
    bool isBoundary(size_t p) const {
        if (p >= text.size()) return true;
        char c = text[p];
        if (c == '*' || c == '/' || c == '^' ||
            c == '(' || c == ')' ||
            c == ' ' || c == '\t' || c == '\n' || c == '\r') return true;
        // Centro de '·' UTF-8 (0xC2 0xB7).
        if (static_cast<unsigned char>(c) == 0xC2 &&
            p + 1 < text.size() &&
            static_cast<unsigned char>(text[p+1]) == 0xB7) return true;
        return false;
    }

    void setError(const std::string& msg) {
        if (errorMsg.empty()) errorMsg = msg;
    }
};

}  // anon

ParseUnitResult parseUnit(std::string_view text) {
    return UnitParser{text}.parse();
}

double prefixFactor(std::string_view sym) {
    for (const auto& pfx : prefixTable())
        if (sym == pfx.symbol) return pfx.factor;
    return 0.0;
}

// ---------------------------------------------------------------------------
// toCanonicalString — algoritmo del display canónico (etapa 6C).
//
// Política:
//   1. Adimensional + mag=1   → ""
//   2. Adimensional + mag≠1   → distinguir rad/deg/etc. si conocido;
//                                sino "(×factor)".
//   3. exp matches named unit → "<prefix?><symbol>".
//   4. Decomposición genérica de SI bases ordenadas.
//
// El paso 3 reutiliza la misma tabla que el parser — coherencia total
// entre lectura y escritura.  Sin esto, el round-trip
// parseUnit(toCanonicalString(u)).unit puede no equivaler a u — bug
// que evitamos por construcción.
// ---------------------------------------------------------------------------
namespace {

// Lista de prefijos en orden de magnitud decreciente — el primero que
// matche es el más cercano.  Excluye magnitudes muy lejos del usuario
// promedio (Y, Z, E, ...) en preferencia visual; siguen disponibles
// para el parser, no para el display.
const std::vector<PrefixEntry>& displayPrefixes() {
    // Display debe espejar la tabla del parser: si parseUnit acepta
    // "fm" (femto-meter), toCanonicalString del Unit equivalente debe
    // devolver "fm" — sin eso, el round-trip queda con factor explícito
    // "(×1e-15)m" en vez del prefijo SI.  Orden por magnitud
    // decreciente — `displayPrefixes` busca el primer match.
    static const std::vector<PrefixEntry> t = {
        { "Y",  1e24 }, { "Z", 1e21 }, { "E", 1e18 }, { "P", 1e15 },
        { "T",  1e12 }, { "G", 1e9  }, { "M", 1e6  }, { "k", 1e3  },
        { "h",  1e2  }, { "da", 1e1 },
        { "d",  1e-1 }, { "c",  1e-2 }, { "m", 1e-3 },
        { "μ",  1e-6 },
        { "n",  1e-9 }, { "p",  1e-12 }, { "f", 1e-15 },
        { "a",  1e-18 },{ "z",  1e-21 }, { "y", 1e-24 },
    };
    return t;
}

bool approxEqual(double a, double b, double rel = 1e-9) {
    if (a == b) return true;
    double scale = std::max(std::fabs(a), std::fabs(b));
    return std::fabs(a - b) <= rel * scale;
}

// Símbolos canónicos para las dimensiones SI base, en orden de display
// (m, kg, s, A, K, mol, cd).
constexpr const char* kBaseSymbol[8] = { "m", "kg", "s", "A", "K", "mol", "cd", "rad" };

}  // anon

std::string Unit::toCanonicalString() const {
    // 1. Adimensional.
    if (isDimensionless()) {
        if (approxEqual(magnitude, 1.0)) return "";
        if (approxEqual(magnitude, 3.14159265358979323846 / 180.0)) return "deg";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "(×%g)", magnitude);
        return std::string(buf);
    }

    // 2. Exact named unit match.  Estrategia: PREFERIMOS los símbolos
    // con magnitud SI canónica (1.0) — son los nombres oficiales
    // (Hz, V, W, ...).  rpm y deg comparten exp con Hz y rad pero
    // tienen magnitud distinta; sólo los devolvemos si la magnitud
    // coincide exactamente con la suya.
    //
    // Dos pasadas:
    //   (a) Símbolos con e.value.magnitude == 1.0 (SI canonical):
    //       intentamos magnitud exacta o vía prefix del display.
    //   (b) Símbolos no-canónicos (deg, rpm, ...): sólo aceptamos si
    //       la magnitud coincide exactamente.
    for (const auto& e : unitTable()) {
        if (e.value.exp != exp) continue;
        if (!approxEqual(e.value.magnitude, 1.0)) continue;   // sólo SI base
        if (approxEqual(magnitude, 1.0)) {
            return std::string(e.symbol);
        }
        for (const auto& pfx : displayPrefixes()) {
            if (approxEqual(magnitude, pfx.factor)) {
                return std::string(pfx.symbol) + std::string(e.symbol);
            }
        }
        // Magnitud no encaja a prefix conocido; sigue buscando otra
        // entrada (rare; ej. exp = Hz pero mag = 2π).  Si ninguna
        // matchea, cae a decomposición.
    }
    // Pasada (b): no-canónicos exactos.
    for (const auto& e : unitTable()) {
        if (e.value.exp != exp) continue;
        if (approxEqual(magnitude, e.value.magnitude)) {
            return std::string(e.symbol);
        }
    }

    // (c) Búsqueda algebraica del display MÁS CORTO entre compuestos
    // de unidades nombradas y la decomposición SI genérica.
    // No es "base de datos de unidades permitidas" — es composición
    // gramatical: probamos cada par/triple de unidades del unitTable
    // y vemos cuál combinación algebraica iguala la dim+mag objetivo.
    // De todas las que matchean, devolvemos la más corta en
    // caracteres.  Cubre casos como V·s/rad, N·m/A, m^2/s, etc., sin
    // privilegiar un nombre derivado sobre otro: si J y N·m igualan,
    // J gana por ser más corto.
    //
    // No usamos prefijos en compuestos (kV·s, etc.) — la combinatoria
    // explota y los prefijos individuales se cubren en pasada (a).
    {
        const auto& tab = unitTable();
        // Cuenta caracteres VISUALES (UTF-8 code points), no bytes —
        // "·" es 2 bytes pero 1 carácter al ojo.
        auto visualLen = [](const std::string& s) -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < s.size(); ) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if      (c < 0x80) ++i;
                else if ((c & 0xE0) == 0xC0) i += 2;
                else if ((c & 0xF0) == 0xE0) i += 3;
                else                          i += 4;
                ++n;
            }
            return n;
        };
        // Dignidad por unidad — menor = más preferido en display.
        // Base SI = 0, derivadas comunes (V/N/J/W/Pa/Hz) = 1, exóticas
        // (Ω/T/H/C/F) = 2.  El score total de un candidato es la suma
        // sobre las unidades usadas; ante empate de longitud visual,
        // gana la de menor score (V·s gana sobre Ω·C, m²·kg gana sobre
        // J·H·F).
        auto dignity = [](std::string_view sym) -> int {
            if (sym == "m" || sym == "kg" || sym == "s" || sym == "A"
             || sym == "K" || sym == "mol" || sym == "cd" || sym == "rad")
                return 0;
            if (sym == "V" || sym == "N" || sym == "J" || sym == "W"
             || sym == "Pa" || sym == "Hz" || sym == "g")
                return 1;
            // Ohm, Ω, T, H, C, F, deg, rpm, ...
            return 2;
        };
        struct Candidate { std::string text; int score; size_t vLen; };
        Candidate best;
        // Costo efectivo: vLen + score × 2.  Cada unidad de dignidad
        // alta agrega 2 chars virtuales.  Equilibra "más corto" con
        // "más legible": V·s (3+1·2=5) gana sobre m^2·kg/(s^2·A)
        // (~13+0=13); m^2·kg (6+0=6) gana sobre J·H·F (5+5·2=15).
        auto effective = [](size_t L, int score) { return L + size_t(score) * 2; };
        auto consider = [&](std::string&& candidate, const Unit& result,
                            int score) {
            if (result.exp != exp) return;
            if (!approxEqual(result.magnitude, magnitude)) return;
            size_t L = visualLen(candidate);
            const bool replace =
                best.text.empty()
                || effective(L, score) < effective(best.vLen, best.score);
            if (replace) best = { std::move(candidate), score, L };
        };
        // Helper: string_view → string para operator+.
        auto S = [](std::string_view sv) { return std::string(sv); };
        // 2-unit: u1·u2, u1/u2.  Filtramos s1==s2 — duplicados se
        // expresan mejor como s^2 via el genérico (m·m vs m^2).
        for (const auto& e1 : tab) {
            if (!approxEqual(e1.value.magnitude, 1.0)) continue;
            int d1 = dignity(e1.symbol);
            for (const auto& e2 : tab) {
                if (!approxEqual(e2.value.magnitude, 1.0)) continue;
                if (e1.symbol == e2.symbol) continue;
                int score = d1 + dignity(e2.symbol);
                consider(S(e1.symbol) + "\xC2\xB7" + S(e2.symbol),
                         e1.value * e2.value, score);
                consider(S(e1.symbol) + "/" + S(e2.symbol),
                         e1.value / e2.value, score);
            }
        }
        // 3-unit: filtramos cualquier par igual (sin s·s·J etc.).
        for (const auto& e1 : tab) {
            if (!approxEqual(e1.value.magnitude, 1.0)) continue;
            int d1 = dignity(e1.symbol);
            for (const auto& e2 : tab) {
                if (!approxEqual(e2.value.magnitude, 1.0)) continue;
                if (e1.symbol == e2.symbol) continue;
                int d2 = dignity(e2.symbol);
                for (const auto& e3 : tab) {
                    if (!approxEqual(e3.value.magnitude, 1.0)) continue;
                    if (e3.symbol == e1.symbol ||
                        e3.symbol == e2.symbol) continue;
                    int score = d1 + d2 + dignity(e3.symbol);
                    consider(S(e1.symbol) + "\xC2\xB7" + S(e2.symbol)
                             + "/" + S(e3.symbol),
                             (e1.value * e2.value) / e3.value, score);
                    consider(S(e1.symbol) + "\xC2\xB7" + S(e2.symbol)
                             + "\xC2\xB7" + S(e3.symbol),
                             e1.value * e2.value * e3.value, score);
                    consider(S(e1.symbol) + "/(" + S(e2.symbol)
                             + "\xC2\xB7" + S(e3.symbol) + ")",
                             e1.value / (e2.value * e3.value), score);
                }
            }
        }
        // Construimos también la decomposición SI genérica como
        // candidato más, después elegimos el más corto entre todos.
        std::string num;
        std::string den;
        for (int i = 0; i < 8; ++i) {
            if (exp[i] == 0) continue;
            auto append = [](std::string& s, const char* sym, int e) {
                if (!s.empty()) s += "\xC2\xB7";
                s += sym;
                if (e != 1) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "^%d", e);
                    s += buf;
                }
            };
            if (exp[i] > 0) append(num, kBaseSymbol[i], exp[i]);
            else            append(den, kBaseSymbol[i], -exp[i]);
        }
        std::string generic;
        if (num.empty() && !den.empty()) generic = "1";
        else generic = num;
        if (!den.empty()) {
            generic += "/";
            if (den.find("\xC2\xB7") != std::string::npos)
                generic += "(" + den + ")";
            else
                generic += den;
        }
        // Magnitud no-1 sin nombre conocido — prefijar (×mag).
        if (!approxEqual(magnitude, 1.0)) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "(×%g)", magnitude);
            generic = std::string(buf) + generic;
        }
        // Considerar la genérica también; gana m^3 sobre m·m·m, etc.
        // Pero sólo si la magnitud del Unit es 1.0 — la genérica no
        // sabe expresar otras magnitudes salvo via "(×N)".
        auto vLen = [](const std::string& s) -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < s.size(); ) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if      (c < 0x80) ++i;
                else if ((c & 0xE0) == 0xC0) i += 2;
                else if ((c & 0xF0) == 0xE0) i += 3;
                else                          i += 4;
                ++n;
            }
            return n;
        };
        if (!generic.empty() && approxEqual(magnitude, 1.0)) {
            // Genérico = score 0.  Comparamos con la misma fórmula
            // effective = vLen + score×2.
            size_t gL = vLen(generic);
            const auto eff = [](size_t L, int s) { return L + size_t(s) * 2; };
            const bool replace = best.text.empty()
                                 || eff(gL, 0) < eff(best.vLen, best.score);
            if (replace) best = { generic, 0, gL };
        } else if (best.text.empty()) {
            // Magnitud != 1 sin compound match: genérica con "(×N)".
            best.text = generic;
        }
        return best.text;
    }
}

Unit Unit::operator*(const Unit& o) const noexcept {
    Unit r;
    for (size_t i = 0; i < exp.size(); ++i) {
        // int + int en int8_t — los exponentes físicos reales caben
        // sobradamente (intensidad luminosa rara vez pasa de ±3).
        r.exp[i] = static_cast<int8_t>(exp[i] + o.exp[i]);
    }
    r.magnitude = magnitude * o.magnitude;
    return r;
}

Unit Unit::operator/(const Unit& o) const noexcept {
    Unit r;
    for (size_t i = 0; i < exp.size(); ++i) {
        r.exp[i] = static_cast<int8_t>(exp[i] - o.exp[i]);
    }
    r.magnitude = magnitude / o.magnitude;
    return r;
}

Unit Unit::pow(int n) const noexcept {
    Unit r;
    for (size_t i = 0; i < exp.size(); ++i) {
        r.exp[i] = static_cast<int8_t>(exp[i] * n);
    }
    r.magnitude = std::pow(magnitude, n);
    return r;
}

}  // namespace scinodes
