# Estructura del repositorio

```
SciNodes/
├── CMakeLists.txt           # CMake 3.25+, C++20, FetchContent SDL2/ImGui/json/glslang/tinygltf
├── README.md
├── CHANGELOG.md
├── LICENSE                  # MIT
├── doc/
│   ├── db/                  # Tablas JSON: catálogo, reglas, menús, módulos…
│   └── manual/              # mdBook (usuario + desarrollador)
├── tools/                   # auditorías doc↔código + hook de pre-commit
├── src/
│   ├── main.cpp             # entrypoint: instancia AppWindow y entra al frame loop
│   ├── core/                # capa de modelo: sin SDL/Vulkan/ImGui
│   │   ├── Unit.cpp, Quantity.cpp           → lib scinodes_units
│   │   ├── NodeType, NodeInstance, NodeKind,
│   │   │   NodeGraph, GrammarParser, Field,
│   │   │   DimensionalAnalyzer, UndoRedoStack,
│   │   │   CustomNodeRegistry               → lib scinodes_graph
│   │   ├── ScilabCodeGen, ScilabBridge, ScnSerializer,
│   │   │   Fft, CsvExport, CsvParamIO, I18n, …
│   │   └── backends/        # IComputeBackend: subprocess + call_scilab
│   ├── app/                 # capa de aplicación (orquestación)
│   │   ├── AppWindow, VulkanContext, Vulkan3DRenderer,
│   │   ├── SimController, WorkspaceManager, PanelContext,
│   │   └── FrameClock, ShortcutHandler, FileDialog, …
│   ├── ui/                  # capa de paneles (Dear ImGui)
│   │   ├── NodeCanvas{,Render,Panels,Popups}.cpp
│   │   ├── NodePalette, PlotPanel, OutlinerPanel, StatusBar, …
│   │   ├── View3DPanel{,Mesh,Asset}.cpp
│   │   ├── canvas/          # Canvas + NativeNodeRenderer (lienzo propio)
│   │   └── plots/           # 5 renderers de plot  → lib scinodes_plots
│   └── shaders/             # GLSL → SPIR-V (compilados con glslang en el build)
└── tests/                   # 7 binarios headless (grammar, integration, canvas,
                             #   i18n, contracts, callapi_bridge, example_library)
```

## Las tres capas

![Arquitectura por capas](../diagrams/architecture_layers.svg)

La separación es estricta. `core/` no incluye headers de SDL,
Vulkan ni Dear ImGui. La suite `test_grammar` lo demuestra
construyendo grafos y ejerciendo R0–R7, alcanzabilidad y
undo/redo sin levantar ventana —1146 aserciones en milisegundos—.

`ui/` consume `core/` y lo expone con Dear ImGui (rama `docking`).
Desde v0.0.8 el lienzo es propio (`ui/canvas/`, ya no la librería
externa de nodos). Cada panel implementa su propio `draw()` que se
llama desde el *frame loop* del `AppWindow`. El estado vive en
`core/`; los paneles son ventanas sobre ese estado.

`app/` orquesta a las dos: instancia el `VulkanContext`, abre la
ventana SDL, dibuja la barra de menús (File, View, Help), recibe
las acciones de los paneles —en particular `SimAction` de la
`StatusBar`— y las despacha al subproceso de Scilab vía el
`ScilabBridge`.

## Librerías internas

El modelo no se compila como un monolito: tres librerías estáticas
internas emergen de `core/` y `ui/`, cada una con su propia frontera
de dependencias, en un solo sentido:

| Librería | Qué contiene | Depende de |
|---|---|---|
| `scinodes_units` | parser de unidades, álgebra SI, `Quantity` | sólo stdlib |
| `scinodes_graph` | `NodeType`, `NodeInstance`, `NodeKind`, `NodeGraph`, gramática, `DimensionalAnalyzer`, undo/redo, registry custom | `scinodes_units`, json |
| `scinodes_plots` | 5 renderers de plot (`ui/plots/`) | `imgui_lib` |

(Una cuarta, `imgui_lib`, envuelve Dear ImGui con su *backend*
SDL2+Vulkan.) La consecuencia práctica: la batería de pruebas del
modelo enlaza sólo `scinodes_units` + `scinodes_graph` —sin nada
gráfico— y corre en milisegundos sin tocar la tarjeta de video.
`scinodes_units` y `scinodes_graph` están pensadas para una eventual
extracción a repos independientes cuando estabilicen.

Los archivos más grandes se dividen **por responsabilidad**, no por
tamaño arbitrario: `NodeCanvas` → `…Render`/`…Panels`/`…Popups`;
`View3DPanel` → `…Mesh`/`…Asset`; `NativeNodeRenderer` →
`…Node`/`…Link`/`…Interaction`. Cada *translation unit* comparte un
header `*Internal.hpp` con sus pares.

## Dependencias del *build*

CMake descarga vía `FetchContent`, en orden: SDL2
(`release-2.30.2`, estática), Dear ImGui (rama `docking`),
nlohmann/json (`v3.11.3`, *header-only*), glslang (`15.4.0`, para
compilar los shaders a SPIR-V) y tinygltf (`v2.9.3`, carga de
`.gltf` para los dispositivos). Vulkan no se descarga: lo busca con
`find_package(Vulkan REQUIRED)` y debe estar instalado en el
sistema.

## Dependencias de *runtime*

Sólo una: **Scilab 2026** o más nuevo. El editor lanza
`scilab-cli` como subproceso al pulsar Run y la simulación se
delega completamente a ese hijo. SciNodes no incluye solucionador
propio.

## Documentación

`doc/db/` contiene las tablas JSON que describen las entidades
reales del código en este *tag*: el catálogo de nodos, las
reglas de gramática, los items de menú, los atajos, los módulos,
las dependencias, los controles de simulación, los escenarios de
prueba, el formato `.scn`, y la metadata del *tag* mismo. Cada
afirmación del manual (usuario y desarrollador) se apoya en esas
tablas, y `tools/` contiene las auditorías que verifican la
correspondencia (más el hook de pre-commit que bloquea un commit con
la doc desincronizada). `doc/manual/` es el *mdBook* publicado; su
`book.toml` configura el tema, el enlace al repositorio público y el
despliegue a GitHub Pages.
