# Arquitectura interna: librerías + dispatch polimórfico

A partir de v0.1.0 el árbol `src/` se compila en cuatro
artefactos: el binario `SciNodes` (que une todo) y tres
librerías estáticas internas que aíslan responsabilidades.

## Las tres librerías internas

```
scinodes_units   (sin dependencias del proyecto)
    └─ src/core/Unit.cpp + Quantity.cpp + UnitParser.cpp
       + UnitCatalog.cpp

scinodes_graph   (depende de scinodes_units)
    └─ src/core/NodeType.cpp + NodeInstance.cpp + NodeGraph.cpp
       + GrammarParser.cpp + Field.cpp + DimensionalAnalyzer.cpp
       + UndoRedoStack.cpp + CustomNodeRegistry.cpp

scinodes_plots   (depende de scinodes_graph, ImGui)
    └─ src/ui/plots/Wave + Phase + Spectrum + Heatmap
       + Histogram renderers
```

`SciNodes` (binario) las consume y agrega lo que requiere
Vulkan / SDL2 / glTF / Scilab (los paneles, los backends, el
visor 3-D, la persistencia, el bridge). La regla de
dependencias es **acíclica y vertical**: `units → graph →
plots → SciNodes`.

Esta separación deja construir un binario auxiliar
(`test_grammar`, `dump_catalog`, `example_library_query`, ...)
linkeando sólo lo necesario, sin arrastrar SDL ni Vulkan. La
suite `test_grammar` corre en milisegundos porque sólo enlaza
`scinodes_units + scinodes_graph`.

## Dispatch polimórfico sobre `NodeKind`

El dispatch sobre tipos de nodo dejó de ser un `switch
(node.type)` repetido por toda la base de código y se
convirtió en una **jerarquía cerrada de `Kind`s** sobre
`std::variant`:

```cpp
struct SignalSourceKind { ... };
struct StatefulTransformerKind { ... };
struct DeviceKind { ... };
struct GeometryTransformerKind { ... };
// ...

using NodeKind = std::variant<
    SignalSourceKind, StatefulTransformerKind,
    DeviceKind, GeometryTransformerKind, ...>;
```

Los pasos del editor (gramática, codegen, walker 3-D, análisis
dimensional, checks de custom nodes) se implementan como
visitors:

```cpp
auto units = std::visit(InferUnitsVisitor{...}, kind);
auto plan  = std::visit(PlanNodeVisitor{...}, kind);
auto eval  = std::visit(EvalVec3Visitor{...}, kind);
```

La regla es: **un solo `discriminator boundary`** en la capa
de bridge (`kindOf(node.type)` mapea `NodeType` → `NodeKind`).
A partir de ahí, los `switch`s desaparecen. Agregar un
NodeType nuevo significa definir su `Kind` (si no encaja en uno
existente) y sus implementaciones para los visitors; el
compilador exige cobertura total.

## Split de los archivos grandes

Tres componentes que pasaban los 1500 LOC se dividen por
responsabilidad:

- **`NodeCanvas`** → backbone + popup handlers + selection +
  shortcuts.
- **`View3DPanel`** → panel base + mesh procedural + asset
  glTF.
- **`NativeNodeRenderer`** → backbone + link drawing + style +
  interaction.

Ningún archivo del repo supera los 1000 LOC tras este split.

## Validación: `features.md` + auditoría triple

La sincronización entre la documentación y el código se
verifica con tres estrategias independientes (PARSE,
INTROSPECT, RUNTIME) que el script `tools/triple_audit.py`
ejecuta en cada *tag*. La auditoría se complementa con
`features.md` —un inventario exhaustivo del catálogo, los
atajos, los formatos y las capacidades— verificado contra el
código antes de cerrar el milestone.
