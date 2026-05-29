#pragma once

#include "Unit.hpp"

#include <string>
#include <string_view>

namespace scinodes {

// ---------------------------------------------------------------------------
// Quantity — magnitud física = (escalar × unidad).  Etapa 6I.A del refactor
// de campos del nodo (ver `doc/dimensional_analysis_proposal.md`).
//
// Un Quantity es la unidad mínima de información que un campo del nodo
// transporta: NO se separa el valor numérico de la unidad, son una sola
// entidad parseable.  Conceptualmente igual a las "Units" de Blender que
// muestran `0.7 m`, `90°`, `1.0 V` como un solo widget.
//
// Distinción frente a `Unit`:
//   - `Unit` representa SOLO la dimensión + magnitud factor (rad, m,
//     V·s/rad).  Es metadata pura; no lleva un número.
//   - `Quantity` lleva además el ESCALAR del usuario.  `{100, cm}` es
//     una longitud específica; `{cm}` (sin valor) no tiene sentido.
//
// Representación NO canonical:
//   `parseQuantity("100cm")` devuelve `{value=100, unit=Unit{m, mag=0.01}}`.
//   Para obtener el valor SI canónico (en metros base): `q.toSI() = 1.0`.
//   Esto preserva la intención del usuario (round-trip lossless) y deja
//   la canonicalización para el momento de render o de emisión a Scilab.
// ---------------------------------------------------------------------------
struct Quantity {
    double value = 0.0;
    Unit   unit;

    // Valor en SI base (multiplica el factor de magnitud).  Lo usa el
    // codegen al emitir números a Scilab — el solver SIEMPRE trabaja
    // en SI canonical, sin importar qué unidad eligió el usuario en el
    // .scn.  Ej: `{100, cm}.toSI() = 1.0` (metros).
    double toSI() const noexcept { return value * unit.magnitude; }

    // ¿Sólo escalar (sin dimensión física)?  Coeficientes adimensionales
    // de un PID, factores de escala, signos ±1.  Diferencia de
    // `unit.isDimensionless()`: aquí garantizamos que el contexto es
    // realmente un número puro, no un radián disfrazado.
    bool isDimensionless() const noexcept { return unit.isDimensionless(); }

    // Igualdad estricta: mismo escalar Y misma unidad (incluyendo
    // magnitud).  `{100, cm} != {1, m}` aun siendo equivalentes en SI —
    // el round-trip los distingue.  Para equivalencia física usar
    // `equivalentSI`.
    bool operator==(const Quantity& o) const noexcept {
        return value == o.value && unit == o.unit;
    }
    bool operator!=(const Quantity& o) const noexcept { return !(*this == o); }

    // ¿Representan el MISMO valor físico en SI?  Compara dimensión +
    // valor canonicalizado.  `{100, cm}.equivalentSI({1, m}) = true`.
    // Usa tolerancia relativa para evitar falsos negativos por
    // redondeo (1e-9, el mismo umbral que usa Unit::toCanonicalString).
    bool equivalentSI(const Quantity& o) const noexcept;
};

// ---------------------------------------------------------------------------
// parseQuantity — parser gramatical de la cadena combinada `valor unidad`.
//
// Gramática (consistente con la EBNF de §3 del proposal):
//
//   quantity ::= ws? (number_unit | unit_only) ws?
//   number_unit ::= number ws? unit?
//   unit_only   ::= unit                     // value = 1.0 implícito
//   number      ::= sign? (digits ('.' digits)?
//                          | '.' digits) exponent?
//   sign        ::= '+' | '-'
//   exponent    ::= ('e' | 'E') sign? digits
//   unit        ::= (delega a `parseUnit`)
//
// Ejemplos válidos:
//   "1V"        → {1.0, V}
//   "100cm"     → {100, cm}  (cm tiene magnitud 0.01 → toSI = 1.0 m)
//   "0.5"       → {0.5, dimensionless}   (sin unidad = adimensional)
//   "V"         → {1.0, V}               (sin número = 1 unidad)
//   ".5 kg"    → {0.5, kg}
//   "-3 m/s"   → {-3.0, m/s}
//   "2.5 V/s"  → {2.5, V/s}
//   "1e3 Hz"   → {1000.0, Hz}
//   "1 N·m"    → {1.0, N·m}              (≡ {1.0, J} dimensionalmente)
//
// Inválidos (devuelven error):
//   ""          → "vacío"
//   "xyz"       → "unidad desconocida"
//   "1 xyz"     → "unidad desconocida"
//   "1.2.3"     → "número malformado"
//
// Ambigüedad resuelta:
//   "1V" — entre la lectura "1" + "V" y "1V" como número exponencial:
//   `1V` NO es número (no hay forma de leer V como exponente válido),
//   así que strtod consume "1" y deja "V" para parseUnit.  Comportamiento
//   coherente con Blender y SI estándar (`1V` se lee como uno-volt).
// ---------------------------------------------------------------------------
struct ParseQuantityResult {
    Quantity     quantity;
    std::string  error;     // vacío = éxito
    // `hasUnit = false` ⇔ el input fue un número puro ("5", "0.5",
    // "1e3").  Permite al caller decidir si el usuario quiso REEMPLAZAR
    // toda la cantidad (typed "5V") o sólo cambiar el escalar
    // preservando la unidad existente (typed "5", al editar un campo
    // que ya estaba en V → debería seguir en V).  Comportamiento estilo
    // Blender: la unidad sólo cambia cuando el usuario la nombra.
    bool         hasUnit = false;
    bool ok() const noexcept { return error.empty(); }
};

ParseQuantityResult parseQuantity(std::string_view text);

// Variante context-aware (etapa 6I.M): si el parse normal falla y el
// texto es del tipo "<número><prefijo-solo>" (p.ej. "2k", "5m", "100μ"),
// interpretamos el prefijo aplicado a la unidad del contexto.
//   "2k"   en Ohm-field   →  {2, kΩ}    (toSI = 2000)
//   "2k"   en V-field     →  {2, kV}    (toSI = 2000)
//   "100μ" en s-field     →  {100, μs}  (toSI = 1e-4)
// La motivación es de UX: el usuario sabe la dimensión del campo y NO
// debería tipearla otra vez para usar un prefijo.  Esto NO toca la
// semántica del parse normal — si el texto contiene una unidad
// completa (p.ej. "2 V"), el parser respeta la unidad que el usuario
// escribió aunque sea distinta a la del contexto.  El R7 a nivel
// field (etapa 6I.F.1) decide qué hacer con eso.
ParseQuantityResult parseQuantity(std::string_view text,
                                  const Unit& contextUnit);

// Inversa para display: rinde el Quantity como `"<value> <unit>"` con la
// unidad canonicalizada (delega a `Unit::toCanonicalString()`).  Si la
// unidad es dimensionless con magnitud 1, devuelve sólo el número.  Útil
// para inicializar el InputText del widget de campo (etapa 6I.F).
//
// El número se formatea con `%g` (precisión razonable, sin ceros
// trailing).  Si se necesita precisión fija, el caller formatea aparte y
// concatena con `toCanonicalString()`.
std::string toDisplayString(const Quantity& q);

}  // namespace scinodes
