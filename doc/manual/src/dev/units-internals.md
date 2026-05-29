# `Unit`, `Quantity`, `Field`: análisis dimensional

## `Unit`

`src/core/Unit.{cpp,hpp}` modela un sistema SI con 7 + 1
exponentes: `m, kg, s, A, K, mol, cd, rad`
(`rad` como "*phantom angle exponent*" para distinguir
`rad/s` de `Hz`).

```cpp
struct Unit {
    int exp[8];      // SI exponents
    double scale;    // factor a la unidad SI canónica
    std::string display;  // ej. "kΩ", "rad/s"
};
```

Soporta `operator*`, `operator/`, `pow(int)`, equivalencia
algebraica.

## Parser de unidades textuales

`UnitCatalog.hpp` mapea símbolos a `Unit`: `V`, `A`, `Ω`,
`Hz`, `kg·m²`, etc.  Acepta prefijos SI (`k`, `M`, `m`, `μ`,
…).  El parser distingue prefijo (`2k` en un *field* en Ω →
`2 kΩ`) de número crudo (`5` en cualquier *field* →
canónica SI del *field*).

## `Quantity`

Pareja `(valor, Unit)`.  `Quantity::toSI()` devuelve el
escalar puro que el codegen consume.  `parseQuantity` lee
el `QuantityField` widget del usuario.

## Propagación dimensional

`DimensionalAnalyzer.{cpp,hpp}` recorre el grafo en orden
topológico, propaga unidades *forward* (los outputs de un
nodo heredan de sus inputs según una *signature*
declarada) y *backward* (un sumidero con unidad fija
restringe sus upstreams).  Mismatches violan R7 y se
reportan al usuario.

## `Field`

Reemplaza `ParamDef` con una jerarquía unificada:

```cpp
enum class FieldKind { Quantity, Ideal, String, Bool };
struct FieldDef {
    std::string name;
    FieldKind   kind;
    Unit        unit;        // solo Quantity
    Quantity    defaultValue;
    bool        unitOverridable;
};
```

`NodeInstance::fields` es un mapa `name → Quantity|Ideal|...`
que persiste en el `.scn`.
