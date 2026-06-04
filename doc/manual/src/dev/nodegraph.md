# Modelo: `NodeGraph`

`NodeGraph` (en `src/core/NodeGraph.{cpp,hpp}`) es el modelo del
grafo: una lista de `NodeInstance` y una lista de `Edge`, junto
con las operaciones de mutación que respetan la gramática.
No depende de Dear ImGui, SDL ni Vulkan; es 100 % testeable
*headless*.

## `NodeInstance`

```cpp
struct NodeInstance {
    int         id;          // único en el grafo (secuencial, lo asigna NodeGraph)
    NodeType    type;
    std::string customType;  // sólo si type == Custom: qué descriptor del
                             // CustomNodeRegistry usa esta instancia

    // Valor numérico de cada parámetro, indexado por nombre. Fuente para
    // el codegen, la GUI y el serializer.
    std::unordered_map<std::string, double>             params;

    // Capa dimensional: el mismo valor como Quantity (valor + Unit del
    // FieldDef). setParam mantiene fields[name].value == params[name];
    // es lo que consume el análisis dimensional (R7).
    std::unordered_map<std::string, scinodes::Quantity> fields;

    // Metadata string de sinks multicanal (labels y unidades de puerto
    // editables), persistida en el .scn.
    std::unordered_map<std::string, std::string>        stringParams;

    std::string assetPath;   // Device: ruta del glTF validado contra el contrato
    std::string comment;     // nota libre del usuario (F2; tooltip con Ctrl+hover)

    // Overrides per-instancia (stubs de SubGraph / unidades de puerto):
    std::unordered_map<int, TypeExpr> portTypeOverrides;
    std::unordered_map<int, Unit>     portUnitOverrides;
};
```

`params` guarda el valor crudo; `fields` lo refleja como `Quantity`
(valor + `Unit`) para el análisis dimensional. El catálogo
(`NodeType.cpp :: nodeRegistry()`) define, por tipo, qué parámetros
existen, sus defaults y su unidad; al crear una instancia se inicializan
desde ahí. La familia de comportamiento del nodo (`NodeKind`) **no se
almacena**: se deriva del `type` en la frontera del *bridge* (`kindOf`).

El modelo puro **no almacena la posición visual del nodo**. La gestiona
el canvas (`NodeCanvas`) en un mapa aparte, sincronizado con el grafo al
cargar y guardar `.scn`. Así `core/` queda libre de tipos de UI.

## *Attribute IDs* (nodo + puerto/param en un entero)

Puertos y params se direccionan con un único `int`. Cada nodo `N` reserva
un bloque de `kAttrIdNodeStride = 10000` ids:

| Rango (local al nodo) | Qué | Decodificación |
|---|---|---|
| `[0, 100)`      | puertos de **entrada** | `port = attrLocal` |
| `[100, 9000)`   | **params** (per-param pins) | `idx = attrLocal − 100` |
| `[9000, 10000)` | puertos de **salida**  | `port = attrLocal − 9000` |

con `attrNodeId(a) = a / 10000`, `attrLocal(a) = a % 10000` y los
predicados `attrIsOutput` / `attrIsParam` (todo en `NodeInstance.hpp`).
El rango de params es lo que habilita cablear directo a un parámetro
(*per-param pins*): un `Edge` cuyo `toAttrId` cae en `[100, 9000)` modula
un param en vez de entrar por un puerto.

## `Edge`

```cpp
struct Edge {
    int id;
    int fromNodeId;
    int toNodeId;
    int fromAttrId;   // salida del origen  (rango [9000, 10000))
    int toAttrId;     // entrada o param del destino
};
```

El `Edge` no guarda el puerto por separado: nodo + puerto/param van
codificados en `fromAttrId` / `toAttrId` con el esquema de arriba.

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

1. **Tests *headless*.** `test_grammar` ejerce sus aserciones
   (R0–R7, alcanzabilidad, operaciones del `NodeGraph`, undo/redo,
   per-param pins, álgebra de unidades, …) sin instanciar nada del
   entorno gráfico.
2. **Reuso.** `ScilabCodeGen` y `ScnSerializer` trabajan sobre el
   `NodeGraph`, no sobre el canvas. El generador no sabe ni le
   importa qué backend gráfico está activo.
3. **Espacio para crecer.** Cambiar la UI —otro canvas, otro
   *renderer*— no obliga a tocar la lógica del grafo.

Esa estricta separación entre modelo y UI es la primera decisión
arquitectónica del proyecto.
