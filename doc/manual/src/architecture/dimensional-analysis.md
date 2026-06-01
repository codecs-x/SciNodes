# Análisis dimensional (Quantity, Unit)

R7 enforcement.  Las unidades son álgebra real en el dominio
de los exponentes SI, no strings.

## Tipos fundamentales

### `Unit`

```cpp
struct Unit {
    std::array<int8_t, 8> exp;       // m, kg, s, A, K, mol, cd, rad
    double                magnitude; // factor multiplicativo (ej. km vs m → 1000)
};
```

Las 8 dimensiones: las 7 fundamentales del SI más ángulo
("fantasma" — extra-SI; lo mantenemos para distinguir
`Hz` de `rad/s`).  Magnitude permite representar `km` como
`Unit{exp=(1,0,0,0,0,0,0,0), magnitude=1000}` sin perder
que el exponente de metro es 1.

Operaciones declaradas en `src/core/Unit.hpp`:

| Op                       | Resultado                                              |
|--------------------------|--------------------------------------------------------|
| `a * b`                  | exponentes sumados, magnitudes multiplicadas.          |
| `a / b`                  | exponentes restados, magnitudes divididas.             |
| `a.pow(n)`               | exponentes ×n, magnitude^n.                            |
| `a == b`                 | exponentes iguales **y** magnitudes iguales.           |
| `a.sameDimension(b)`     | exponentes iguales (magnitud ignorada). **Es la comparación que usa R7.** |
| `a.isDimensionless()`    | todos los exp == 0 (rad NO es dimensionless por la 8ª dimensión). |
| `a.toCanonicalString()`  | string legible (`"V"`, `"kg·m^2"`, etc.).              |

### `Quantity`

```cpp
struct Quantity {
    double value;
    Unit   unit;
};
```

El valor numérico **en la unidad declarada**.  No se normaliza
a SI internamente — `Quantity{100, km}` se guarda así, no como
`Quantity{100000, m}`.  Normalizamos sólo al cruzar a Scilab.

## Parser gramatical

Función libre `scinodes::parseUnit(text)` retorna
`ParseUnitResult{ Unit unit; std::string error; bool ok() }`.
Para Quantity (número + unidad) hay `parseQuantity` análoga.

Reglas:

- Número (decimal opcional) seguido de **separador** (espacio
  o nada) seguido de **expresión de unidad**.
- La expresión de unidad es una gramática mini:
  ```
  unit  := factor ('·'|'*'|'/' factor)*
  factor := prefix? base ('^' integer)?
  ```
- Sin unidad, devuelve `Quantity{value, dimensionless}`.

Casos cubiertos (verificados por tests `[100]–[115]` y
`[147]–[163]`):

| Entrada       | Salida                                                    |
|---------------|-----------------------------------------------------------|
| `100`         | `{100, dimensionless}`                                    |
| `100 cm`      | `{100, m·0.01}` (magnitud 0.01, exponente m=1)            |
| `9.81 m/s^2`  | `{9.81, m·s^-2}`                                          |
| `60 Hz`       | `{60, s^-1}`                                              |
| `1 V·A`       | `{1, V·A}` que `sameDimension` reconoce como `W`          |

## Propagación por el grafo

Función libre `scinodes::analyzeUnits(NodeGraph& graph)`
definida en `src/core/DimensionalAnalyzer.cpp:69-259`.  Retorna
una `DimensionalAnalysis` con un mapa `inferred: attrId →
Unit` y una lista de `conflicts`.

Corre hasta punto fijo (cap 32 iteraciones; los grafos típicos
convergen en 2–3).  Cinco sub-fases por iteración:

1. **Seed** (líneas 77-115).  Toma las unidades declaradas en
   el registry (`NodeDef::inputPortUnits`,
   `NodeDef::outputPortUnits`) y los overrides per-instancia
   (`NodeInstance::portUnitOverrides`).  Los overrides solo se
   aplican a puertos NO declarados en el registry; los nodos
   canónicamente dimensionados (VoltageSource, DCMotor) son
   inmunes a corrupción por override.

2. **Edge propagation** (líneas 126-146).  Para cada cable:
   - si ambos extremos conocen su unit → chequea
     `sameDimension`; conflicto si no.
   - si solo el origen conoce → asigna esa unit al destino.
   - si solo el destino conoce → asigna esa unit al origen
     ("propagación backward").  Una señal cuyo `out` no
     declara unit hereda la del consumer.

3. **Alias-like** (líneas 154-167).  El output del nodo Alias
   toma la unit del puerto target referenciado por
   `target_node_id` / `target_port`.

4. **Unit-transformers** (líneas 176-213).
   `Integrator` (`MultiplyDomain`): `out = in × domainUnit`.
   `Differentiator` (`DivideDomain`): `out = in / domainUnit`.
   `domainUnit` por defecto es `s` (time-domain), accesible
   vía `NodeGraph::domainUnit()`.

5. **Fully polymorphic** (líneas 215-255).  Un nodo es
   *fully polymorphic* si: el registry no declara unit en
   ningún puerto suyo, no hay overrides, no es unit-transformer,
   no es alias.  En ese caso **todos sus puertos comparten
   unit** — si alguno se conoce, los demás la heredan.

## R7 enforcement

Vive en `NodeGraph::tryAddEdge` (`src/core/NodeGraph.cpp:513-548`)
con una estrategia pre/post: corre `analyzeUnits` **antes** de
añadir el cable y **después** de añadirlo tentativamente.  Si
la cantidad de conflictos sube, el cable es responsable de al
menos uno y se rechaza inmediatamente con
`GrammarError{"R7", message, fromNodeId, toNodeId}`.  El cable
NO se agrega al grafo.  No hay estado intermedio "cable
inválido".

El toggle `m_dimEnforce` (default ON; se apaga vía
`NodeGraph::setDimensionalEnforcement(false)`) permite saltearse
el chequeo — usado principalmente por tests de regresión sobre
`.scn` legacy que datan de antes de R7.  No hay UI para
desactivarlo.

## Override per-instancia ≠ display

Son dos mecanismos distintos.  No confundirlos:

| Concepto                      | Dónde vive                            | Afecta cómputo | Afecta display |
|-------------------------------|---------------------------------------|----------------|----------------|
| `NodeInstance::portUnitOverrides` | por nodo, por puerto              | **sí**         | sí             |
| `NodeGraph::displayUnits`     | por grafo, por DimensionKey           | no             | **sí**         |

- **`portUnitOverrides`** (etapa 6G).  Permite tipar un puerto
  polimórfico para esa instancia concreta.  El analyzer lo
  trata como si el registry lo hubiera declarado, así que
  participa en R7.  Persiste en `.scn` por nodo.
- **`displayUnits`** (etapa 6I.C).  Mapa
  `{DimensionKey → Unit}` a nivel proyecto que la UI usa para
  mostrar valores en la unidad preferida del usuario (ver un
  oscilloscope rotulado en `°` aunque el cable lleve `rad`).
  No interviene en el cómputo ni en R7.  Persiste en `.scn` a
  nivel grafo.

## El ángulo como dimensión fantasma

Las 7 dimensiones del SI no incluyen ángulo (técnicamente
adimensional).  Pero confundir `rad` con número puro lleva a
bugs sutiles — el más común: integrar una velocidad angular
durante 60 segundos y obtener `60` sin saber si son radianes,
revoluciones o grados.

SciNodes le asigna al ángulo el slot 7 (octavo en 0-indexed).
Esto hace que `rad` ≠ `dimensionless` aunque
`sameDimension(rad, m/m) == false`.  Y permite distinguir `Hz`
(`s^-1`) de `rad/s` (`s^-1 · rad`) en `sameDimension`.

Casos canónicos verificados por tests:

- `Integrator(x rad/s)·Δt → rad` (test `[188]`).
- `Differentiator(x rad)/Δt → rad/s` (test `[190]`).
- `Integrator(x V)·Δt → V·s` (test `[189]` — útil para Wb).

## Dónde se vuelve relevante

- En el `Oscilloscope`: el rótulo del eje Y se infiere por
  `analyzeUnits` (etapa 6I.J).
- En el codegen: los parámetros del nodo se normalizan a SI
  antes de emitir el `.sce` (`1 km` → `1000` en el script).
- En cualquier consumer downstream: el flow de unidades
  permite a los walkers (3D, plots, etc.) saber qué unidad
  trae cada cable sin tener que inferirla a mano.
