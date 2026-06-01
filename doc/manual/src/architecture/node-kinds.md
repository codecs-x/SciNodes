# NodeKind y dispatch polimórfico

Cada nodo del grafo tiene un **NodeType** (un string como
`StepSignal`, `Gain`, …) y un **NodeKind** (una etiqueta de
gramática que indica cómo se procesa).

## Los cinco kinds

```cpp
using NodeKind = std::variant<
    BuiltinKind,            // Nodo del registry estándar
    CustomKind,             // Nodo cargado de JSON en runtime
    SubGraphContainerKind,  // El "padre" del sub-grafo
    SubGraphInputKind,      // Stub de entrada
    SubGraphOutputKind      // Stub de salida
>;
```

La función `kindOf(NodeType)` es el único discriminador
permitido en el codebase — todos los demás dispatchs polimórficos
caen en este boundary.

## Por qué variant + visit

El estilo anterior del proyecto usaba cadenas de `if/else` o
`switch` sobre strings.  Resultado: agregar un kind nuevo
requería tocar 12 archivos.

Con `std::variant + std::visit` cada kind aporta su lógica
en un método miembro o una función libre con sobrecarga, y
agregar un kind nuevo se concentra en un archivo.

## Tablas de hooks por NodeType

Para los `BuiltinKind` (la mayoría), el dispatch dentro de un
algoritmo (codegen, analyzer, walker 3D) se hace con una
**tabla de hooks** indexada por NodeType:

```cpp
// Ejemplo: ScilabCodeGen
using PlanFn = std::function<void(PlanContext&, const NodeInstance&)>;
const std::unordered_map<std::string, PlanFn> kPlanners = {
    { "Gain",        planGain },
    { "Integrator",  planIntegrator },
    { "Summation",   planSummation },
    // ...
};
```

Esto evita el `if-else-if` repetitivo y permite registrar tipos
nuevos sin tocar el dispatcher.

## Sitios donde aparece el dispatch

| Sitio                              | Decisión                                      |
|------------------------------------|-----------------------------------------------|
| `ScilabCodeGen::planNode`           | Cómo se emite el `.sce` para este nodo.       |
| `scinodes::analyzeUnits`             | Cómo transforma la unidad de su salida (vía `unitTransformKind`, alias-target, o polimorfismo). |
| `SceneCollector::evalVec3At`         | Cómo computa su valor vec3 en t actual.       |
| `NodeCanvas::readLiveSampleAt`      | Cómo lee la muestra en vivo durante la sim.   |
| `View3DPanel` para `Object3D`       | Cómo expone el dropdown de assets.            |

Cada uno tiene su propia tabla.  Los kinds estructurales
(SubGraph*, Custom) tienen rutas especiales que cruzan la
frontera del grafo activo.

## SubGraph traversal

Caso real (etapa 6J.1.b): un walker 3D que cruzaba la frontera
de un SubGraph correctamente, pero su helper `findSubTap` no.
Resultado: el motor 3D se quedaba estático cuando había un
convertidor `RadToDeg` adentro.  La lección:

> **Todos los walkers comparten la misma pila de frames de
> SubGraph.  El lexical scope es global al paso, no por helper.**

Ver `src/core/SceneCollector.cpp` para el patrón correcto.

## Cómo agregar un kind nuevo

1. Declarar la nueva struct (`MyNewKind {};`) y agregarla al
   `std::variant`.
2. Implementar `kindOf` para los NodeType que devuelvan este
   kind.
3. Implementar los visitors o entradas de tabla en cada uno de
   los cinco sitios listados arriba.
4. Compilar.  El compilador grita en cada `std::visit` que no
   cubre el nuevo caso — eso es el feature, no el bug.
