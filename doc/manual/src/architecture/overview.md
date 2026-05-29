# Vista de las tres capas

SciNodes está dividido en tres capas con responsabilidades
claras.  Esta página es el mapa — cada página siguiente cubre
una capa o subsistema en detalle.

```
┌──────────────────────────────────────────────────────────────┐
│  Capa UI  (src/ui/)                                          │
│  ──────                                                      │
│  NodeCanvas, View3DPanel, StatusBar, OutlinerPanel, ...      │
│  Renderiza con Dear ImGui.  No conoce Scilab.                │
└──────────────────────────────────────────────────────────────┘
                            │
                            ▼   model = NodeGraph
                                renderer = INodeRenderer
┌──────────────────────────────────────────────────────────────┐
│  Capa Modelo  (src/core/, scinodes_graph)                    │
│  ───────────                                                 │
│  NodeGraph, NodeInstance, NodeType, NodeKind,                │
│  DimensionalAnalyzer, GrammarParser, UndoRedoStack,          │
│  ScilabCodeGen.  C++ puro.  Headless-testeable.              │
└──────────────────────────────────────────────────────────────┘
                            │
                            ▼   IComputeBackend
┌──────────────────────────────────────────────────────────────┐
│  Capa Backend  (src/core/ScilabBridge.cpp, …)                │
│  ─────────────                                               │
│  IComputeBackend.  Implementación actual: ScilabBridge       │
│  (subprocess scilab-cli + pipe + STATE protocol).            │
│  Reemplazable: SUNDIALS, FMI, Modelica son candidatos.       │
└──────────────────────────────────────────────────────────────┘
```

## Reglas duras entre capas

- **UI nunca llama a Scilab directamente**.  Va por
  `IComputeBackend`.  Esto permite testear toda la UI sin
  necesidad de Scilab corriendo.
- **Modelo nunca depende de ImGui**.  Si una mutación del
  grafo no se puede expresar como llamada a una función
  pura sobre `NodeGraph` con IDs, hay un bug de arquitectura.
- **Backend nunca conoce la estructura del grafo**.  Recibe
  un `.sce` ya generado y devuelve trazas.  La generación
  vive en `ScilabCodeGen` (capa modelo).

## Librerías internas

Para reducir acoplamiento dentro de la capa modelo, hay
sub-librerías estáticas:

| Librería            | Responsabilidad                                 |
|---------------------|-------------------------------------------------|
| `scinodes_units`    | Quantity, Unit, parser, álgebra dimensional.    |
| `scinodes_graph`    | NodeGraph, NodeType, NodeKind, Grammar, Analyzer. |
| `scinodes_plots`    | Renderers de plots (5: time, FFT, phase, log, sink). |

La regla de import:  
`scinodes_units` no depende de nadie.  
`scinodes_graph` depende de `scinodes_units`.  
`scinodes_plots` depende de `scinodes_graph` + ImGui.  
La app final depende de las tres + el resto.

## Idioma del código

Lo que sigue es una regla blanda pero útil para entender el
estilo del repo:

> **Cada librería externa es un idioma extranjero.  El código
> debe hablar SU propio idioma (gramáticas + interfaces) y
> traducir en la frontera.**

Por eso:

- Scilab vive detrás de `IComputeBackend`.  No hay `mxArray`
  ni `JavaVM` en el resto del código.
- Dear ImGui vive detrás de `INodeRenderer`.  No hay `ImVec2`
  cruzando la frontera de la capa modelo.
- glTF vive detrás de `IAssetService`.  Los nodos hablan en
  `geometry`, no en `cgltf_node`.

Ver memoria interna: [Anti-corruption layers](https://en.wikipedia.org/wiki/Domain-driven_design)
para el patrón original.

## Próximas páginas

- [Gramáticas R1–R7](grammar.md) — las cinco (más dos) reglas
  que validan un grafo.
- [NodeKind y dispatch polimórfico](node-kinds.md) — cómo se
  procesa cada nodo según su gramática.
- [Análisis dimensional](dimensional-analysis.md) — implementación
  de R7.
- [IComputeBackend](compute-backend.md) — el contrato hacia
  Scilab.
