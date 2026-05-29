# `NodeKind` variant + dispatch polimórfico (C++20)

A partir de esta versión la gramática se modela como una
jerarquía polimórfica sobre `NodeKind`, no como un *switch*
plano sobre `NodeType`.

## `NodeKind`

```cpp
struct SourceKind;
struct StatelessTransformerKind;
struct StatefulTransformerKind;
struct SinkKind;
struct SubGraphKind;
struct AliasKind;
struct CustomKind;

using NodeKind = std::variant<SourceKind, StatelessTransformerKind,
                              StatefulTransformerKind, SinkKind,
                              SubGraphKind, AliasKind, CustomKind>;
```

Cada *kind* declara su interfaz: cuántos puertos consume,
cómo se piensa el codegen, qué unidades propaga, cómo se
dibuja el ícono en el popup.

## Dispatch

`std::visit` con un visitor por *cliente*:

```cpp
GeneratedSpec spec = std::visit(CodegenVisitor{ctx}, node.kind);
Unit unit         = std::visit(UnitPropagationVisitor{ctx}, node.kind);
```

El compilador C++20 exige cubrir cada alternativa del
*variant*, así que agregar un `Kind` nuevo da error en
todos los *visitors* hasta que se implementa el caso.
Garantía estática de exhaustividad.

## Beneficios prácticos

- Codegen, validación dimensional, render del popup y
  análisis de scope viven en *visitors* separados, no en
  *switches* paralelos sincronizados a mano.
- `DimensionalAnalyzer` ya no toma decisiones por
  `NodeType`; dispatcha sobre `NodeKind`.
- *Custom checks* en codegen + *factory dispatch* unificada.
