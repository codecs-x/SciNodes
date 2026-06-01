# Gramáticas R1–R7

SciNodes valida cada grafo contra siete reglas duras antes de
dejar correr la simulación.  Esta página las enumera con sus
implementaciones.

| Regla | Significado                                            | Implementación                                    |
|-------|--------------------------------------------------------|---------------------------------------------------|
| **R1** | Cada nodo tiene un NodeType conocido.                  | `NodeGraph::validateNode` consulta el registry.  |
| **R2** | Cada arista conecta tipos compatibles (scalar↔scalar). | `tryAddEdge` chequea `TypeExpr` matching.        |
| **R3** | Los sub-grafos están cerrados (no hay aristas sueltas que crucen). | Walker en `GrammarParser`.                       |
| **R4** | No hay ciclos algebraicos.  Los lazos pasan por al menos un nodo stateful. | Tarjan SCC en `GrammarParser::detectCycles`.    |
| **R5** | Cada sumidero conectado tiene una fuente alcanzable.   | DFS reverso en `GrammarParser::reachability`.    |
| **R6** | Reservado para extensión futura (constraint solving).  | No implementado.                                  |
| **R7** | Las unidades son coherentes en toda arista.            | `scinodes::analyzeUnits` (función libre en `DimensionalAnalyzer.cpp`). |

## Cómo se ejecuta la validación

Tres puntos:

1. **Edición incremental**: `tryAddEdge` aplica R1+R2+R7
   inmediatamente.  Si fallan, la arista no se crea y la UI
   muestra el motivo.
2. **Antes de Run**: `GrammarParser::validateFull` corre R1–R7
   sobre el grafo completo y devuelve una lista de
   diagnósticos.  El botón Run queda deshabilitado si hay
   errores duros.
3. **Al cargar `.scn`**: idéntico a "antes de Run".  Las
   aristas problemáticas quedan visibles pero rojas.

## Diagnósticos

`GrammarParser` produce un `vector<Diagnostic>` con severidad
+ posición + mensaje localizable.  La UI los muestra en el
panel de problemas y los superpone como markers rojos en
los nodos afectados.

## R7 en detalle

Ver [Análisis dimensional](dimensional-analysis.md).

## Cómo extender

Si necesitás una regla R8:

1. Implementala en `GrammarParser::validateFull`.
2. Define un código de diagnóstico nuevo en `Diagnostic.hpp`.
3. Traducí el mensaje en `i18n/es.json` y `i18n/en.json`.

Las reglas R1–R5 originales se diseñaron para que cualquier
grafo válido pueda generarse a Scilab sin información extra.
Si tu regla nueva no es necesaria para ese contrato,
considerala una **warning**, no un **error** — los warnings
muestran marker amarillo y no bloquean Run.
