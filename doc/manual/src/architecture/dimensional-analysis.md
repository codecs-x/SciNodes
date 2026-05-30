# Análisis dimensional (Quantity, Unit)

R7 enforcement.  Las unidades son álgebra real en el dominio
de los exponentes SI, no strings.

## Tipos fundamentales

### `Unit`

```cpp
struct Unit {
    std::array<int8_t, 8> exponents;  // s, m, kg, A, K, mol, cd, rad
    double                magnitude;   // factor multiplicativo (ej. km vs m → 1000)
};
```

Las 8 dimensiones: las 7 fundamentales del SI + ángulo
("fantasma").  Magnitude permite representar `km` como
`Unit{[0,1,0,0,0,0,0,0], 1000}` sin perder que el exponente
de metro es 1.

Operaciones:

| Op                | Resultado                                            |
|-------------------|------------------------------------------------------|
| `a * b`           | exponentes sumados, magnitudes multiplicadas.        |
| `a / b`           | exponentes restados, magnitudes divididas.           |
| `pow(a, n)`       | exponentes × n, magnitude^n.                         |
| `a == b`          | exponentes iguales **y** magnitudes iguales.         |
| `compatible(a, b)`| exponentes iguales (no requiere magnitud).           |

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

`parseQuantity("120 deg")` produce `Quantity{120, Unit{deg}}`.

Reglas:

- Número (decimal opcional) seguido de **separador** (espacio
  o nada) seguido de **expresión de unidad**.
- La expresión de unidad es una gramática mini:
  ```
  unit  := factor ('·'|'*'|'/' factor)*
  factor := prefix? base ('^' integer)?
  ```
- Sin unidad, devuelve `Quantity{value, dimensionless}`.

Casos cubiertos:

| Entrada       | Salida                                                    |
|---------------|-----------------------------------------------------------|
| `100`         | `{100, dimensionless}`                                    |
| `100 cm`      | `{100, m·0.01}`  (magnitud, exponente m=1)                |
| `9.81 m/s^2`  | `{9.81, m·s^-2}`                                          |
| `60 Hz`       | `{60, s^-1}`                                              |
| `1 V·A`       | `{1, V·A}` que se simplifica a `W`                        |

## Propagación por el grafo

`DimensionalAnalyzer::propagate(NodeGraph&)` corre un pase
forward+backward sobre el DAG.

Forward: para cada nodo, leer la unidad de cada input, aplicar
la regla del nodo (`kindOf` + tabla de hooks), escribir la
unidad inferida de cada output.

Backward: para sumadores y otros nodos que exigen unidades
iguales en todas sus entradas, hay un segundo pase que propaga
hacia atrás la unidad del puerto que sí conocemos a los demás
inputs si están sin asignar.

R7: si dos extremos de una arista terminan con unidades
incompatibles, la arista queda marcada inválida y se reporta
diagnóstico.

## Override por instancia

Cada `NodeInstance` puede tener un override de display de
unidades en sus puertos.  Esto NO cambia el cómputo —
solamente la representación.  Útil cuando el modelo está en
radianes pero querés inspeccionar un puerto en grados.

Persistencia: en `.scn`, sección `displayUnits[nodeId][portIndex]`.

## El ángulo como dimensión fantasma

Las 7 dimensiones del SI no incluyen ángulo (técnicamente
adimensional).  Pero confundir `rad` con número puro lleva a
bugs sutiles — el más común: integrar una velocidad angular
durante 60 segundos y obtener `60` sin saber si son radianes,
revoluciones o grados.

SciNodes le asigna al ángulo el slot 7 (octavo en 0-indexed).
Operaciones canónicas:

- `sin(x rad)` → adimensional.
- `cos(x rad)` → adimensional.
- `Differentiator(x rad)/Δt` → `rad/s`.
- `Integrator(x rad/s)·Δt` → `rad`.

Multiplicar dos ángulos sin pasar por trigonometría queda
flag-eado como warning (no error duro, para no romper modelos
exóticos).

## Dónde se vuelve relevante

- En el `Oscilloscope`: el rótulo del eje Y se infiere por
  `analyzeUnits(graph, sinkNodeId)`.
- En el codegen: los parámetros del nodo se normalizan a SI
  antes de emitir el `.sce` (`1 km` → `1000` en el script).
- En los plots de phase portrait: los dos ejes deben tener
  unidades compatibles entre sí, no necesariamente iguales.
