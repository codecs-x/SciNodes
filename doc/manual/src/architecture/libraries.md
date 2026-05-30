# Librerías internas

Tres librerías estáticas extraídas del core durante la etapa
6K.  Se diseñaron para ser **reutilizables fuera de SciNodes** —
cualquier programa que necesite álgebra dimensional o un editor
de nodos podría depender de ellas.

## `scinodes_units`

Álgebra dimensional pura.  Sin dependencias.

| Header                | Tipos principales                     |
|-----------------------|---------------------------------------|
| `Unit.hpp`            | `Unit` (8 exponentes + magnitud)      |
| `Quantity.hpp`        | `Quantity` (value + Unit)             |
| `QuantityParse.hpp`   | `parseQuantity(string)`               |
| `UnitCatalog.hpp`     | Tabla canónica + aliases              |
| `UnitFormat.hpp`      | `formatUnit(Unit)` → display string   |

Para usar fuera del proyecto:

```cmake
target_link_libraries(my_target PRIVATE scinodes_units)
```

## `scinodes_graph`

Modelo del grafo + gramática + análisis.  Depende de
`scinodes_units`.

| Header                          | Tipos                                         |
|---------------------------------|-----------------------------------------------|
| `NodeGraph.hpp`                  | grafo + persistencia .scn                    |
| `NodeInstance.hpp`               | una instancia con parámetros                 |
| `NodeType.hpp`                   | definición de tipos del registry             |
| `NodeKind.hpp`                   | sum type de gramáticas                       |
| `GrammarParser.hpp`              | validación R1–R7                              |
| `DimensionalAnalyzer.hpp`        | propagación de unidades                       |
| `Field.hpp`                      | QuantityField + ParamDef                     |
| `UndoRedoStack.hpp`              | mutaciones reversibles                        |
| `CustomNodeRegistry.hpp`         | nodos cargados de JSON                       |

No depende de UI ni de Scilab.  Headless-testeable.

## `scinodes_plots`

Renderers de plots IMGUI en `src/ui/plots/`.  Depende de
`scinodes_graph` + Dear ImGui.

| Renderer            | Función                                       |
|---------------------|-----------------------------------------------|
| `WaveRenderer`      | Plot temporal con auto-escala (Oscilloscope)  |
| `SpectrumRenderer`  | Magnitud + fase (FFTAnalyzer)                 |
| `PhaseRenderer`     | Plot 2D (x, y) — PhasePortrait                |
| `HistogramRenderer` | Distribución de muestras (DistributionSink)   |
| `HeatmapRenderer`   | Heatmap 2D temporal (HeatmapSink)             |

Comparten `PlotDefaults.hpp` y `ZoomState.hpp` para
consistencia visual y persistencia del zoom entre frames.
Sin dependencia de NodeGraph — reciben el buffer directamente.

## Por qué tres y no una sola

Tres niveles de "blast radius":

- Cambiar `scinodes_units`: pocas líneas afectadas, recompila
  todo el resto.
- Cambiar `scinodes_graph`: recompila plots + UI, no las units.
- Cambiar UI o backend: nada del resto se toca.

La separación es **build hygiene**, no run-time isolation.

## Migración futura — librerías públicas

Después de v0.2, las tres librerías podrían empaquetarse como:

- Header-only versión de `scinodes_units` (cambiar el `.cpp`
  por inline templates).
- Conan / vcpkg recipes.
- API estable con guarantee de SemVer.

Esto está fuera del scope de la tesis pero sería el camino si
SciNodes se publica como tool.
