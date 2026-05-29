# `ScilabBridge`

`src/core/ScilabBridge.{cpp,hpp}` posee el subproceso de
`scilab-cli` y pipea comandos / lee estados.

## Lifecycle

- **`reset(graph)`** — genera el script con
  `ScilabCodeGen::generate(graph)`, lanza `scilab-cli`,
  espera el `READY`, asigna los *ring buffers* de cada
  sumidero.
- **`step(dt)`** — envía `step <t>`, parsea el `STATE`,
  empuja muestras a los buffers.
- **`stop()`** — envía `quit`, espera el `BYE`, *reaps* el
  child.

## Hilo dedicado del solver

`startSolverThread(dt)` lanza un *worker* que llama
`step()` a paso fijo `dt = 1/60 s`.  El hilo es el **único**
escritor del *pipe*; las llamadas de `sendParameter` desde
la UI se acolan vía un *mutex* y el hilo las drena al borde
del *tick*.

## Live tuning

`sendParameter(nodeId, paramIdx, value)` escribe
`SETPARAM <node>.<param> <value>` al subproceso.  El driver
Scilab lo recoge entre `step`s.
