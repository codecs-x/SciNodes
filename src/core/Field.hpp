#pragma once

#include "NodeType.hpp"
#include "Quantity.hpp"

#include <string>
#include <vector>

namespace scinodes {

// ---------------------------------------------------------------------------
// FieldKind — categoría de un campo dentro de un nodo (etapa 6I.B del
// refactor de campos).  Antes esto vivía implícito en tres listas
// paralelas de NodeDef:
//
//   - `inputPorts` / `inputPortLabels` / `inputPortUnits`
//   - `params` (ParamDef[])
//   - `outputPorts` / `outputPortLabels` / `outputPortUnits`
//
// La unificación bajo Field hace explícita la jerarquía
// `Graph → Node → Field → Quantity(value, unit)` que propuso el usuario.
// ---------------------------------------------------------------------------
enum class FieldKind {
    Input,        // recibe señal del cable; default editable si desconectado
    Output,       // emite señal al cable
    Parameter,    // valor editable; con pin de override (etapa 5/6)
};

// ---------------------------------------------------------------------------
// FieldDef — declaración de un campo en el registry.  Equivalente
// semántico de:
//
//   ParamDef:
//      name → FieldDef::name
//      defaultValue + unit → FieldDef::defaultQuantity
//      (kind = Parameter, type = scalar)
//
//   Input port:
//      stringParams["portLabel<i>"] / inputPortLabels[i] → FieldDef::label
//      inputPortTypes[i] → FieldDef::type
//      inputPortUnits[i] → FieldDef::defaultQuantity.unit
//                          (polymorphic si vacío)
//      (kind = Input)
//
//   Output port: idem inputs.
//
// `polymorphic = true` significa que el registry NO declara la unidad —
// el analyzer la infiere (propagación) o el usuario la pinea
// (override per-instance).  `defaultQuantity.unit` en ese caso es
// adimensional × 1 (placeholder).
// ---------------------------------------------------------------------------
struct FieldDef {
    std::string name;             // id único dentro del nodo
    std::string label;            // display; vacío → fallback "<kind> <index>"
    FieldKind   kind;
    TypeExpr    type;             // shape (scalar / vec3 / geometry / ...)
    Quantity    defaultQuantity;  // value + unit por defecto
    bool        polymorphic = false;  // true → unit no declarada por registry
};

// ---------------------------------------------------------------------------
// synthesizeFields — vista unificada del NodeDef como lista ordenada de
// FieldDef.  Orden: inputs primero, params, outputs al final (igual que
// el renderer del nodo).
//
// Esta función es la API NUEVA que los consumers pueden usar sin esperar
// a la migración total.  Lee la estructura legacy (`params`,
// `inputPortLabels`, `inputPortUnits`, etc.) y emite FieldDef
// equivalentes.  Etapas 6I.D-G migran los consumers para que llamen
// `fields()`; etapa 6I.H borra los campos legacy y `fields()` se vuelve
// el storage directo.
//
// Coste: O(n) en el conteo de campos, sin caching.  Llamadores de hot
// loops deberían cachear el resultado.
// ---------------------------------------------------------------------------
std::vector<FieldDef> synthesizeFields(const NodeDef& def);

}  // namespace scinodes
