# Modelo `NodeGraph`

`NodeGraph` es el modelo del editor.  Vive en `src/core/`,
no conoce ImGui ni Vulkan.

## Estructuras

- `NodeInstance` — un nodo en el canvas con id, tipo,
  parámetros editables y posición.
- `Edge` — `{fromNodeId, fromAttrId, toNodeId, toAttrId}`.
- `NodeGraph` — vector de nodos + vector de aristas.

## API mutante

- `addNode(NodeType, x, y)` devuelve el `id` nuevo.
- `removeNode(id)` borra el nodo y sus aristas adyacentes.
- `tryAddEdge(...)` valida contra la gramática **antes** de
  insertar; devuelve `bool` + código de error.
- `setParam(nodeId, key, value)` muta un parámetro.

## Snapshots

`UndoRedoStack` toma *snapshots* del grafo (movimiento
profundo, sin alocar copias innecesarias) y los acomoda en
dos pilas acotadas a 128 entradas.
