# Modelo: `NodeGraph`

`NodeGraph` (en `src/core/NodeGraph.{cpp,hpp}`) es el modelo del
grafo: una lista de `NodeInstance` y una lista de `Edge`, junto
con las operaciones de mutación que respetan la gramática.
No depende de Dear ImGui, SDL ni Vulkan; es 100 % testeable
*headless*.

## `NodeInstance`

```cpp
struct NodeInstance {
    int      id;        // único en el grafo (asignado por NodeGraph)
    NodeType type;
    std::unordered_map<std::string, double> params;

    int inputAttrId(int port = 0) const { return id * 10000 + port; }
    int outputAttrId()            const { return id * 10000 + 9000; }
};
```

Los parámetros son un mapa `nombre → valor`. El catálogo
(`NodeType.cpp :: nodeRegistry()`) define para cada tipo qué
nombres existen, sus valores por defecto y la unidad de display.
Cuando `tryAddNode` crea una instancia, los parámetros se
inicializan a partir del catálogo.

El modelo puro **no almacena la posición visual del nodo**. La
gestiona el canvas (`NodeCanvas`) en un mapa
`ScnPositions = unordered_map<int, ScnVec2>` separado, cuyas
entradas se sincronizan con el grafo al cargar y guardar `.scn`.
Esa decisión mantiene a `core/` libre de tipos de UI.

Los helpers `inputAttrId` y `outputAttrId` traducen el `id` del
nodo a *attribute IDs* del esquema de `imnodes`, que codifican
nodo y puerto en un único entero.

## `Edge`

```cpp
struct Edge {
    int id;
    int fromNodeId;
    int toNodeId;
    int fromAttrId;   // = fromNodeId * 10000 + 9000
    int toAttrId;     // = toNodeId   * 10000 + toPort
};
```

`fromAttrId` y `toAttrId` codifican nodo + puerto en el formato
de `imnodes`. Dado un *attribute ID*, el nodo se recupera con
`attrId / 10000` y la posición o el tipo de puerto con
`attrId % 10000` (0–8999 para entradas, 9000+ para salidas).

## Operaciones

| Método                                  | Qué hace |
|-----------------------------------------|----------|
| `addNode(type)`                         | Agrega; devuelve el `id` asignado. |
| `removeNode(id)`                        | Elimina; arrastra las aristas conectadas. |
| `tryAddEdge(fromAttrId, toAttrId)`      | Toma los *attribute IDs* (codifican nodo+puerto). Consulta a `GrammarParser::validateEdge`. Devuelve `std::optional<GrammarError>` con el código y mensaje si rechaza; si acepta, registra la arista y devuelve `std::nullopt`. |
| `removeEdge(edgeId)`                    | Elimina una arista por su id. |
| `setParam(nodeId, name, value)`         | Cambia un parámetro por nombre. |

`addNode` y `removeNode` no nominan a la gramática —agregar un
nodo aislado o borrarlo es siempre legal—. La validación
solamente entra para `tryAddEdge`. Los *snapshots* del
`UndoRedoStack` los empuja el `NodeCanvas` (no el `NodeGraph`)
cuando cierra una transacción del usuario, para que un solo gesto
de arrastre o de edición continua no inunde la pila.

## `UndoRedoStack`

Pila bidireccional con capacidad máxima
`UndoRedoStack::MAX_DEPTH = 50` (en `UndoRedoStack.hpp`). Cuando
se empuja un *snapshot* y la pila excede el tope, se descarta el
más antiguo. Cada *snapshot* es una copia profunda del
`NodeGraph`; los grafos son pequeños —decenas, cientos de
nodos— así que la copia es trivial en costo.

`undo(currentSnapshot)` retrocede al *snapshot* anterior y empuja
el actual a la pila de *redo*; `redo()` hace la operación inversa.

## Por qué un modelo separado de la UI

Tres razones:

1. **Tests *headless*.** `test_grammar` ejerce las 117 aserciones
   sobre R0–R5, alcanzabilidad y undo/redo sin instanciar nada
   del entorno gráfico.
2. **Reuso.** `ScilabCodeGen` y `ScnSerializer` trabajan sobre el
   `NodeGraph`, no sobre el canvas. El generador no sabe ni le
   importa qué backend gráfico está activo.
3. **Espacio para crecer.** Cambiar la UI —otro canvas, otro
   *renderer*— no obliga a tocar la lógica del grafo.

Esa estricta separación entre modelo y UI es la primera decisión
arquitectónica del proyecto.
