# Librerías internas: `scinodes_units`, `scinodes_graph`, `scinodes_plots`

El código `core/` y `ui/` se reparte en sub-librerías para
acortar tiempos de compilación y aislar dependencias.

## `scinodes_units`

`src/core/Unit*.{cpp,hpp}` + `Quantity.{cpp,hpp}` se
empaquetan como una librería estática que **no** depende
de ImGui ni del modelo del grafo.  Tests
(`test_units`) corren *headless* en milisegundos.

## `scinodes_graph`

Todo el modelo: `NodeType`, `NodeInstance`, `NodeKind`,
`NodeGraph`, `GrammarParser`, `Field`, `DimensionalAnalyzer`,
`UndoRedoStack`, `CustomNodeRegistry`.  Depende de
`scinodes_units`.  Sin ImGui, sin Vulkan.

## `scinodes_plots`

`src/ui/plots/`.  Cinco renderers de plot (`WaveRenderer`,
`SpectrumRenderer`, `PhaseRenderer`, `HistogramRenderer`,
`HeatmapRenderer`) con `PlotDefaults.hpp` para colores
compartidos.  Depende solo de ImGui.

## Splits a nivel de archivo

`NodeCanvas.cpp` se reparte en `NodeCanvasPanels.cpp` +
`NodeCanvasPopups.cpp` + `NodeCanvasRender.cpp` +
`NodeCanvasInternal.hpp`.  `View3DPanel.cpp` (~1835 líneas
en su pico) se divide en `View3DPanel.cpp` + `View3DPanelMesh.cpp`
+ `View3DPanelAsset.cpp`.  `NativeNodeRenderer.cpp` (~1053)
se divide en cuatro archivos por responsabilidad.

Resultado: un cambio típico toca uno o dos archivos, no
una pieza monolítica.
