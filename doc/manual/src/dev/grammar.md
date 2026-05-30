# Gramática

`GrammarParser` (en `src/core/GrammarParser.{cpp,hpp}`) es una
clase pura sin dependencias de UI cuya única tarea es decidir si
una operación sobre el grafo es legal. La interfaz pública tiene
dos funciones: `validateEdge`, que decide en O(1) si una arista
propuesta cumple las reglas, y `reachable`, que hace BFS desde las
fuentes para verificar que existe un camino hasta algún sumidero.

## Las seis reglas

`validateEdge` evalúa seis reglas en este orden:

1. **R3** — no auto-conexión (`from.id ≠ to.id`).
2. **R1** — el origen tiene puerto de salida (`from.outputPorts > 0`).
3. **R2** — el destino tiene puerto de entrada (`to.inputPorts > 0`).
4. **R4** — no hay ya una arista entre el mismo par.
5. **R5** — el destino tiene aún puertos de entrada libres
   (`count(edges → to) < to.inputPorts`).
6. **R0** — las categorías son compatibles. Los pares válidos son
   `S → T`, `S → Sk`, `T → T` y `T → Sk`. Cualquier otra
   combinación se rechaza.

El primer rechazo gana. Cada regla devuelve un `GrammarError` con
su código (`"R0"` … `"R5"`), un mensaje listo para mostrar
—por ejemplo *"All input ports of \"Summation\" are already
connected."* para R5— y los IDs de los nodos involucrados. Si
ninguna regla falla, la función devuelve `std::nullopt` y la
arista se acepta.

## Categorías

Tres valores en el `enum class NodeCategory`:

```cpp
enum class NodeCategory { Source, Transformer, Sink };
```

El catálogo en `NodeType.cpp` etiqueta cada tipo con una
categoría:

- **Source** — `outputPorts > 0`, `inputPorts == 0`.
- **Transformer** — `inputPorts > 0`, `outputPorts > 0`.
- **Sink** — `inputPorts > 0`, `outputPorts == 0`.

R1, R2 y R0 son consecuencia de estas invariantes. R1 y R2
chequean los conteos directamente; R0 verifica la combinación de
categorías y aporta un mensaje más legible para el usuario.

## Alcanzabilidad

`reachable(graph)` arranca un BFS desde cada nodo `Source` y
devuelve `true` si toca al menos un nodo `Sink`. La barra de
estado lo usa como aviso no bloqueante: si el grafo es
gramaticalmente válido pero ningún sumidero es alcanzable, el
editor muestra "el grafo no llega a ningún sumidero" sin impedir
la edición. Cuando el usuario pulse Run, la simulación arranca
igual pero no habrá nada que dibujar en el panel de plots.

## Un único punto de aplicación

`NodeGraph::tryAddEdge` consulta a `GrammarParser::validateEdge`
antes de aceptar cualquier arista. El canvas en `ui/` no
implementa lógica de gramática por su cuenta; sólo propone
operaciones al `NodeGraph` y dibuja el resultado. Cargar un
`.scn` también pasa por el mismo punto: el `ScnSerializer`
intenta reinsertar cada arista con `tryAddEdge`, las que la
gramática rechaza se acumulan en `LoadReport::rejectedEdges` con
su código de regla, y el grafo se abre en modo de sólo lectura.

La consecuencia es que las reglas se aplican uniformemente en el
editor en ejecución, en la carga de archivos, y en los tests
*headless*. Cualquier cambio futuro a la gramática se hace en un
único lugar.

## Tests

`test_grammar.cpp` cubre 400 aserciones en runtime, repartidas
entre las seis reglas, la alcanzabilidad, las operaciones del
`NodeGraph` (`addNode`, `tryAddEdge`, `removeNode`, `setParam`)
y el ciclo `undo/redo`. Las invocaciones textuales de
`EXPECT_*` (`EXPECT_TRUE`, `EXPECT_FALSE`, `EXPECT_VALID`,
`EXPECT_INVALID`, `EXPECT_RULE`) se ejercen más veces que las
que aparecen en el código porque varias viven dentro de loops
sobre el catálogo de nodos. La
suite corre en milisegundos y no requiere Scilab. Es la primera
línea de regresión cuando se amplía la gramática.
