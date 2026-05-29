#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace scinodes {

// ---------------------------------------------------------------------------
// Unit — representación algebraica de unidades físicas.  Fundación de R7
// (coherencia dimensional) — etapa 6A del upgrade gramatical.  Ver
// `doc/designs/dimensional_analysis_proposal.md` (v2).
//
// Una unidad se descompone en:
//
//   1. `exp` — vector de exponentes.  Las 7 primeras posiciones son
//      las dimensiones SI base; la 8ª es la "phantom angle exponent"
//      (etapa 6I.L) que cuenta radianes y distingue rad (adimensional
//      pero con identidad angular) de un escalar puro:
//
//        índice  símbolo  magnitud SI
//          0       m       longitud
//          1       kg      masa
//          2       s       tiempo
//          3       A       corriente eléctrica
//          4       K       temperatura
//          5       mol     cantidad de sustancia
//          6       cd      intensidad luminosa
//          7       rad     ángulo (extra-SI; el SI puro la trata como
//                          adimensional, pero el análisis aquí la
//                          mantiene para distinguir Hz de rad/s)
//
//   2. `magnitude` — factor multiplicativo desde la unidad declarada hacia
//      la unidad SI base canónica.  Permite distinguir prefijos (km vs m,
//      mV vs V) y unidades derivadas no-canónicas (deg vs rad, rpm vs
//      rad/s) sin perder su dimensión.  Comparaciones de dimensión IGNORAN
//      la magnitud — 100 cm == 1 m dimensionalmente.
//
// Ejemplos canónicos:
//
//     1 m     → exp = (1,0,0,0,0,0,0,0)        magnitude = 1.0
//     1 km    → exp = (1,0,0,0,0,0,0,0)        magnitude = 1000.0
//     1 cm    → exp = (1,0,0,0,0,0,0,0)        magnitude = 0.01
//     1 V     → exp = (2,1,-3,-1,0,0,0,0)      magnitude = 1.0
//     1 W     → exp = (2,1,-3,0,0,0,0,0)       magnitude = 1.0
//     1 V·A   → V * A = exp(2,1,-3,0,0,0,0,0)  magnitude = 1.0       == W ✓
//     1 J     → exp = (2,1,-2,0,0,0,0,0)       magnitude = 1.0
//     1 N·m   → N * m = exp(2,1,-2,0,0,0,0,0)  magnitude = 1.0       == J ✓
//     1 rad   → exp = (0,0,0,0,0,0,0,1)        magnitude = 1.0       (angle)
//     1 deg   → exp = (0,0,0,0,0,0,0,1)        magnitude = π/180     (angle)
//     1 rad/s → exp = (0,0,-1,0,0,0,0,1)       magnitude = 1.0
//     1 Hz    → exp = (0,0,-1,0,0,0,0,0)       magnitude = 1.0       ≠ rad/s (sin angle)
//
// El parser de unidades (etapa 6B) toma strings como "V·A" o "kg·m/s^2"
// y produce instancias de Unit usando estos operadores algebraicos.
// ---------------------------------------------------------------------------
struct Unit {
    std::array<int8_t, 8> exp = { 0, 0, 0, 0, 0, 0, 0, 0 };
    double magnitude = 1.0;

    // ¿Las dos unidades comparten DIMENSIÓN física?  Esto es lo que R7
    // chequea — la magnitud NO se compara (100 cm y 1 m son
    // dimensionalmente compatibles).
    bool sameDimension(const Unit& o) const noexcept {
        return exp == o.exp;
    }

    // Adimensional sii TODOS los exponentes son cero.  No es un caso
    // especial — es la unidad neutra para multiplicación.  rad es
    // DIMENSIONALMENTE adimensional (rad = m/m); deg y rpm también lo son
    // dimensionalmente, distinguibles sólo por magnitud.
    bool isDimensionless() const noexcept {
        for (auto e : exp) if (e != 0) return false;
        return true;
    }

    // Igualdad estricta: misma dimensión Y misma magnitud.  Útil para
    // tests deterministas.  R7 usa sameDimension, no operator==.
    bool operator==(const Unit& o) const noexcept {
        return exp == o.exp && magnitude == o.magnitude;
    }
    bool operator!=(const Unit& o) const noexcept { return !(*this == o); }

    // Producto: exponentes suman, magnitudes multiplican.  V * A = W.
    Unit operator*(const Unit& o) const noexcept;

    // Cociente: exponentes restan, magnitudes dividen.  V / A = Ω.
    Unit operator/(const Unit& o) const noexcept;

    // Potencia entera: exponentes se multiplican por n, magnitud se
    // eleva a n.  m^2 = área; m^-1 = 1/m.
    //
    // Entero (no real) porque las unidades físicas reales tienen
    // exponentes enteros — m^0.5 no aparece en física estándar y
    // permitirlo abriría preguntas (¿cuánto es kg^0.5?) que no necesitamos
    // resolver ahora.  Si surge un caso (densidad espectral kg^0.5),
    // promover a int8_t fraccionario futuro.
    Unit pow(int n) const noexcept;

    // Display canónico (etapa 6C).  Reglas:
    //   - Adimensional con mag=1.0 → "" (string vacío).
    //   - Adimensional con mag distinto → "rad" o "deg" o "(×N)" según
    //     coincida con valores conocidos; sino, "(dimensionless × N)".
    //   - Si los exp coinciden EXACTAMENTE con un símbolo del catálogo
    //     (V, W, J, N, Pa, Hz, Ω, ...) Y la magnitud coincide
    //     (1.0 para SI base, o un prefijo SI conocido), devolver
    //     "<prefix><symbol>".  Ej: Unit{exp V, mag 1e3} → "kV".
    //   - Sino, decomposición genérica: "<num>/<den>" con cada base SI
    //     y su exponente.  Ej: "m^2·kg·s^-3·A^-1".
    //
    // No incluye una unidad de magnitud factor genérico — eso queda como
    // anomalía visible (rare en la práctica si las constantes del
    // catálogo se usan).
    std::string toCanonicalString() const;
};

// ---------------------------------------------------------------------------
// Constructores convenientes — síntesis directa de unidades base.  El
// parser textual (etapa 6B) compone usando estos + los operadores.
// ---------------------------------------------------------------------------
inline constexpr Unit unitDimensionless() noexcept { return Unit{}; }

inline Unit unitMeter()    noexcept { return Unit{ {1,0,0,0,0,0,0,0}, 1.0 }; }
inline Unit unitKilogram() noexcept { return Unit{ {0,1,0,0,0,0,0,0}, 1.0 }; }
inline Unit unitSecond()   noexcept { return Unit{ {0,0,1,0,0,0,0,0}, 1.0 }; }
inline Unit unitAmpere()   noexcept { return Unit{ {0,0,0,1,0,0,0,0}, 1.0 }; }
inline Unit unitKelvin()   noexcept { return Unit{ {0,0,0,0,1,0,0,0}, 1.0 }; }
inline Unit unitMole()     noexcept { return Unit{ {0,0,0,0,0,1,0,0}, 1.0 }; }
inline Unit unitCandela()  noexcept { return Unit{ {0,0,0,0,0,0,1,0}, 1.0 }; }
inline Unit unitRadian()   noexcept { return Unit{ {0,0,0,0,0,0,0,1}, 1.0 }; }

// Aplica un factor de magnitud (prefijo) a una unidad existente.
// El parser lo usa para "k m" → unitWithPrefix(unitMeter(), 1e3).
inline Unit unitWithPrefix(Unit base, double factor) noexcept {
    base.magnitude *= factor;
    return base;
}

// ---------------------------------------------------------------------------
// parseUnit — parser gramatical textual (etapa 6B del análisis dimensional).
//
// Reconoce expresiones de unidad construidas según la gramática EBNF
// documentada en `doc/designs/dimensional_analysis_proposal.md` §3:
//
//   unit  ::= term ( ('·' | '*') term | '/' term )*
//   term  ::= base ('^' integer)?
//   base  ::= ( prefix? unit_name ) | '(' unit ')'
//
// Acepta:
//   - Bases SI: m, kg, s, A, K, mol, cd, g.
//   - Derivadas: V, W, J, N, Pa, Hz, Ω, C, F, T, H, rad, deg, rpm.
//   - Prefijos: Y, Z, E, P, T, G, M, k, h, da, d, c, m, μ (= u), n, p,
//     f, a, z, y.
//   - Compuestos: "V·A", "kg·m/s^2", "rad/s", "kg·m^2/(A·s^3)".
//   - Whitespace ignorable alrededor de operadores.
//
// Ambigüedades resueltas:
//   - Letras adyacentes sin separador → prefix + unit name.
//     "ms" = milli-second; "kV" = kilo-volt; "Tm" = tera-meter.
//   - Para obtener producto sin prefijo, EXIGE separador explícito.
//     "m·s" = meter * second; "m s" rechazado (whitespace NO es operador
//     de multiplicación — evita ambigüedad con valores numéricos prefijos
//     como "100 cm" donde "100" no es una unidad).
//   - Unidades multi-carácter (rad, mol, kg, Hz, Pa, ...) tienen
//     precedencia sobre interpretaciones de un solo carácter.
//
// El resultado lleva `error` no vacío si el parsing falló (texto vacío,
// símbolo desconocido, paréntesis sin cerrar, etc.).  El `unit` queda
// con valor default (adimensional) — el caller DEBE chequear `ok()`.
// ---------------------------------------------------------------------------
struct ParseUnitResult {
    Unit        unit;
    std::string error;     // vacío = éxito
    bool ok() const noexcept { return error.empty(); }
};

ParseUnitResult parseUnit(std::string_view text);

// Lookup de prefijos SI por símbolo.  Devuelve el factor multiplicativo
// si `sym` matchea exactamente un prefijo conocido (k = 1e3, m = 1e-3,
// etc.), 0.0 si no.  Útil para callers que quieren detectar
// "bare-prefix input" como `parseQuantity(text, contextUnit)` en
// etapa 6I.M — el usuario tipea "2k" en un field y queremos saber
// que "k" es kilo, no parte de un símbolo más largo.
double prefixFactor(std::string_view sym);

}  // namespace scinodes
