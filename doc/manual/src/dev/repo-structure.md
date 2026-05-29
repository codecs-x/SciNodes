# Estructura del código

El binario se reparte en tres capas que viven en `src/`:

| Capa     | Responsabilidad                                          |
|----------|----------------------------------------------------------|
| `core/`  | Tipos del dominio, gramática, modelo del grafo, codegen, |
|          | persistencia, *bridge* con Scilab.  Sin ImGui, sin Vulkan. |
| `ui/`    | `NodeCanvas`, `NodePalette`, `PlotPanel`, `View3DPanel`,    |
|          | `StatusBar`.  Habla Dear ImGui.                            |
| `app/`   | Ciclo de vida del proceso, contexto Vulkan, ventana SDL.   |

La regla de dependencia es estricta: las capas externas
incluyen a las internas, nunca al revés.

## Tests

- `tests/test_grammar.cpp` — *headless*; cubre cada regla
  con casos positivos y negativos, valida el codegen para
  cada `NodeType` emitible.
- `tests/test_integration.cpp` — arranca `scilab-cli` real y
  compara las trayectorias producidas contra soluciones
  analíticas.
