#include "Quantity.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace scinodes {

bool Quantity::equivalentSI(const Quantity& o) const noexcept {
    if (!unit.sameDimension(o.unit)) return false;
    const double a = toSI();
    const double b = o.toSI();
    if (a == b) return true;
    const double scale = std::max(std::fabs(a), std::fabs(b));
    return std::fabs(a - b) <= 1e-9 * scale;
}

namespace {

// Skip whitespace and tab characters in-place (advances the index).  No
// support for newline within a Quantity literal — un Quantity es una
// línea, los .scn que rompan eso son inválidos.
void skipWs(std::string_view text, size_t& i) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
}

// Mirar (no consumir) si el subrango `text[i..]` comienza con un dígito
// o un signo válido para arrancar un número.  Determina si la siguiente
// fase del parser intenta leer un número o cae directo a parseUnit.
bool looksLikeNumber(std::string_view text, size_t i) {
    if (i >= text.size()) return false;
    const char c = text[i];
    if (std::isdigit(static_cast<unsigned char>(c))) return true;
    // Signo seguido de dígito o punto.  El signo solo no es número.
    if ((c == '+' || c == '-') && i + 1 < text.size()) {
        const char d = text[i + 1];
        if (std::isdigit(static_cast<unsigned char>(d))) return true;
        if (d == '.' && i + 2 < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[i + 2]))) return true;
    }
    // Punto seguido de dígito (".5").
    if (c == '.' && i + 1 < text.size() &&
        std::isdigit(static_cast<unsigned char>(text[i + 1]))) return true;
    return false;
}

// Intenta consumir un número desde `text[i..]` usando strtod (que ya
// maneja correctamente decimal + exponencial + signos).  En éxito,
// actualiza `i` al primer carácter no consumido y devuelve true.  No
// modifica `i` si el parseo falla.
//
// Por qué strtod y no std::from_chars: en C++17 strtod es portable y
// maneja exponentes sin requerir <charconv> con FP support, que algunas
// libstdc++ viejas todavía no implementan.
bool tryParseNumber(std::string_view text, size_t& i, double& out) {
    if (!looksLikeNumber(text, i)) return false;
    // strtod necesita un C-string null-terminated.  Copiamos el slice.
    // El tamaño máximo razonable de un número (incluido exponente) es
    // ~32 caracteres — basta con un buffer fijo para evitar alloc.
    const size_t maxLen = std::min(text.size() - i, size_t{64});
    char buf[65];
    std::memcpy(buf, text.data() + i, maxLen);
    buf[maxLen] = '\0';
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end == buf) return false;
    const size_t consumed = static_cast<size_t>(end - buf);
    i += consumed;
    out = v;
    return true;
}

}  // namespace

ParseQuantityResult parseQuantity(std::string_view text) {
    ParseQuantityResult r;
    size_t i = 0;
    skipWs(text, i);

    if (i >= text.size()) {
        r.error = "vacío";
        return r;
    }

    double value = 1.0;
    bool   hasNumber = false;
    if (tryParseNumber(text, i, value)) {
        hasNumber = true;
    }

    skipWs(text, i);

    // Resto del string = candidato a unidad.  Vacío después del número
    // significa adimensional (`0.5` → {0.5, dimensionless}).
    if (i >= text.size()) {
        if (!hasNumber) {
            // No había número ni unidad — texto vacío post-whitespace.
            // looksLikeNumber ya descartó este caso arriba; cae aquí
            // sólo si text era puro whitespace.
            r.error = "vacío";
            return r;
        }
        r.quantity = Quantity{ value, Unit{} };  // dimensionless
        return r;
    }

    // Parsea lo que queda como unidad.  parseUnit ya ignora whitespace
    // y produce un mensaje de error con contexto.
    std::string_view rest = text.substr(i);
    auto unitR = parseUnit(rest);
    if (!unitR.ok()) {
        // Propagar el mensaje del parser de unidades — más útil que
        // un mensaje genérico ("magnitud desconocida ...").
        r.error = unitR.error.empty() ? "unidad inválida" : unitR.error;
        return r;
    }
    r.quantity = Quantity{ value, unitR.unit };
    r.hasUnit  = true;
    return r;
}

ParseQuantityResult parseQuantity(std::string_view text,
                                  const Unit& contextUnit) {
    // 1. Intento normal: si el usuario tipeó algo válido, lo usamos.
    auto r = parseQuantity(text);
    if (r.ok()) return r;

    // 2. Fallback: chequear si el texto es "<número><prefijo-solo>".
    //    Reproducimos el tokenizer mínimo localmente — no exponemos
    //    skipWs/tryParseNumber del anon namespace.
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;

    // Misma lógica de tryParseNumber en línea (no tenemos acceso al
    // helper anónimo desde acá; replicamos minimalmente).
    double value = 0.0;
    const size_t numStart = i;
    {
        // Permite signo + dígitos/decimal/exponente.  Si strtod no
        // consume nada, el fallback no aplica.
        if (i >= text.size()) return r;
        char head = text[i];
        const bool startsLikeNumber =
            std::isdigit(static_cast<unsigned char>(head)) ||
            ((head == '+' || head == '-' || head == '.') && i + 1 < text.size());
        if (!startsLikeNumber) return r;
        char buf[65];
        const size_t maxLen = std::min(text.size() - i, size_t{64});
        std::memcpy(buf, text.data() + i, maxLen);
        buf[maxLen] = '\0';
        char* end = nullptr;
        value = std::strtod(buf, &end);
        if (end == buf) return r;          // no consumed
        i += static_cast<size_t>(end - buf);
    }
    if (i == numStart) return r;

    // Trim whitespace + posibles separadores entre número y prefijo.
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= text.size()) return r;        // sin sufijo, ya hubiera parsed

    // El resto, sin trailing whitespace, debe matchear EXACTAMENTE un
    // prefijo conocido.
    size_t end = text.size();
    while (end > i && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    std::string_view suf = text.substr(i, end - i);
    const double factor = prefixFactor(suf);
    if (factor == 0.0) return r;           // no es prefijo-solo

    // Construimos el Quantity: misma dimensión que contextUnit, pero la
    // magnitud queda determinada por el prefijo (resetea cualquier mag
    // previa del contextUnit — el prefijo del usuario es la verdad).
    ParseQuantityResult ok;
    ok.quantity.value     = value;
    ok.quantity.unit      = contextUnit;
    ok.quantity.unit.magnitude = factor;
    ok.hasUnit = true;
    return ok;
}

std::string toDisplayString(const Quantity& q) {
    char numBuf[32];
    std::snprintf(numBuf, sizeof(numBuf), "%g", q.value);
    const std::string unitStr = q.unit.toCanonicalString();
    if (unitStr.empty())
        return std::string(numBuf);
    return std::string(numBuf) + " " + unitStr;
}

}  // namespace scinodes
