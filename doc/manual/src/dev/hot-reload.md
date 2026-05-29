# Hot-reload Pause → Edit → Resume

Cuando el solver está en `Pause`, el `NodeGraph` puede
mutarse libremente.  Al `Resume` el `ScilabBridge` aplica un
diff entre el grafo antes/después y empuja al solver solo
los deltas:

- *Aditivos* (`addNode`, `addEdge`) — se compilan sus
  expresiones y se acolan al *codegen* del próximo `step`.
- *Destructivos* (`removeNode`, `removeEdge`) — se descarga
  el estado asociado, se borran las expresiones y se
  emiten `clear()` al Scilab.
- Cambios de parámetro — se aplican via `sendParameter` sin
  recompilación.

El estado \\((t, x)\\) sobrevive porque el vector `x` se
guarda al pausar y se restaura al reanudar.  Buffers de
plot incluidos.
