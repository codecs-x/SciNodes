# `ScilabCodeGen`

Traduce un `NodeGraph` a un *script* Scilab con un protocolo
*REPL* simple sobre `stdin`/`stdout`:

| Comando entrante  | Respuesta                          |
|-------------------|------------------------------------|
| `step <t>`        | `STATE v1 v2 ... vN` (uno por sink) |
| `quit`            | `BYE`                              |

Al arrancar imprime `READY\n` para que el `ScilabBridge`
sepa sincronizarse.

## Pasos del codegen

1. **`topoSort(graph)`** ordena los nodos en orden de
   evaluación, rompiendo aristas que entran a *pure state*
   para que ciclos cerrados sorteen correctamente.
2. **`planNode(...)`** emite la expresión de cada nodo:
   - Sources: literal o función de `t`.
   - Stateless: combinación de sus inputs (`u_i` arriba en
     la topología).
   - Stateful: declara `dxdt(slot) = ...` y lee
     `x(slot)` como su salida.
3. **`emit dynamics(t, x)`** que devuelve `dxdt` para
   `ode("rk", x0, t_prev, t, dynamics)`.
4. **Bucle `while step`** que lee `t`, integra, imprime el
   `STATE` y vuelve a leer.

## Tipos soportados

Sources, transformers (stateless + stateful), sinks, alias,
custom nodes (JSON).  La cobertura exacta se chequea en
`tests/test_grammar.cpp` que enumera cada `NodeType` con
`isSupported(t)`.
