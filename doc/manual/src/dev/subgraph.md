# SubGraph en core

`NodeKind::SubGraph` agrupa un sub-grafo con sus *stubs* de
entrada/salida (`SubGraphInput`, `SubGraphOutput`).  El
codegen aplana cada `SubGraph` antes de emitir Scilab.

## Persistencia `.scn` 0.4

```json
{ "id": 7, "type": "SubGraph", "label": "Lazo control",
  "subgraph": { "nodes": [...], "edges": [...] } }
```

La sub-estructura se serializa recursivamente.  `Undo/Redo`
captura el grafo completo (incluyendo subgraphs) como un
solo *snapshot*.

## Recursividad de la gramática

R1–R5 se aplican a cada nivel.  El validador descende por
`subGraphOf(...)` cuando una arista cruza la frontera del
sub-grafo, así un `SubGraph` mal formado bloquea su propia
inserción al canvas padre.
